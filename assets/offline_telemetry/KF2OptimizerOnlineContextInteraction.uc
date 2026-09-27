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
var float OnlineGraphicsListenerNextCheckRealTime;
var float OnlineGraphicsListenerRetryDelay;
var string OnlineGraphicsListenerMapName;
var string LastOnlineGraphicsListenerStatus;
var bool bOnlineCorpseCapabilityReported;
var bool bOnlineCorpsePoolObserved;
var bool bOnlineCorpseSleepArmed;
var bool bOnlineCorpseSleepApplied;
var float OnlineCorpseLastCapacityRealTime;
var int OnlineCorpseOriginalMaximum;
var bool bOnlineCorpseOriginalMaximumCaptured;
var string OnlineCorpseOriginalMapName;

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

function ClearOnlineCorpseMaximumSnapshot()
{
    OnlineCorpseOriginalMaximum = 0;
    OnlineCorpseOriginalMapName = "";
    bOnlineCorpseOriginalMaximumCaptured = false;
}

function DiscardOnlineCorpseMaximumSnapshot(string Boundary)
{
    if (!bOnlineCorpseOriginalMaximumCaptured)
    {
        return;
    }
    `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=discarded boundary="$Boundary$
         " reason=world_changed original="$OnlineCorpseOriginalMaximum$
         " map="$OnlineCorpseOriginalMapName);
    ClearOnlineCorpseMaximumSnapshot();
}

function bool CaptureOnlineCorpseMaximum(
    WorldInfo CurrentWorld, KFGoreManager GoreManager)
{
    local string CurrentMapName;

    if (CurrentWorld == None || GoreManager == None)
    {
        return false;
    }
    CurrentMapName = CurrentWorld.GetMapName(true);
    if (bOnlineCorpseOriginalMaximumCaptured)
    {
        if (OnlineCorpseOriginalMapName == CurrentMapName)
        {
            return true;
        }
        DiscardOnlineCorpseMaximumSnapshot("world_change");
    }
    OnlineCorpseOriginalMaximum = GoreManager.MaxDeadBodies;
    OnlineCorpseOriginalMapName = CurrentMapName;
    bOnlineCorpseOriginalMaximumCaptured = true;
    `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=captured original="$
         OnlineCorpseOriginalMaximum$" map="$OnlineCorpseOriginalMapName$
         " local_only=true readback=verified");
    return true;
}

function bool RestoreOnlineCorpseMaximum(
    WorldInfo CurrentWorld, string Boundary)
{
    local KFGoreManager GoreManager;
    local string CurrentMapName;

    if (!bOnlineCorpseOriginalMaximumCaptured)
    {
        return true;
    }
    if (CurrentWorld == None)
    {
        `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restore_failed boundary="$
             Boundary$" reason=world_unavailable expected="$
             OnlineCorpseOriginalMaximum);
        return false;
    }
    CurrentMapName = CurrentWorld.GetMapName(true);
    if (CurrentMapName != OnlineCorpseOriginalMapName)
    {
        DiscardOnlineCorpseMaximumSnapshot(Boundary);
        return true;
    }
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None)
    {
        `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restore_failed boundary="$
             Boundary$" reason=no_gore_manager expected="$
             OnlineCorpseOriginalMaximum);
        return false;
    }
    GoreManager.MaxDeadBodies = OnlineCorpseOriginalMaximum;
    if (GoreManager.MaxDeadBodies != OnlineCorpseOriginalMaximum)
    {
        `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restore_failed boundary="$
             Boundary$" reason=readback_mismatch expected="$
             OnlineCorpseOriginalMaximum$" actual="$GoreManager.MaxDeadBodies);
        return false;
    }
    `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restored boundary="$Boundary$
         " maximum="$OnlineCorpseOriginalMaximum$
         " local_only=true readback=verified");
    ClearOnlineCorpseMaximumSnapshot();
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
        if (bOnlineCorpseOriginalMaximumCaptured &&
            CurrentWorld.RealTimeSeconds < LastObservedRealTime)
        {
            DiscardOnlineCorpseMaximumSnapshot("world_change");
        }
        if (!CaptureOnlineCorpseMaximum(CurrentWorld, GoreManager))
        {
            return false;
        }
        GoreManager.MaxDeadBodies = Quality;
        if (GoreManager.MaxDeadBodies != Quality)
        {
            RestoreOnlineCorpseMaximum(CurrentWorld, "enable_failure");
            return false;
        }
        CurrentState = GetOnlineGraphicsState();
        if (CurrentState == None || !EnsureOnlineFixedEffectsBaseline())
        {
            RestoreOnlineCorpseMaximum(CurrentWorld, "enable_failure");
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
        if (!RestoreOnlineCorpseMaximum(CurrentWorld, "disable"))
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
          (Resource ~= "ram") || (Resource ~= "recover")))
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

