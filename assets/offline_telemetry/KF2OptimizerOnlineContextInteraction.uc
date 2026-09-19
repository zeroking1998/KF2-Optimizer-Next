// Process-local, read-only session classifier installed by the protected
// viewport. It deliberately performs no actor iteration, spawning, control,
// console commands or replicated writes. Online capabilities remain disabled
// until the native optimizer receives this process-bound context receipt.
class KF2OptimizerOnlineContextInteraction extends Interaction
    within GameViewportClient;

var string LastReportedContext;
var float LastObservedRealTime;

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
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    MapName = CurrentWorld.GetMapName(true);
    if (MapName ~= "KFMainMenu")
    {
        LastReportedContext = "";
        return;
    }
    if (CurrentWorld.NetMode == NM_Client)
    {
        ReportSessionContext(
            "online_client_read_only", "NM_Client", MapName);
    }
    else if (CurrentWorld.NetMode == NM_ListenServer)
    {
        ReportSessionContext(
            "online_host_read_only", "NM_ListenServer", MapName);
    }
    else if (CurrentWorld.NetMode == NM_Standalone)
    {
        ReportSessionContext("offline_full", "NM_Standalone", MapName);
    }
}

defaultproperties
{
}
