// Persistent viewport interaction inserted into KF2's native viewport by the
// Published mutator. It creates the protected provider only in a standalone
// gameplay world and draws optional corpse IDs.
class KF2OptimizerTelemetryInteraction extends Interaction
    within GameViewportClient;

const AchievementPrewarmRequestTimeoutSeconds=10.0;

var string OptimizerContextState;
var string OptimizerProbeState;
var string OptimizerGameplayUiState;
var bool bGameSessionEnding;
var KF2OptimizerAdaptiveGraphicsState ProcessAdaptiveGraphicsState;
var bool bProcessAdaptiveRuntimeStateInitialized;
var bool bProcessAdaptiveRuntimeEnabled;
var bool bAchievementPrewarmRequested;
var bool bAchievementPrewarmComplete;
var bool bAchievementPrewarmDelegateRegistered;
var byte AchievementPrewarmPlayerControllerId;
var int AchievementPrewarmAttempts;
var float AchievementPrewarmNextAttemptRealTime;
var float AchievementPrewarmRequestStartedRealTime;
var int FixedEffectsBaselineAttempts;
var float FixedEffectsBaselineNextAttemptRealTime;
var float FixedEffectsBaselineRetryDelay;
var string FixedEffectsBaselineRetryStatus;
var bool bProcessGraphicsRestorePending;
var int ProcessGraphicsRestoreAttempts;
var float ProcessGraphicsRestoreNextAttemptRealTime;
var float ProcessGraphicsRestoreRetryDelay;
var string ProcessGraphicsRestoreRetryStatus;

function SetProcessAdaptiveRuntimeEnabled(bool bEnabled)
{
    bProcessAdaptiveRuntimeStateInitialized = true;
    bProcessAdaptiveRuntimeEnabled = bEnabled;
}

function ReportOptimizerContextState(string State)
{
    if (OptimizerContextState ~= State)
    {
        return;
    }
    OptimizerContextState = State;
    `log("KF2OPT_INTERACTION schema=1 state="$State);
}

function ReportOptimizerProbeState(string State)
{
    if (OptimizerProbeState ~= State)
    {
        return;
    }
    OptimizerProbeState = State;
    `log("KF2OPT_INTERACTION schema=1 probe="$State);
}

function ReportGameplayUiState(string State)
{
    if (OptimizerGameplayUiState ~= State)
    {
        return;
    }
    OptimizerGameplayUiState = State;
    `log("KF2OPT_GAMEPLAY_CONTEXT schema=1 state="$State);
}

function ResetFixedEffectsBaselineRetry()
{
    FixedEffectsBaselineAttempts = 0;
    FixedEffectsBaselineNextAttemptRealTime = 0.0;
    FixedEffectsBaselineRetryDelay = 0.5;
    FixedEffectsBaselineRetryStatus = "";
}

function ReportFixedEffectsBaselineRetry(
    string State, string Reason, int NextRetryMs)
{
    local string Status;

    Status = State$"|"$Reason;
    if (Status == FixedEffectsBaselineRetryStatus)
    {
        return;
    }
    FixedEffectsBaselineRetryStatus = Status;
    `log("KF2OPT_FIXED_EFFECT_RETRY mode=offline state="$State$
         " reason="$Reason$" attempt="$FixedEffectsBaselineAttempts$
         " next_retry_ms="$NextRetryMs);
}

