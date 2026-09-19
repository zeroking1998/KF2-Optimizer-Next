// Process-local session classifier installed by the protected viewport. Its
// context receipt remains read-only. A separate authenticated loopback actor
// applies only reversible local GFXSettings; it performs no gameplay-actor
// iteration, console command or replicated write.
class KF2OptimizerOnlineContextInteraction extends Interaction
    within GameViewportClient;

var string LastReportedContext;
var float LastObservedRealTime;
var KF2OptimizerAdaptiveControlListener OnlineGraphicsListener;
var KF2OptimizerAdaptiveGraphicsState OnlineGraphicsState;
var int OnlineGraphicsLastSequence;
var bool bOnlineGraphicsEnabled;

function bool ValidOnlineGraphicsToken(string Candidate)
{
    return Len(class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken) ==
               32 &&
           Candidate ==
               class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken;
}

function bool GetOnlineWorld(out WorldInfo CurrentWorld)
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;

    if (GamePlayers.Length == 0) return false;
    PrimaryPlayer = GamePlayers[0];
    if (PrimaryPlayer == None) return false;
    PrimaryController = PrimaryPlayer.Actor;
    if (PrimaryController == None) return false;
    CurrentWorld = PrimaryController.WorldInfo;
    return CurrentWorld != None &&
        (CurrentWorld.NetMode == NM_Client ||
         CurrentWorld.NetMode == NM_ListenServer) &&
        !(CurrentWorld.GetMapName(true) ~= "KFMainMenu");
}

function KF2OptimizerAdaptiveGraphicsState GetOnlineGraphicsState()
{
    if (OnlineGraphicsState == None)
    {
        OnlineGraphicsState = new(self)
            class'KF2OptimizerAdaptiveGraphicsState';
    }
    return OnlineGraphicsState;
}

function bool ApplyOnlineGraphicsControl(
    string Token, int Sequence, string Resource, int Quality)
{
    local WorldInfo CurrentWorld;
    local KF2OptimizerAdaptiveGraphicsState CurrentState;

    if (!GetOnlineWorld(CurrentWorld) ||
        !ValidOnlineGraphicsToken(Token) || Sequence <= 0 ||
        Sequence <= OnlineGraphicsLastSequence ||
        Quality < 10 || Quality > 100)
    {
        return false;
    }
    if (Resource ~= "enable")
    {
        bOnlineGraphicsEnabled = true;
        OnlineGraphicsLastSequence = Sequence;
        `log("KF2OPT_ONLINE_GRAPHICS state=enabled readback=verified");
        return true;
    }
    CurrentState = GetOnlineGraphicsState();
    if (CurrentState == None) return false;
    if (Resource ~= "disable")
    {
        if (!class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
                CurrentState))
        {
            return false;
        }
        bOnlineGraphicsEnabled = false;
        OnlineGraphicsLastSequence = Sequence;
        `log("KF2OPT_ONLINE_GRAPHICS state=disabled readback=verified");
        return true;
    }
    if (!bOnlineGraphicsEnabled ||
        !((Resource ~= "gpu") || (Resource ~= "vram") ||
          (Resource ~= "ram") || (Resource ~= "overdraw") ||
          (Resource ~= "effects") || (Resource ~= "recover")))
    {
        return false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            CurrentState, Resource, Quality))
    {
        return false;
    }
    OnlineGraphicsLastSequence = Sequence;
    `log("KF2OPT_ONLINE_GRAPHICS state=applied seq="$Sequence$
         " resource="$Resource$" quality="$Quality$
         " readback=verified");
    return true;
}

function EnsureOnlineGraphicsListener(PlayerController PrimaryController)
{
    if (OnlineGraphicsListener != None &&
        !OnlineGraphicsListener.bDeleteMe)
    {
        return;
    }
    OnlineGraphicsListener = PrimaryController.Spawn(
            class'KF2OptimizerAdaptiveControlListener');
    if (OnlineGraphicsListener == None || OnlineGraphicsListener.bDeleteMe)
    {
        `log("KF2OPT_ONLINE_GRAPHICS_BRIDGE state=unavailable reason=spawn_failed");
    }
}

function RestoreOnlineGraphicsAtMainMenu()
{
    if (OnlineGraphicsState != None &&
        OnlineGraphicsState.bOriginalCaptured)
    {
        if (class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
                OnlineGraphicsState))
        {
            `log("KF2OPT_ONLINE_GRAPHICS state=restored boundary=main_menu");
        }
        else
        {
            `log("KF2OPT_ONLINE_GRAPHICS state=restore_failed boundary=main_menu");
        }
    }
    bOnlineGraphicsEnabled = false;
    OnlineGraphicsLastSequence = 0;
    OnlineGraphicsListener = None;
}

function ReportSessionContext(
    string State, string NetModeName, string MapName)
{
    local string Context;

    Context = State$"|"$NetModeName$"|"$MapName;
    if (Context == LastReportedContext)
    {
        return;
    }
    LastReportedContext = Context;
    `log("KF2OPT_SESSION_CONTEXT schema=1 state="$State$
         " net_mode="$NetModeName$" map="$MapName);
}

event Tick(float DeltaTime)
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local string MapName;

    if (GamePlayers.Length == 0)
    {
        return;
    }
    PrimaryPlayer = GamePlayers[0];
    if (PrimaryPlayer == None)
    {
        return;
    }
    PrimaryController = PrimaryPlayer.Actor;
    if (PrimaryController == None)
    {
        return;
    }
    CurrentWorld = PrimaryController.WorldInfo;
    if (CurrentWorld == None)
    {
        return;
    }

    // RealTimeSeconds restarts with each world. Clear the value-only receipt
    // without retaining WorldInfo across map teardown.
    if (CurrentWorld.RealTimeSeconds < LastObservedRealTime)
    {
        LastReportedContext = "";
        OnlineGraphicsListener = None;
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    MapName = CurrentWorld.GetMapName(true);
    if (MapName ~= "KFMainMenu")
    {
        RestoreOnlineGraphicsAtMainMenu();
        LastReportedContext = "";
        return;
    }
    if (CurrentWorld.NetMode == NM_Client)
    {
        ReportSessionContext(
            "online_client_read_only", "NM_Client", MapName);
        EnsureOnlineGraphicsListener(PrimaryController);
    }
    else if (CurrentWorld.NetMode == NM_ListenServer)
    {
        ReportSessionContext(
            "online_host_read_only", "NM_ListenServer", MapName);
        EnsureOnlineGraphicsListener(PrimaryController);
    }
    else if (CurrentWorld.NetMode == NM_Standalone)
    {
        ReportSessionContext("offline_full", "NM_Standalone", MapName);
    }
}

defaultproperties
{
}
