// Process-local session classifier installed by the protected viewport. Its
// context receipt remains read-only. A separate authenticated loopback actor
// applies reversible local GFXSettings and bounded local corpse-pool actions;
// it performs no console command or replicated write.
class KF2OptimizerOnlineContextInteraction extends Interaction
    within GameViewportClient;

var string LastReportedContext;
var float LastObservedRealTime;
var KF2OptimizerAdaptiveGraphicsState OnlineGraphicsState;
var int OnlineGraphicsLastSequence;
var bool bOnlineGraphicsEnabled;
var bool bOnlineFixedEffectsApplied;
var bool bOnlineGraphicsListenerStarted;
var bool bOnlineCorpseCapabilityReported;
var bool bOnlineCorpsePoolObserved;
var bool bOnlineCorpseSleepArmed;
var bool bOnlineCorpseSleepApplied;
var float OnlineCorpseLastCapacityRealTime;

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

function bool IsOnlineAdaptiveEnabled()
{
    return bOnlineGraphicsEnabled;
}

function bool EnsureOnlineFixedEffectsBaseline()
{
    local KF2OptimizerAdaptiveGraphicsState CurrentState;

    if (bOnlineFixedEffectsApplied)
    {
        return true;
    }
    CurrentState = GetOnlineGraphicsState();
    if (CurrentState == None ||
        !class'KF2OptimizerAdaptiveGraphics'.static.
            ApplyFixedSessionEffects(CurrentState))
    {
        return false;
    }
    bOnlineFixedEffectsApplied = true;
    `log("KF2OPT_FIXED_EFFECT_BASELINE state=applied mode=online"$
         " quality="$class'KF2OptimizerAdaptiveGraphics'.static.
            GetFixedSessionEffectsQuality()$" readback=verified");
    return true;
}

function bool ApplyOnlineGraphicsControl(
    string Token, int Sequence, string Resource, int Quality)
{
    local WorldInfo CurrentWorld;
    local KF2OptimizerAdaptiveGraphicsState CurrentState;
    local KFGoreManager GoreManager;

    if (!GetOnlineWorld(CurrentWorld) ||
        !ValidOnlineGraphicsToken(Token) || Sequence <= 0 ||
        Sequence <= OnlineGraphicsLastSequence)
    {
        return false;
    }
    if (Resource ~= "enable")
    {
        if (Quality < 4 || Quality > 2000)
        {
            return false;
        }
        GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
        if (GoreManager == None)
        {
            return false;
        }
        GoreManager.MaxDeadBodies = Quality;
        if (GoreManager.MaxDeadBodies != Quality)
        {
            return false;
        }
        CurrentState = GetOnlineGraphicsState();
        if (CurrentState == None || !EnsureOnlineFixedEffectsBaseline())
        {
            return false;
        }
        bOnlineGraphicsEnabled = true;
        bOnlineCorpseSleepArmed = true;
        bOnlineCorpseSleepApplied = false;
        OnlineGraphicsLastSequence = Sequence;
        `log("KF2OPT_ONLINE_GRAPHICS state=enabled corpse_maximum="$Quality$
             " fixed_effect_quality="$
             class'KF2OptimizerAdaptiveGraphics'.static.
                GetFixedSessionEffectsQuality()$
             " local_only=true readback=verified");
        return true;
    }
    if (Quality < 10 || Quality > 100)
    {
        return false;
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
        bOnlineFixedEffectsApplied = false;
        if (!EnsureOnlineFixedEffectsBaseline())
        {
            return false;
        }
        bOnlineGraphicsEnabled = false;
        bOnlineCorpseSleepArmed = false;
        OnlineGraphicsLastSequence = Sequence;
        `log("KF2OPT_ONLINE_GRAPHICS state=disabled fixed_effect_quality="$
             class'KF2OptimizerAdaptiveGraphics'.static.
                GetFixedSessionEffectsQuality()$
             " readback=verified");
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

function bool TrySleepOneOnlineCorpse(WorldInfo CurrentWorld)
{
    local int Index;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;

    if (!bOnlineGraphicsEnabled || !bOnlineCorpseSleepArmed ||
        bOnlineCorpseSleepApplied || CurrentWorld == None)
    {
        return false;
    }
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return false;
    }
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None ||
            Candidate.IsAliveAndWell() || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0 ||
            CurrentWorld.TimeSeconds - Candidate.TimeOfDeath < 1.5 ||
            Candidate.SpecialMove == SM_DeathAnim ||
            Candidate.Physics != PHYS_RigidBody ||
            !Candidate.Mesh.RigidBodyIsAwake())
        {
            continue;
        }
        Candidate.Mesh.PutRigidBodyToSleep();
        if (Candidate.Mesh.RigidBodyIsAwake())
        {
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=failed action=sleep"$
                 " corpse_id="$string(Candidate.Name)$
                 " reason=readback_mismatch local_only=true");
            return false;
        }
        bOnlineCorpseSleepApplied = true;
        bOnlineCorpseSleepArmed = false;
        `log("KF2OPT_ONLINE_CORPSE_ACTION state=sleep corpse_id="$
             string(Candidate.Name)$" pool="$GoreManager.CorpsePool.Length$
             " awake=false local_only=true readback=verified");
        return true;
    }
    return false;
}

