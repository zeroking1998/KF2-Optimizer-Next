// Process-local session classifier installed by the protected viewport. Its
// context receipt remains read-only. A separate authenticated loopback actor
// applies reversible local GFXSettings and bounded local corpse-pool actions;
// it performs no replicated write. UI transitions only flush the local log.
class KF2OptimizerOnlineContextInteraction extends Interaction
    within GameViewportClient;

const OnlineCorpseScanBudget=64;

var string LastReportedContext;
var string LastReportedGameplayUiState;
var string LastReportedGameplayUiNetMode;
var int LastReportedGameplayUiGeneration;
var string LastOnlineContextMapName;
var int OnlineContextGeneration;
var float LastObservedRealTime;
var KF2OptimizerAdaptiveGraphicsState OnlineGraphicsState;
var int OnlineGraphicsLastSequence;
var bool bOnlineGraphicsEnabled;
var bool bLastOnlineGraphicsCapabilityRejected;
var bool bOnlineFixedEffectsApplied;
var float OnlineGraphicsListenerNextCheckRealTime;
var float OnlineGraphicsListenerRetryDelay;
var string OnlineGraphicsListenerMapName;
var string LastOnlineGraphicsListenerStatus;
var string OnlineGraphicsRetryMapName;
var int OnlineFixedEffectsBaselineAttempts;
var float OnlineFixedEffectsBaselineNextAttemptRealTime;
var float OnlineFixedEffectsBaselineRetryDelay;
var string OnlineFixedEffectsBaselineRetryStatus;
var int OnlineMainMenuRestoreAttempts;
var float OnlineMainMenuRestoreNextAttemptRealTime;
var float OnlineMainMenuRestoreRetryDelay;
var string OnlineMainMenuRestoreRetryStatus;
var bool bOnlineMainMenuRestoreComplete;
var bool bOnlineCorpseCapabilityReported;
var bool bOnlineCorpseUnavailableReported;
var bool bOnlineCorpsePoolObserved;
var bool bOnlineCorpseSleepArmed;
var bool bOnlineCorpseSleepApplied;
var int OnlineCorpseSleepScanCursor;
var int OnlineCorpseCapacityScanCursor;
var float OnlineCorpseLastSleepRealTime;
var float OnlineCorpseLastCapacityRealTime;
var int OnlineCorpseOriginalMaximum;
var bool bOnlineCorpseOriginalMaximumCaptured;
var string OnlineCorpseOriginalMapName;
var int OnlineCorpseEnablePreviousMaximum;
var bool bOnlineCorpseEnableRestorePending;
var bool bOnlineSessionEnding;
var string OnlineSessionEndingMapName;

function bool ValidOnlineGraphicsToken(string Candidate)
{
    return Len(class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken) ==
               32 &&
           Candidate ==
               class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken;
}