function bool EnsureFixedEffectsBaselineWithBackoff(
    KF2OptimizerTelemetryProbe CurrentProbe, WorldInfo CurrentWorld)
{
    local int CompletedAttempts;

    if (CurrentProbe == None || CurrentWorld == None)
    {
        return false;
    }
    if (CurrentProbe.bFixedSessionEffectsApplied)
    {
        return true;
    }
    if (CurrentWorld.RealTimeSeconds <
        FixedEffectsBaselineNextAttemptRealTime)
    {
        return false;
    }
    ++FixedEffectsBaselineAttempts;
    if (CurrentProbe.EnsureFixedSessionEffects())
    {
        CompletedAttempts = FixedEffectsBaselineAttempts;
        ResetFixedEffectsBaselineRetry();
        if (CompletedAttempts > 1)
        {
            `log("KF2OPT_FIXED_EFFECT_RETRY mode=offline state=recovered"$
                 " attempts="$CompletedAttempts$" readback=verified");
        }
        return true;
    }
    if (FixedEffectsBaselineRetryDelay <= 0.0)
    {
        FixedEffectsBaselineRetryDelay = 0.5;
    }
    FixedEffectsBaselineNextAttemptRealTime =
        CurrentWorld.RealTimeSeconds + FixedEffectsBaselineRetryDelay;
    ReportFixedEffectsBaselineRetry(
        "deferred", "readback_failed",
        int(FixedEffectsBaselineRetryDelay * 1000.0));
    FixedEffectsBaselineRetryDelay =
        FMin(8.0, FixedEffectsBaselineRetryDelay * 2.0);
    return false;
}

function ResetProcessGraphicsRestoreRetry()
{
    ProcessGraphicsRestoreAttempts = 0;
    ProcessGraphicsRestoreNextAttemptRealTime = 0.0;
    ProcessGraphicsRestoreRetryDelay = 0.5;
    ProcessGraphicsRestoreRetryStatus = "";
}

function ReportProcessGraphicsRestoreRetry(
    string State, string Reason, int NextRetryMs)
{
    local string Status;

    Status = State$"|"$Reason;
    if (Status == ProcessGraphicsRestoreRetryStatus)
    {
        return;
    }
    ProcessGraphicsRestoreRetryStatus = Status;
    `log("KF2OPT_PROCESS_GRAPHICS_RETRY state="$State$
         " reason="$Reason$" attempt="$ProcessGraphicsRestoreAttempts$
         " next_retry_ms="$NextRetryMs$" ownership=retained");
}

function bool RestorePendingProcessGraphicsWithBackoff(WorldInfo CurrentWorld)
{
    local int CompletedAttempts;

    if (!bProcessGraphicsRestorePending)
    {
        return true;
    }
    if (CurrentWorld == None || CurrentWorld.RealTimeSeconds <
        ProcessGraphicsRestoreNextAttemptRealTime)
    {
        return false;
    }
    ++ProcessGraphicsRestoreAttempts;
    if (class'KF2OptimizerAdaptiveGraphics'.static.
            RestoreOriginal(ProcessAdaptiveGraphicsState))
    {
        CompletedAttempts = ProcessGraphicsRestoreAttempts;
        bProcessGraphicsRestorePending = false;
        ResetProcessGraphicsRestoreRetry();
        `log("KF2OPT_PROCESS_GRAPHICS_RETRY state=recovered attempts="$
             CompletedAttempts$" readback=verified ownership=released");
        return true;
    }
    if (ProcessGraphicsRestoreRetryDelay <= 0.0)
    {
        ProcessGraphicsRestoreRetryDelay = 0.5;
    }
    ProcessGraphicsRestoreNextAttemptRealTime =
        CurrentWorld.RealTimeSeconds + ProcessGraphicsRestoreRetryDelay;
    ReportProcessGraphicsRestoreRetry(
        "deferred", "readback_mismatch",
        int(ProcessGraphicsRestoreRetryDelay * 1000.0));
    ProcessGraphicsRestoreRetryDelay =
        FMin(8.0, ProcessGraphicsRestoreRetryDelay * 2.0);
    return false;
}

function UpdateGameplayUiState(PlayerController PrimaryController)
{
    local KFPlayerController KFPC;

    KFPC = KFPlayerController(PrimaryController);
    if (KFPC == None || KFPC.MyGFxManager == None)
    {
        ReportGameplayUiState("unavailable");
        return;
    }
    if (!KFPC.MyGFxManager.bMenusOpen)
    {
        ReportGameplayUiState("gameplay");
    }
    else if (KFPC.MyGFxManager.CurrentMenu == KFPC.MyGFxManager.TraderMenu)
    {
        ReportGameplayUiState("trader");
    }
    else
    {
        ReportGameplayUiState("menu");
    }
}

function KF2OptimizerAdaptiveGraphicsState GetProcessAdaptiveGraphicsState()
{
    if (ProcessAdaptiveGraphicsState == None)
    {
        ProcessAdaptiveGraphicsState = new(self)
            class'KF2OptimizerAdaptiveGraphicsState';
    }
    return ProcessAdaptiveGraphicsState;
}

function ClearAchievementPrewarmDelegate()
{
    local OnlineSubsystem OnlineSub;

    if (!bAchievementPrewarmDelegateRegistered)
    {
        return;
    }
    OnlineSub = class'GameEngine'.static.GetOnlineSubsystem();
    if (OnlineSub != None && OnlineSub.PlayerInterface != None)
    {
        OnlineSub.PlayerInterface.ClearReadAchievementsCompleteDelegate(
            AchievementPrewarmPlayerControllerId,
            OnAchievementPrewarmComplete);
    }
    bAchievementPrewarmDelegateRegistered = false;
}

function OnAchievementPrewarmComplete(int TitleId)
{
    bAchievementPrewarmRequested = false;
    bAchievementPrewarmComplete = true;
    AchievementPrewarmRequestStartedRealTime = 0.0;
    ClearAchievementPrewarmDelegate();
    `log("KF2OPT_ACHIEVEMENT_PREWARM schema=1 state=complete title="$
         TitleId$" attempts="$AchievementPrewarmAttempts);
}

function TryPrewarmAchievements(PlayerController PrimaryController)
{
    local LocalPlayer PrimaryPlayer;
    local OnlineSubsystem OnlineSub;
    local byte PlayerControllerId;
    local int NextRetryMs;

    if (bAchievementPrewarmRequested)
    {
        if (PrimaryController != None && PrimaryController.WorldInfo != None &&
            (PrimaryController.WorldInfo.RealTimeSeconds <
                 AchievementPrewarmRequestStartedRealTime ||
             PrimaryController.WorldInfo.RealTimeSeconds -
                 AchievementPrewarmRequestStartedRealTime >=
                 AchievementPrewarmRequestTimeoutSeconds))
        {
            ClearAchievementPrewarmDelegate();
            bAchievementPrewarmRequested = false;
            AchievementPrewarmRequestStartedRealTime = 0.0;
            if (AchievementPrewarmAttempts < 3)
            {
                AchievementPrewarmNextAttemptRealTime =
                    PrimaryController.WorldInfo.RealTimeSeconds + 1.0;
                NextRetryMs = 1000;
            }
            `log("KF2OPT_ACHIEVEMENT_PREWARM schema=1 state=timeout attempt="$
                 AchievementPrewarmAttempts$" next_retry_ms="$NextRetryMs);
        }
        return;
    }
    if (bAchievementPrewarmComplete ||
        AchievementPrewarmAttempts >= 3 || PrimaryController == None ||
        PrimaryController.WorldInfo == None || GamePlayers.Length == 0 ||
        PrimaryController.WorldInfo.RealTimeSeconds <
            AchievementPrewarmNextAttemptRealTime)
    {
        return;
    }
    PrimaryPlayer = GamePlayers[0];
    if (PrimaryPlayer == None)
    {
        return;
    }
    PlayerControllerId = byte(PrimaryPlayer.ControllerId);
    OnlineSub = class'GameEngine'.static.GetOnlineSubsystem();
    if (OnlineSub == None || OnlineSub.PlayerInterface == None ||
        OnlineSub.PlayerInterface.GetLoginStatus(PlayerControllerId) <=
            LS_NotLoggedIn ||
        OnlineSub.PlayerInterface.IsGuestLogin(PlayerControllerId))
    {
        AchievementPrewarmNextAttemptRealTime =
            PrimaryController.WorldInfo.RealTimeSeconds + 1.0;
        return;
    }

    AchievementPrewarmPlayerControllerId = PlayerControllerId;
    OnlineSub.PlayerInterface.AddReadAchievementsCompleteDelegate(
        PlayerControllerId, OnAchievementPrewarmComplete);
    bAchievementPrewarmDelegateRegistered = true;
    bAchievementPrewarmRequested = true;
    AchievementPrewarmRequestStartedRealTime =
        PrimaryController.WorldInfo.RealTimeSeconds;
    ++AchievementPrewarmAttempts;
    `log("KF2OPT_ACHIEVEMENT_PREWARM schema=1 state=requested"$
         " text=true images=true attempt="$AchievementPrewarmAttempts);
    if (!OnlineSub.PlayerInterface.ReadAchievements(
            PlayerControllerId, 0, true, true))
    {
        bAchievementPrewarmRequested = false;
        AchievementPrewarmRequestStartedRealTime = 0.0;
        ClearAchievementPrewarmDelegate();
        AchievementPrewarmNextAttemptRealTime =
            PrimaryController.WorldInfo.RealTimeSeconds + 1.0;
        `log("KF2OPT_ACHIEVEMENT_PREWARM schema=1 state=deferred attempt="$
             AchievementPrewarmAttempts);
        return;
    }
}

function PrepareForGameplayWorld()
{
    // The viewport interaction outlives gameplay worlds. A newly initialized
    // standalone gameplay mutator is the authoritative rearm boundary; local
    // players may also be added while KF2 is only returning to the main menu.
    ClearAchievementPrewarmDelegate();
    bGameSessionEnding = false;
    bAchievementPrewarmRequested = false;
    bAchievementPrewarmComplete = false;
    AchievementPrewarmAttempts = 0;
    AchievementPrewarmNextAttemptRealTime = 0.0;
    AchievementPrewarmRequestStartedRealTime = 0.0;
    ResetFixedEffectsBaselineRetry();
    if (bProcessGraphicsRestorePending)
    {
        // RealTimeSeconds restarts with the world. Keep ownership and retry
        // immediately in the new valid gameplay context.
        ProcessGraphicsRestoreNextAttemptRealTime = 0.0;
        ProcessGraphicsRestoreRetryStatus = "";
    }
    else
    {
        ResetProcessGraphicsRestoreRetry();
    }
    OptimizerContextState = "";
    OptimizerProbeState = "";
    OptimizerGameplayUiState = "";
    `log("KF2OPT_INTERACTION schema=1 state=rearmed");
}

function bool GetStandaloneGameplayContext(
    out PlayerController PrimaryController,
    out WorldInfo CurrentWorld)
{
    local LocalPlayer PrimaryPlayer;

    PrimaryController = None;
    CurrentWorld = None;

    if (GamePlayers.Length == 0)
    {
        ReportOptimizerContextState("waiting_players");
        return false;
    }
    PrimaryPlayer = GamePlayers[0];
    if (PrimaryPlayer == None)
    {
        ReportOptimizerContextState("waiting_local_player");
        return false;
    }
    PrimaryController = PrimaryPlayer.Actor;
    if (PrimaryController == None)
    {
        ReportOptimizerContextState("waiting_controller");
        return false;
    }
    CurrentWorld = PrimaryController.WorldInfo;
    if (CurrentWorld == None)
    {
        ReportOptimizerContextState("waiting_world");
        return false;
    }
    if (CurrentWorld.NetMode != NM_Standalone)
    {
        ReportOptimizerContextState("blocked_net_mode");
        return false;
    }
    if (CurrentWorld.GetMapName(true) ~= "KFMainMenu")
    {
        ReportOptimizerContextState("main_menu");
        return false;
    }
    ReportOptimizerContextState("gameplay_ready");
    return true;
}

event Tick(float DeltaTime)
{
    local KF2OptimizerTelemetryProbe CurrentProbe;
    local KF2OptimizerAdaptiveControlListener CurrentListener;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;

    if (bGameSessionEnding)
    {
        return;
    }
    if (!GetStandaloneGameplayContext(PrimaryController, CurrentWorld))
    {
        return;
    }
    if (!RestorePendingProcessGraphicsWithBackoff(CurrentWorld))
    {
        ReportOptimizerProbeState("process_graphics_restore_pending");
        return;
    }
    TryPrewarmAchievements(PrimaryController);
    UpdateGameplayUiState(PrimaryController);

    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerTelemetryProbe', CurrentProbe)
    {
        if (CurrentProbe != None && !CurrentProbe.bDeleteMe)
        {
            break;
        }
    }
    if (CurrentProbe == None || CurrentProbe.bDeleteMe)
    {
        CurrentProbe = PrimaryController.Spawn(
            class'KF2OptimizerTelemetryProbe');
    }
    if (CurrentProbe == None || CurrentProbe.bDeleteMe)
    {
        ReportOptimizerProbeState("spawn_failed");
        return;
    }
    if (CurrentProbe.AdaptiveGraphicsState == None)
    {
        CurrentProbe.AdaptiveGraphicsState =
            GetProcessAdaptiveGraphicsState();
    }
    if (CurrentProbe.AdaptiveGraphicsState == None)
    {
        ReportOptimizerProbeState("graphics_state_unavailable");
        return;
    }
    if (!EnsureFixedEffectsBaselineWithBackoff(CurrentProbe, CurrentWorld))
    {
        ReportOptimizerProbeState("fixed_effect_baseline_failed");
        return;
    }
    if (!bProcessAdaptiveRuntimeStateInitialized)
    {
        SetProcessAdaptiveRuntimeEnabled(
            CurrentProbe.bAdaptiveRuntimeEnabled);
    }
    if (CurrentProbe.bAdaptiveRuntimeEnabled !=
        bProcessAdaptiveRuntimeEnabled &&
        !CurrentProbe.SetAdaptiveRuntimeEnabled(
            bProcessAdaptiveRuntimeEnabled))
    {
        ReportOptimizerProbeState("adaptive_mode_sync_failed");
        return;
    }
    if (Len(CurrentProbe.AdaptiveControlToken) < 32)
    {
        ReportOptimizerProbeState("token_unavailable");
        return;
    }
    ReportOptimizerProbeState("ready");

    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerAdaptiveControlListener', CurrentListener)
    {
        if (CurrentListener != None && !CurrentListener.bDeleteMe)
        {
            return;
        }
    }
    PrimaryController.Spawn(class'KF2OptimizerAdaptiveControlListener');
}

function NotifyGameSessionEnded()
{
    local bool bProcessGraphicsRestored;
    local bool bProbeFound;
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local KF2OptimizerTelemetryProbe CurrentProbe;

    if (bGameSessionEnding)
    {
        return;
    }

    // GameViewportClient calls this before unloading the current map. Stop all
    // Stop Tick access immediately so the persistent interaction cannot
    // touch a controller, world or render object while UE3 tears them down.
    bGameSessionEnding = true;
    ClearAchievementPrewarmDelegate();
    bAchievementPrewarmRequested = false;
    AchievementPrewarmRequestStartedRealTime = 0.0;
    bProcessGraphicsRestored = class'KF2OptimizerAdaptiveGraphics'.static.
        RestoreOriginal(
        ProcessAdaptiveGraphicsState);
    if (bProcessGraphicsRestored)
    {
        bProcessGraphicsRestorePending = false;
        ResetProcessGraphicsRestoreRetry();
        `log("KF2OPT_SESSION_TEARDOWN state=restored boundary=session_end"$
             " domain=process_graphics readback=verified ownership=released");
    }
    else
    {
        bProcessGraphicsRestorePending = true;
        ResetProcessGraphicsRestoreRetry();
        `log("KF2OPT_SESSION_TEARDOWN state=restore_deferred"$
             " boundary=session_end domain=process_graphics"$
             " reason=readback_mismatch ownership=retained");
    }
    if (GamePlayers.Length > 0)
    {
        PrimaryPlayer = GamePlayers[0];
        if (PrimaryPlayer != None)
        {
            PrimaryController = PrimaryPlayer.Actor;
        }
    }
    if (PrimaryController != None)
    {
        CurrentWorld = PrimaryController.WorldInfo;
    }
    if (CurrentWorld != None)
    {
        foreach CurrentWorld.DynamicActors(
            class'KF2OptimizerTelemetryProbe', CurrentProbe)
        {
            if (CurrentProbe != None && !CurrentProbe.bDeleteMe)
            {
                bProbeFound = true;
                // Only world-owned manager/emitter state depends on the probe.
                // The interaction restored process graphics independently.
                CurrentProbe.RestoreSessionWorldRuntime();
                CurrentProbe.QuiesceForWorldTeardown();
            }
        }
    }
    if (!bProbeFound)
    {
        `log("KF2OPT_SESSION_TEARDOWN state=probe_missing"$
             " boundary=session_end domain=world_runtime"$
             " reason="$((CurrentWorld == None) ?
                "world_unavailable" : "probe_unavailable"));
    }
    OptimizerContextState = "";
    OptimizerProbeState = "";
    OptimizerGameplayUiState = "";
    if (bProcessGraphicsRestorePending)
    {
        `log("KF2OPT_INTERACTION schema=1"$
             " state=session_end_restore_pending");
    }
    else
    {
        `log("KF2OPT_INTERACTION schema=1 state=session_ended");
    }
}

function NotifyPlayerAdded(int PlayerIndex, LocalPlayer AddedPlayer)
{
    // Do not rearm here. KF2 also adds local players while returning to the
    // main menu. InitMutator confirms the next standalone gameplay world and
    // calls PrepareForGameplayWorld instead.
}

defaultproperties
{
}