function bool TryEnforceOnlineCorpseCapacity(WorldInfo CurrentWorld)
{
    local int Index;
    local int PoolBefore;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;
    local string CandidateName;

    if (!bOnlineGraphicsEnabled || CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds - OnlineCorpseLastCapacityRealTime < 0.45)
    {
        return false;
    }
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None || GoreManager.MaxDeadBodies < 4 ||
        GoreManager.CorpsePool.Length <= GoreManager.MaxDeadBodies)
    {
        return false;
    }
    PoolBefore = GoreManager.CorpsePool.Length;
    for (Index = 0; Index < PoolBefore; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None ||
            Candidate.IsAliveAndWell() || Candidate.TimeOfDeath <= 0.0 ||
            CurrentWorld.TimeSeconds - Candidate.TimeOfDeath < 1.5 ||
            Candidate.SpecialMove == SM_DeathAnim)
        {
            continue;
        }
        CandidateName = string(Candidate.Name);
        OnlineCorpseLastCapacityRealTime = CurrentWorld.RealTimeSeconds;
        if (GoreManager.RemoveAndDeleteCorpse(Index) &&
            GoreManager.CorpsePool.Length == PoolBefore - 1)
        {
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=capacity corpse_id="$
                 CandidateName$" pool_before="$PoolBefore$
                 " pool_after="$GoreManager.CorpsePool.Length$
                 " maximum="$GoreManager.MaxDeadBodies$
                 " local_only=true readback=verified");
            return true;
        }
        `log("KF2OPT_ONLINE_CORPSE_ACTION state=failed action=capacity"$
             " pool_before="$PoolBefore$" pool_after="$
             GoreManager.CorpsePool.Length$
             " maximum="$GoreManager.MaxDeadBodies$
             " reason=readback_mismatch local_only=true");
        return false;
    }
    return false;
}

function EnsureOnlineGraphicsListener(PlayerController PrimaryController)
{
    local KF2OptimizerAdaptiveControlListener NewListener;

    if (bOnlineGraphicsListenerStarted)
    {
        return;
    }
    NewListener = PrimaryController.Spawn(
            class'KF2OptimizerAdaptiveControlListener');
    if (NewListener == None || NewListener.bDeleteMe)
    {
        `log("KF2OPT_ONLINE_GRAPHICS_BRIDGE state=unavailable reason=spawn_failed");
        return;
    }
    // Do not retain an Actor reference from this viewport-owned Interaction.
    // The listener belongs to the current World and must be collectible with
    // it during server map travel.
    bOnlineGraphicsListenerStarted = true;
}

function ReportOnlineCorpseCapability(WorldInfo CurrentWorld)
{
    local KFGoreManager GoreManager;

    if (CurrentWorld == None)
    {
        return;
    }
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None)
    {
        `log("KF2OPT_ONLINE_CORPSE state=unavailable reason=no_gore_manager"$
             " local_only=true readback=verified");
        return;
    }
    if (!bOnlineCorpseCapabilityReported)
    {
        bOnlineCorpseCapabilityReported = true;
        `log("KF2OPT_ONLINE_CORPSE state=available pool="$
             GoreManager.CorpsePool.Length$" maximum="$GoreManager.MaxDeadBodies$
             " local_only=true readback=verified");
    }
    if (!bOnlineCorpsePoolObserved && GoreManager.CorpsePool.Length > 0)
    {
        bOnlineCorpsePoolObserved = true;
        `log("KF2OPT_ONLINE_CORPSE state=populated pool="$
             GoreManager.CorpsePool.Length$" maximum="$GoreManager.MaxDeadBodies$
             " local_only=true readback=verified");
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
    bOnlineFixedEffectsApplied = false;
    OnlineGraphicsLastSequence = 0;
    bOnlineGraphicsListenerStarted = false;
    bOnlineCorpseCapabilityReported = false;
    bOnlineCorpsePoolObserved = false;
    bOnlineCorpseSleepArmed = false;
    bOnlineCorpseSleepApplied = false;
    OnlineCorpseLastCapacityRealTime = 0.0;
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
        bOnlineGraphicsListenerStarted = false;
        bOnlineCorpseCapabilityReported = false;
        bOnlineCorpsePoolObserved = false;
        bOnlineCorpseSleepArmed = bOnlineGraphicsEnabled;
        bOnlineCorpseSleepApplied = false;
        OnlineCorpseLastCapacityRealTime = 0.0;
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
        EnsureOnlineFixedEffectsBaseline();
        ReportSessionContext(
            "online_client_read_only", "NM_Client", MapName);
        EnsureOnlineGraphicsListener(PrimaryController);
        ReportOnlineCorpseCapability(CurrentWorld);
        TrySleepOneOnlineCorpse(CurrentWorld);
        TryEnforceOnlineCorpseCapacity(CurrentWorld);
    }
    else if (CurrentWorld.NetMode == NM_ListenServer)
    {
        EnsureOnlineFixedEffectsBaseline();
        ReportSessionContext(
            "online_host_read_only", "NM_ListenServer", MapName);
        EnsureOnlineGraphicsListener(PrimaryController);
        ReportOnlineCorpseCapability(CurrentWorld);
        TrySleepOneOnlineCorpse(CurrentWorld);
        TryEnforceOnlineCorpseCapacity(CurrentWorld);
    }
    else if (CurrentWorld.NetMode == NM_Standalone)
    {
        ReportSessionContext("offline_full", "NM_Standalone", MapName);
    }
}

defaultproperties
{
}
