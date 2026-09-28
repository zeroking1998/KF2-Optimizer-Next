// Published-only offline entry point. KF2 resolves this mutator after the
// gameplay URL is opened, when Published packages are available.
class KF2OptimizerTelemetryMutator extends KFMutator;

const TelemetryBootstrapMaxAttempts=5;
const TelemetryBootstrapInitialRetrySeconds=0.25;
const TelemetryBootstrapMaximumRetrySeconds=2.0;

var int TelemetryBootstrapAttempts;
var float TelemetryBootstrapRetryDelay;

function bool CanBootstrapTelemetry()
{
    return !bDeleteMe && WorldInfo != None &&
        WorldInfo.NetMode == NM_Standalone;
}

function ScheduleTelemetryBootstrapRetry(string Reason)
{
    local int RetryDelayMs;

    ClearTimer(nameof(RetryTelemetryBootstrap), self);
    if (!CanBootstrapTelemetry())
    {
        return;
    }
    if (TelemetryBootstrapAttempts >= TelemetryBootstrapMaxAttempts)
    {
        `log("KF2OPT_MUTATOR schema=1 state=failed reason="$Reason$
             " attempts="$TelemetryBootstrapAttempts);
        return;
    }
    if (TelemetryBootstrapRetryDelay <= 0.0)
    {
        TelemetryBootstrapRetryDelay =
            TelemetryBootstrapInitialRetrySeconds;
    }
    else
    {
        TelemetryBootstrapRetryDelay = FMin(
            TelemetryBootstrapMaximumRetrySeconds,
            TelemetryBootstrapRetryDelay * 2.0);
    }
    RetryDelayMs = int(TelemetryBootstrapRetryDelay * 1000.0);
    SetTimer(TelemetryBootstrapRetryDelay, false,
             nameof(RetryTelemetryBootstrap), self);
    `log("KF2OPT_MUTATOR schema=1 state=retry_scheduled reason="$Reason$
         " attempt="$TelemetryBootstrapAttempts$
         " delay_ms="$RetryDelayMs);
}

function TryBootstrapTelemetry()
{
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local KF2OptimizerTelemetryInteraction CurrentInteraction;
    local string InteractionPath;

    if (!CanBootstrapTelemetry())
    {
        return;
    }
    ++TelemetryBootstrapAttempts;
    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None || CurrentEngine.GameViewport == None)
    {
        `log("KF2OPT_MUTATOR schema=1 state=viewport_unavailable");
        ScheduleTelemetryBootstrapRetry("viewport_unavailable");
        return;
    }
    CurrentViewport = CurrentEngine.GameViewport;
    InteractionPath = PathName(CurrentViewport)$
        ".KF2OptimizerTelemetryInteraction";
    CurrentInteraction = KF2OptimizerTelemetryInteraction(
        FindObject(InteractionPath,
            class'KF2OptimizerTelemetryInteraction'));
    if (CurrentInteraction != None &&
        CurrentInteraction.IsTelemetryBootstrapInserted())
    {
        ClearTimer(nameof(RetryTelemetryBootstrap), self);
        CurrentInteraction.PrepareForGameplayWorld();
        `log("KF2OPT_MUTATOR schema=1 state=ready interaction=existing");
        return;
    }

    // A named object survives a failed insertion. Reuse it rather than
    // allocating another interaction or treating it as already active.
    if (CurrentInteraction == None)
    {
        CurrentInteraction = new(CurrentViewport,
            "KF2OptimizerTelemetryInteraction")
            class'KF2OptimizerTelemetryInteraction';
    }
    if (CurrentInteraction == None)
    {
        `log("KF2OPT_MUTATOR schema=1 state=interaction_failed");
        ScheduleTelemetryBootstrapRetry("interaction_failed");
        return;
    }
    if (CurrentViewport.InsertInteraction(CurrentInteraction) == -1)
    {
        `log("KF2OPT_MUTATOR schema=1 state=insertion_failed");
        ScheduleTelemetryBootstrapRetry("insertion_failed");
        return;
    }
    CurrentInteraction.MarkTelemetryBootstrapInserted();
    ClearTimer(nameof(RetryTelemetryBootstrap), self);
    CurrentInteraction.PrepareForGameplayWorld();
    `log("KF2OPT_MUTATOR schema=1 state=ready interaction=inserted");
}

function RetryTelemetryBootstrap()
{
    if (!CanBootstrapTelemetry())
    {
        ClearTimer(nameof(RetryTelemetryBootstrap), self);
        return;
    }
    TryBootstrapTelemetry();
}

function RequestInitialMapSettle()
{
    if (WorldInfo.bRequestedBlockOnAsyncLoading)
    {
        `log("KF2OPT_MAP_SETTLE schema=1 state=already_pending map="$
            WorldInfo.GetMapName(true));
        return;
    }

    WorldInfo.bRequestedBlockOnAsyncLoading = true;
    `log("KF2OPT_MAP_SETTLE schema=1 state=requested map="$
        WorldInfo.GetMapName(true));
}

function InitMutator(string Options, out string ErrorMessage)
{
    Super.InitMutator(Options, ErrorMessage);
    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_MUTATOR schema=1 state=blocked_net_mode");
        Destroy();
        return;
    }

    RequestInitialMapSettle();
    TryBootstrapTelemetry();
}

event Destroyed()
{
    ClearTimer(nameof(RetryTelemetryBootstrap), self);
    Super.Destroyed();
}

defaultproperties
{
    GroupNames.Add("KF2OptimizerTelemetry")
}