function ResetOnlineGraphicsListenerHealth(string MapName)
{
    OnlineGraphicsListenerNextCheckRealTime = 0.0;
    OnlineGraphicsListenerRetryDelay = 0.5;
    OnlineGraphicsListenerMapName = MapName;
    LastOnlineGraphicsListenerStatus = "";
}

function ReportOnlineGraphicsListenerStatus(string State, string Reason)
{
    local string Status;

    Status = State$"|"$Reason;
    if (Status == LastOnlineGraphicsListenerStatus)
    {
        return;
    }
    LastOnlineGraphicsListenerStatus = Status;
    `log("KF2OPT_ONLINE_GRAPHICS_BRIDGE state="$State$" reason="$Reason);
}

function bool FindHealthyOnlineGraphicsListener(
    WorldInfo CurrentWorld, out string FailureReason)
{
    local KF2OptimizerAdaptiveControlListener CurrentListener;

    FailureReason = "missing";
    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerAdaptiveControlListener', CurrentListener)
    {
        if (CurrentListener == None || CurrentListener.bDeleteMe)
        {
            continue;
        }
        if (CurrentListener.LinkState != STATE_Listening)
        {
            FailureReason = "not_listening";
            CurrentListener.Destroy();
            continue;
        }
        if (CurrentListener.OnlineCorpseController == None ||
            CurrentListener.OnlineCorpseController.bDeleteMe)
        {
            FailureReason = "corpse_controller_unavailable";
            CurrentListener.Destroy();
            continue;
        }
        return true;
    }
    return false;
}

function EnsureOnlineGraphicsListener(
    WorldInfo CurrentWorld, PlayerController PrimaryController)
{
    local KF2OptimizerAdaptiveControlListener NewListener;
    local string FailureReason;
    local string MapName;

    if (CurrentWorld == None || PrimaryController == None)
    {
        return;
    }
    MapName = CurrentWorld.GetMapName(true);
    if (OnlineGraphicsListenerMapName != MapName)
    {
        ResetOnlineGraphicsListenerHealth(MapName);
    }
    if (CurrentWorld.RealTimeSeconds <
        OnlineGraphicsListenerNextCheckRealTime)
    {
        return;
    }
    if (FindHealthyOnlineGraphicsListener(CurrentWorld, FailureReason))
    {
        OnlineGraphicsListenerRetryDelay = 0.5;
        OnlineGraphicsListenerNextCheckRealTime =
            CurrentWorld.RealTimeSeconds + 1.0;
        ReportOnlineGraphicsListenerStatus("ready", "verified");
        return;
    }
    NewListener = PrimaryController.Spawn(
            class'KF2OptimizerAdaptiveControlListener');
    if (NewListener != None && !NewListener.bDeleteMe &&
        NewListener.LinkState == STATE_Listening &&
        NewListener.OnlineCorpseController != None &&
        !NewListener.OnlineCorpseController.bDeleteMe)
    {
        OnlineGraphicsListenerRetryDelay = 0.5;
        OnlineGraphicsListenerNextCheckRealTime =
            CurrentWorld.RealTimeSeconds + 1.0;
        ReportOnlineGraphicsListenerStatus("ready", "recovered");
        return;
    }
    if (NewListener != None && !NewListener.bDeleteMe)
    {
        if (NewListener.LinkState != STATE_Listening)
        {
            FailureReason = "listen_failed";
        }
        else
        {
            FailureReason = "corpse_controller_spawn_failed";
        }
        NewListener.Destroy();
    }
    else if (FailureReason == "missing")
    {
        FailureReason = "spawn_failed";
    }
    // Do not retain an Actor reference from this viewport-owned Interaction.
    // Health is rediscovered at a bounded interval, and retry work backs off.
    ReportOnlineGraphicsListenerStatus("unavailable", FailureReason);
    OnlineGraphicsListenerNextCheckRealTime =
        CurrentWorld.RealTimeSeconds + OnlineGraphicsListenerRetryDelay;
    OnlineGraphicsListenerRetryDelay =
        FMin(8.0, OnlineGraphicsListenerRetryDelay * 2.0);
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

function bool RestoreOnlineSessionState(
    WorldInfo CurrentWorld, string Boundary)
{
    local bool bGraphicsRestored;

    bGraphicsRestored = true;
    if (OnlineGraphicsState != None &&
        OnlineGraphicsState.bOriginalCaptured)
    {
        if (class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
                OnlineGraphicsState))
        {
            `log("KF2OPT_ONLINE_GRAPHICS state=restored boundary="$Boundary);
        }
        else
        {
            bGraphicsRestored = false;
            `log("KF2OPT_ONLINE_GRAPHICS state=restore_failed boundary="$Boundary);
        }
    }
    if (!RestoreOnlineCorpseMaximum(CurrentWorld, Boundary) ||
        !bGraphicsRestored)
    {
        return false;
    }
    bOnlineGraphicsEnabled = false;
    bOnlineFixedEffectsApplied = false;
    OnlineGraphicsLastSequence = 0;
    ResetOnlineGraphicsListenerHealth("");
    bOnlineCorpseCapabilityReported = false;
    bOnlineCorpsePoolObserved = false;
    bOnlineCorpseSleepArmed = false;
    bOnlineCorpseSleepApplied = false;
    OnlineCorpseLastCapacityRealTime = 0.0;
    return true;
}

function RestoreOnlineGraphicsAtMainMenu(WorldInfo CurrentWorld)
{
    RestoreOnlineSessionState(CurrentWorld, "main_menu");
}

function NotifyGameSessionEnded()
{
    local WorldInfo CurrentWorld;

    if (!GetOnlineWorld(CurrentWorld))
    {
        if (bOnlineCorpseOriginalMaximumCaptured)
        {
            `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restore_failed"$
                 " boundary=session_end reason=world_unavailable expected="$
                 OnlineCorpseOriginalMaximum);
        }
        return;
    }
    RestoreOnlineSessionState(CurrentWorld, "session_end");
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
        DiscardOnlineCorpseMaximumSnapshot("world_change");
        LastReportedContext = "";
        ResetOnlineGraphicsListenerHealth("");
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
        RestoreOnlineGraphicsAtMainMenu(CurrentWorld);
        LastReportedContext = "";
        return;
    }
    if (CurrentWorld.NetMode == NM_Client)
    {
        EnsureOnlineFixedEffectsBaseline();
        ReportSessionContext(
            "online_client_read_only", "NM_Client", MapName);
        EnsureOnlineGraphicsListener(CurrentWorld, PrimaryController);
        ReportOnlineCorpseCapability(CurrentWorld);
        TrySleepOneOnlineCorpse(CurrentWorld);
        TryEnforceOnlineCorpseCapacity(CurrentWorld);
    }
    else if (CurrentWorld.NetMode == NM_ListenServer)
    {
        EnsureOnlineFixedEffectsBaseline();
        ReportSessionContext(
            "online_host_read_only", "NM_ListenServer", MapName);
        EnsureOnlineGraphicsListener(CurrentWorld, PrimaryController);
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