function bool DetailedRuntimeDiagnosticsEnabled()
{
    return class'KF2OptimizerTelemetryProbe'.default.
        bDetailedRuntimeDiagnostics;
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

function KF2OptimizerAdaptiveGraphicsState PeekOnlineGraphicsState()
{
    return OnlineGraphicsState;
}

function bool IsOnlineAdaptiveEnabled()
{
    return bOnlineGraphicsEnabled;
}

function bool WasOnlineGraphicsCapabilityRejected()
{
    return bLastOnlineGraphicsCapabilityRejected;
}

function bool IsOnlineSessionEnding()
{
    return bOnlineSessionEnding;
}

function ResetOnlineGraphicsRetryState(string MapName)
{
    OnlineGraphicsRetryMapName = MapName;
    OnlineFixedEffectsBaselineAttempts = 0;
    OnlineFixedEffectsBaselineNextAttemptRealTime = 0.0;
    OnlineFixedEffectsBaselineRetryDelay = 0.5;
    OnlineFixedEffectsBaselineRetryStatus = "";
    OnlineMainMenuRestoreAttempts = 0;
    OnlineMainMenuRestoreNextAttemptRealTime = 0.0;
    OnlineMainMenuRestoreRetryDelay = 0.5;
    OnlineMainMenuRestoreRetryStatus = "";
    bOnlineMainMenuRestoreComplete = false;
}

function ReportOnlineGraphicsRetry(
    string Operation, string State, string Reason,
    int Attempt, int NextRetryMs)
{
    local string Status;

    Status = State$"|"$Reason;
    if (Operation == "baseline")
    {
        if (Status == OnlineFixedEffectsBaselineRetryStatus)
        {
            return;
        }
        OnlineFixedEffectsBaselineRetryStatus = Status;
    }
    else
    {
        if (Status == OnlineMainMenuRestoreRetryStatus)
        {
            return;
        }
        OnlineMainMenuRestoreRetryStatus = Status;
    }
    `log("KF2OPT_GRAPHICS_RETRY mode=online operation="$Operation$
         " state="$State$" reason="$Reason$" attempt="$Attempt$
         " next_retry_ms="$NextRetryMs);
}

function bool EnsureOnlineFixedEffectsBaseline(WorldInfo CurrentWorld)
{
    local int CompletedAttempts;
    local KF2OptimizerAdaptiveGraphicsState CurrentState;

    if (bOnlineFixedEffectsApplied)
    {
        return true;
    }
    if (CurrentWorld == None || CurrentWorld.RealTimeSeconds <
        OnlineFixedEffectsBaselineNextAttemptRealTime)
    {
        return false;
    }
    ++OnlineFixedEffectsBaselineAttempts;
    CurrentState = GetOnlineGraphicsState();
    if (CurrentState == None ||
        !class'KF2OptimizerAdaptiveGraphics'.static.
            ApplyFixedSessionEffects(CurrentState))
    {
        if (OnlineFixedEffectsBaselineRetryDelay <= 0.0)
        {
            OnlineFixedEffectsBaselineRetryDelay = 0.5;
        }
        OnlineFixedEffectsBaselineNextAttemptRealTime =
            CurrentWorld.RealTimeSeconds +
            OnlineFixedEffectsBaselineRetryDelay;
        ReportOnlineGraphicsRetry(
            "baseline", "deferred", "readback_failed",
            OnlineFixedEffectsBaselineAttempts,
            int(OnlineFixedEffectsBaselineRetryDelay * 1000.0));
        OnlineFixedEffectsBaselineRetryDelay =
            FMin(8.0, OnlineFixedEffectsBaselineRetryDelay * 2.0);
        return false;
    }
    bOnlineFixedEffectsApplied = true;
    CompletedAttempts = OnlineFixedEffectsBaselineAttempts;
    OnlineFixedEffectsBaselineAttempts = 0;
    OnlineFixedEffectsBaselineNextAttemptRealTime = 0.0;
    OnlineFixedEffectsBaselineRetryDelay = 0.5;
    OnlineFixedEffectsBaselineRetryStatus = "";
    `log("KF2OPT_FIXED_EFFECT_BASELINE state=applied mode=online"$
         " quality="$class'KF2OptimizerAdaptiveGraphics'.static.
            GetFixedSessionEffectsQuality()$" attempts="$CompletedAttempts$
         " readback=verified");
    return true;
}

function ClearOnlineCorpseMaximumSnapshot()
{
    OnlineCorpseOriginalMaximum = 0;
    OnlineCorpseOriginalMapName = "";
    bOnlineCorpseOriginalMaximumCaptured = false;
    OnlineCorpseEnablePreviousMaximum = 0;
    bOnlineCorpseEnableRestorePending = false;
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

function bool RestoreOnlineEnableMaximum(KFGoreManager GoreManager)
{
    if (!bOnlineCorpseEnableRestorePending)
    {
        return true;
    }
    if (GoreManager == None)
    {
        return false;
    }
    GoreManager.MaxDeadBodies = OnlineCorpseEnablePreviousMaximum;
    if (GoreManager.MaxDeadBodies != OnlineCorpseEnablePreviousMaximum)
    {
        `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restore_failed"$
             " boundary=enable_failure reason=readback_mismatch expected="$
             OnlineCorpseEnablePreviousMaximum$
             " actual="$GoreManager.MaxDeadBodies$" ownership=retained");
        return false;
    }
    bOnlineCorpseEnableRestorePending = false;
    // Keep the session-original snapshot for eventual disable/cleanup.
    `log("KF2OPT_ONLINE_CORPSE_MAXIMUM state=restored"$
         " boundary=enable_failure maximum="$OnlineCorpseEnablePreviousMaximum$
         " local_only=true readback=verified snapshot=retained");
    return true;
}

function bool ApplyOnlineGraphicsControl(
    string Token, int Sequence, string Resource, int Quality)
{
    local bool bCorpseMaximumRestored;
    local bool bFixedEffectsApplied;
    local bool bGraphicsRestored;
    local WorldInfo CurrentWorld;
    local KF2OptimizerAdaptiveGraphicsState CurrentState;
    local KFGoreManager GoreManager;

    bLastOnlineGraphicsCapabilityRejected = false;
    if (!GetOnlineWorld(CurrentWorld) ||
        !ValidOnlineGraphicsToken(Token) || Sequence <= 0 ||
        Sequence <= OnlineGraphicsLastSequence)
    {
        return false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.
            IsAdaptiveControlResource(Resource))
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
        if (!RestoreOnlineEnableMaximum(GoreManager))
        {
            return false;
        }
        CurrentState = GetOnlineGraphicsState();
        if (CurrentState == None ||
            !EnsureOnlineFixedEffectsBaseline(CurrentWorld))
        {
            return false;
        }
        // All prerequisites are ready. A rejected re-enable must restore the
        // previous live limit, not undo an earlier accepted enable.
        OnlineCorpseEnablePreviousMaximum = GoreManager.MaxDeadBodies;
        GoreManager.MaxDeadBodies = Quality;
        if (GoreManager.MaxDeadBodies != Quality)
        {
            bOnlineCorpseEnableRestorePending = true;
            RestoreOnlineEnableMaximum(GoreManager);
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
    if (Resource ~= "disable")
    {
        // Commit the mode transition first. The World-owned corpse controller
        // observes this flag and performs its bounded restore independently
        // from graphics readback and the fixed-effects baseline.
        bOnlineGraphicsEnabled = false;
        bOnlineCorpseSleepArmed = false;
        bOnlineCorpseSleepApplied = false;
        OnlineGraphicsLastSequence = Sequence;
        CurrentState = GetOnlineGraphicsState();
        bGraphicsRestored = CurrentState != None &&
            class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
                CurrentState);
        bOnlineFixedEffectsApplied = false;
        bFixedEffectsApplied = EnsureOnlineFixedEffectsBaseline(CurrentWorld);
        bCorpseMaximumRestored =
            RestoreOnlineCorpseMaximum(CurrentWorld, "disable");
        if (!bGraphicsRestored || !bFixedEffectsApplied ||
            !bCorpseMaximumRestored)
        {
            `log("KF2OPT_ONLINE_GRAPHICS state=disabled"$
                 " readback=deferred local_only=true");
            // Mode-off is committed independently. Keep confirmation pending
            // so the app's existing bounded retry restores every obligation.
            return false;
        }
        `log("KF2OPT_ONLINE_GRAPHICS state=disabled fixed_effect_quality="$
             class'KF2OptimizerAdaptiveGraphics'.static.
                GetFixedSessionEffectsQuality()$
             " readback=verified");
        return true;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.
            IsAdaptiveQualityResource(Resource))
    {
        return false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.
            IsOnlineAdaptiveQualityResource(Resource))
    {
        bLastOnlineGraphicsCapabilityRejected = true;
        `log("KF2OPT_ONLINE_GRAPHICS state=unsupported seq="$Sequence$
             " resource="$Resource$" reason=capability_unavailable"$
             " local_only=true");
        return false;
    }
    if (!bOnlineGraphicsEnabled)
    {
        return false;
    }
    CurrentState = GetOnlineGraphicsState();
    if (CurrentState == None) return false;
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
    local int PoolLength;
    local int ScanCount;
    local int Scanned;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;

    if (!bOnlineGraphicsEnabled || !bOnlineCorpseSleepArmed ||
        bOnlineCorpseSleepApplied || CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds - OnlineCorpseLastSleepRealTime < 0.15)
    {
        return false;
    }
    // Admission includes misses and failed readbacks, not just success.
    OnlineCorpseLastSleepRealTime = CurrentWorld.RealTimeSeconds;
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return false;
    }
    PoolLength = GoreManager.CorpsePool.Length;
    if (PoolLength == 0) return false;
    OnlineCorpseSleepScanCursor =
        Clamp(OnlineCorpseSleepScanCursor, 0, PoolLength - 1);
    ScanCount = Min(OnlineCorpseScanBudget, PoolLength);
    for (Scanned = 0; Scanned < ScanCount; ++Scanned)
    {
        Index = OnlineCorpseSleepScanCursor;
        OnlineCorpseSleepScanCursor = (Index + 1) % PoolLength;
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
        if (DetailedRuntimeDiagnosticsEnabled())
        {
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=sleep corpse_id="$
                 string(Candidate.Name)$" pool="$
                 GoreManager.CorpsePool.Length$
                 " awake=false local_only=true readback=verified");
        }
        else
        {
            // Keep the single authenticated capability receipt without
            // formatting Actor or pool details in normal sessions.
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=sleep"$
                 " awake=false local_only=true readback=verified");
        }
        return true;
    }
    return false;
}

function bool TryEnforceOnlineCorpseCapacity(WorldInfo CurrentWorld)
{
    local int Index;
    local int PoolBefore;
    local int PoolLength;
    local int ScanCount;
    local int Scanned;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;
    local string CandidateName;

    // Do not delete corpses using an unconfirmed, partially written ceiling.
    if (!bOnlineGraphicsEnabled || bOnlineCorpseEnableRestorePending ||
        CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds - OnlineCorpseLastCapacityRealTime < 0.45)
    {
        return false;
    }
    OnlineCorpseLastCapacityRealTime = CurrentWorld.RealTimeSeconds;
    GoreManager = KFGoreManager(CurrentWorld.MyGoreEffectManager);
    if (GoreManager == None || GoreManager.MaxDeadBodies < 4)
    {
        return false;
    }
    PoolLength = GoreManager.CorpsePool.Length;
    if (PoolLength == 0) return false;
    if (PoolLength <= GoreManager.MaxDeadBodies) return false;
    PoolBefore = PoolLength;
    OnlineCorpseCapacityScanCursor =
        Clamp(OnlineCorpseCapacityScanCursor, 0, PoolLength - 1);
    ScanCount = Min(OnlineCorpseScanBudget, PoolLength);
    for (Scanned = 0; Scanned < ScanCount; ++Scanned)
    {
        Index = OnlineCorpseCapacityScanCursor;
        OnlineCorpseCapacityScanCursor = (Index + 1) % PoolLength;
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
            if (!CurrentListener.EnsureOnlineCorpseController())
            {
                FailureReason = "corpse_controller_unavailable";
                CurrentListener.Destroy();
                continue;
            }
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
        if (!bOnlineCorpseUnavailableReported)
        {
            bOnlineCorpseUnavailableReported = true;
            `log("KF2OPT_ONLINE_CORPSE state=unavailable reason=no_gore_manager"$
                 " local_only=true readback=verified");
        }
        return;
    }
    if (bOnlineCorpseUnavailableReported)
    {
        bOnlineCorpseUnavailableReported = false;
        bOnlineCorpseCapabilityReported = false;
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
            if (Boundary != "main_menu")
            {
                `log("KF2OPT_ONLINE_GRAPHICS state=restore_failed boundary="$
                     Boundary);
            }
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
    bOnlineCorpseUnavailableReported = false;
    bOnlineCorpsePoolObserved = false;
    bOnlineCorpseSleepArmed = false;
    bOnlineCorpseSleepApplied = false;
    OnlineCorpseSleepScanCursor = 0;
    OnlineCorpseCapacityScanCursor = 0;
    OnlineCorpseLastSleepRealTime = 0.0;
    OnlineCorpseLastCapacityRealTime = 0.0;
    return true;
}

function RestoreOnlineGraphicsAtMainMenu(WorldInfo CurrentWorld)
{
    local int CompletedAttempts;

    if (CurrentWorld == None || bOnlineMainMenuRestoreComplete ||
        CurrentWorld.RealTimeSeconds <
            OnlineMainMenuRestoreNextAttemptRealTime)
    {
        return;
    }
    ++OnlineMainMenuRestoreAttempts;
    if (RestoreOnlineSessionState(CurrentWorld, "main_menu"))
    {
        CompletedAttempts = OnlineMainMenuRestoreAttempts;
        OnlineMainMenuRestoreAttempts = 0;
        OnlineMainMenuRestoreNextAttemptRealTime = 0.0;
        OnlineMainMenuRestoreRetryDelay = 0.5;
        OnlineMainMenuRestoreRetryStatus = "";
        bOnlineMainMenuRestoreComplete = true;
        if (CompletedAttempts > 1)
        {
            `log("KF2OPT_GRAPHICS_RETRY mode=online operation=restore"$
                 " state=recovered attempts="$CompletedAttempts$
                 " readback=verified");
        }
        return;
    }
    if (OnlineMainMenuRestoreRetryDelay <= 0.0)
    {
        OnlineMainMenuRestoreRetryDelay = 0.5;
    }
    OnlineMainMenuRestoreNextAttemptRealTime =
        CurrentWorld.RealTimeSeconds + OnlineMainMenuRestoreRetryDelay;
    ReportOnlineGraphicsRetry(
        "restore", "deferred", "restore_failed",
        OnlineMainMenuRestoreAttempts,
        int(OnlineMainMenuRestoreRetryDelay * 1000.0));
    OnlineMainMenuRestoreRetryDelay =
        FMin(8.0, OnlineMainMenuRestoreRetryDelay * 2.0);
}

function NotifyGameSessionEnded()
{
    local KF2OptimizerOnlineCorpseController CurrentController;
    local WorldInfo CurrentWorld;

    bOnlineSessionEnding = true;
    OnlineSessionEndingMapName = OnlineGraphicsListenerMapName;
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
    OnlineSessionEndingMapName = CurrentWorld.GetMapName(true);
    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerOnlineCorpseController', CurrentController)
    {
        if (CurrentController != None && !CurrentController.bDeleteMe)
        {
            CurrentController.PrepareForWorldTeardown();
        }
    }
    RestoreOnlineSessionState(CurrentWorld, "session_end");
}

function ReportSessionContext(
    string State, string NetModeName, string MapName)
{
    local string Context;

    Context = State$"|"$NetModeName$"|"$MapName$"|"$
        OnlineContextGeneration;
    if (Context == LastReportedContext)
    {
        return;
    }
    LastReportedContext = Context;
    `log("KF2OPT_SESSION_CONTEXT schema=2 state="$State$
         " net_mode="$NetModeName$" map="$MapName$
         " generation="$OnlineContextGeneration);
}

function ReportOnlineGameplayUiState(
    string State, string NetModeName, string MapName,
    PlayerController PrimaryController)
{
    if (State ~= LastReportedGameplayUiState &&
        NetModeName ~= LastReportedGameplayUiNetMode &&
        OnlineContextGeneration == LastReportedGameplayUiGeneration)
    {
        return;
    }
    LastReportedGameplayUiState = State;
    LastReportedGameplayUiNetMode = NetModeName;
    LastReportedGameplayUiGeneration = OnlineContextGeneration;
    `log("KF2OPT_GAMEPLAY_CONTEXT schema=2 state="$State$
         " net_mode="$NetModeName$" map="$MapName$
         " generation="$OnlineContextGeneration);
    // A quiet online match must not buffer menu transitions until later logs.
    if (PrimaryController != None)
    {
        PrimaryController.ConsoleCommand("FLUSHLOG", false);
    }
}

function UpdateOnlineGameplayUiState(
    PlayerController PrimaryController, string NetModeName, string MapName)
{
    local KFPlayerController KFPC;

    KFPC = KFPlayerController(PrimaryController);
    if (KFPC == None || KFPC.MyGFxManager == None)
    {
        ReportOnlineGameplayUiState(
            "unavailable", NetModeName, MapName, PrimaryController);
    }
    else if (!KFPC.MyGFxManager.bMenusOpen)
    {
        ReportOnlineGameplayUiState(
            "gameplay", NetModeName, MapName, PrimaryController);
    }
    else if (KFPC.MyGFxManager.CurrentMenu == KFPC.MyGFxManager.TraderMenu)
    {
        ReportOnlineGameplayUiState(
            "trader", NetModeName, MapName, PrimaryController);
    }
    else
    {
        ReportOnlineGameplayUiState(
            "menu", NetModeName, MapName, PrimaryController);
    }
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

    MapName = CurrentWorld.GetMapName(true);
    // RealTimeSeconds restarts with each World. The map check also covers a
    // travel boundary observed before the new World's clock advances. Both
    // receipts carry this generation, so a delayed old UI state cannot make a
    // new online session look like active gameplay.
    if (OnlineContextGeneration <= 0 ||
        CurrentWorld.RealTimeSeconds < LastObservedRealTime ||
        (Len(LastOnlineContextMapName) > 0 &&
         !(MapName ~= LastOnlineContextMapName)))
    {
        ++OnlineContextGeneration;
        DiscardOnlineCorpseMaximumSnapshot("world_change");
        LastReportedContext = "";
        LastReportedGameplayUiState = "";
        LastReportedGameplayUiNetMode = "";
        LastReportedGameplayUiGeneration = 0;
        ResetOnlineGraphicsListenerHealth("");
        bOnlineCorpseCapabilityReported = false;
        bOnlineCorpseUnavailableReported = false;
        bOnlineCorpsePoolObserved = false;
        bOnlineCorpseSleepArmed = bOnlineGraphicsEnabled;
        bOnlineCorpseSleepApplied = false;
        OnlineCorpseSleepScanCursor = 0;
        OnlineCorpseCapacityScanCursor = 0;
        OnlineCorpseLastSleepRealTime = 0.0;
        OnlineCorpseLastCapacityRealTime = 0.0;
        ResetOnlineGraphicsRetryState("");
        bOnlineSessionEnding = false;
        OnlineSessionEndingMapName = "";
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    LastOnlineContextMapName = MapName;
    if (bOnlineSessionEnding && Len(OnlineSessionEndingMapName) > 0 &&
        !(MapName ~= OnlineSessionEndingMapName))
    {
        bOnlineSessionEnding = false;
        OnlineSessionEndingMapName = "";
    }
    if (bOnlineSessionEnding)
    {
        if (CurrentWorld.NetMode == NM_Client)
        {
            ReportOnlineGameplayUiState(
                "unavailable", "NM_Client", MapName, PrimaryController);
        }
        else if (CurrentWorld.NetMode == NM_ListenServer)
        {
            ReportOnlineGameplayUiState(
                "unavailable", "NM_ListenServer", MapName, PrimaryController);
        }
        return;
    }
    if (OnlineGraphicsRetryMapName != MapName)
    {
        ResetOnlineGraphicsRetryState(MapName);
    }
    if (MapName ~= "KFMainMenu")
    {
        RestoreOnlineGraphicsAtMainMenu(CurrentWorld);
        LastReportedContext = "";
        LastReportedGameplayUiState = "";
        LastReportedGameplayUiNetMode = "";
        LastReportedGameplayUiGeneration = 0;
        return;
    }
    if (CurrentWorld.NetMode == NM_Client)
    {
        bOnlineMainMenuRestoreComplete = false;
        EnsureOnlineFixedEffectsBaseline(CurrentWorld);
        ReportSessionContext(
            "online_client_read_only", "NM_Client", MapName);
        UpdateOnlineGameplayUiState(
            PrimaryController, "NM_Client", MapName);
        EnsureOnlineGraphicsListener(CurrentWorld, PrimaryController);
        ReportOnlineCorpseCapability(CurrentWorld);
        TrySleepOneOnlineCorpse(CurrentWorld);
        TryEnforceOnlineCorpseCapacity(CurrentWorld);
    }
    else if (CurrentWorld.NetMode == NM_ListenServer)
    {
        bOnlineMainMenuRestoreComplete = false;
        EnsureOnlineFixedEffectsBaseline(CurrentWorld);
        ReportSessionContext(
            "online_host_read_only", "NM_ListenServer", MapName);
        UpdateOnlineGameplayUiState(
            PrimaryController, "NM_ListenServer", MapName);
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
