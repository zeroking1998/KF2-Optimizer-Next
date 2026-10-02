// Temporary KF2 viewport subclass used only for a protected Optimizer session.
// All native KFGameViewportClient behavior is inherited. The app restores the
// exact original KFEngine.ini after KF2 exits.
class KF2OptimizerGraphicsViewport extends KFGameViewportClient;

// Viewport-owned only: never retain a World or Actor across server travel.
var private KF2OptimizerOnlineContextInteraction OnlineMonitor;
// A user FPS limit belongs to the process, not one World or Adaptive mode.
var private int FrameRateLastSequence;

function string FrameRateReadback()
{
    local Engine CurrentEngine;
    local int Limit;

    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine != None && CurrentEngine.GameViewport == self &&
        CurrentEngine.bSmoothFrameRate && CurrentEngine.MinSmoothedFrameRate == 22.0 &&
        CurrentEngine.MaxSmoothedFrameRate >= 30.0 &&
        CurrentEngine.MaxSmoothedFrameRate <= 240.0 &&
        CurrentEngine.MaxSmoothedFrameRate == float(int(CurrentEngine.MaxSmoothedFrameRate)))
    {
        Limit = int(CurrentEngine.MaxSmoothedFrameRate);
    }
    return " frame_rate_sequence="$FrameRateLastSequence$" frame_rate_limit="$Limit;
}

function string ApplyRuntimeFrameRate(string Token, int Sequence, int TargetFPS)
{
    local Engine CurrentEngine;
    local float PreviousMaximum, PreviousMinimum;
    local bool bPreviousSmoothing;

    if (Len(class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken) != 32 ||
        Token != class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken ||
        Sequence <= FrameRateLastSequence || Sequence <= 0 ||
        TargetFPS < 30 || TargetFPS > 240)
    {
        return "unsupported";
    }
    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None || CurrentEngine.GameViewport != self)
    {
        return "unsupported";
    }
    FrameRateLastSequence = Sequence;
    PreviousMaximum = CurrentEngine.MaxSmoothedFrameRate;
    PreviousMinimum = CurrentEngine.MinSmoothedFrameRate;
    bPreviousSmoothing = CurrentEngine.bSmoothFrameRate;
    // The shipping instance fields control the real native limiter. Do not
    // use t.MaxFPS (unsupported here), class defaults, or SaveConfig.
    CurrentEngine.MaxSmoothedFrameRate = float(TargetFPS);
    CurrentEngine.MinSmoothedFrameRate = 22.0;
    CurrentEngine.bSmoothFrameRate = true;
    if (CurrentEngine.MaxSmoothedFrameRate == float(TargetFPS) &&
        CurrentEngine.MinSmoothedFrameRate == 22.0 && CurrentEngine.bSmoothFrameRate)
    {
        // A process-owned goal also covers an existing probe and later maps.
        // This is memory-only; never persist a protected session's config.
        class'KF2OptimizerTelemetryProbe'.default.AdaptiveTargetFPS = TargetFPS;
        `log("KF2OPT_FRAME_RATE state=applied target_fps="$TargetFPS$
             " sequence="$Sequence$" readback=verified local_only=true");
        return "applied";
    }
    CurrentEngine.MaxSmoothedFrameRate = PreviousMaximum;
    CurrentEngine.MinSmoothedFrameRate = PreviousMinimum;
    CurrentEngine.bSmoothFrameRate = bPreviousSmoothing;
    if (CurrentEngine.MaxSmoothedFrameRate == PreviousMaximum &&
        CurrentEngine.MinSmoothedFrameRate == PreviousMinimum &&
        CurrentEngine.bSmoothFrameRate == bPreviousSmoothing)
    {
        return "restored";
    }
    return "unknown";
}

function KF2OptimizerOnlineContextInteraction GetOnlineMonitor()
{
    if (OnlineMonitor == None || GlobalInteractions.Find(OnlineMonitor) == -1)
    {
        return None;
    }
    return OnlineMonitor;
}

function bool RestoreAdaptiveAtMainMenu(
    string Token, int Sequence, WorldInfo CurrentWorld)
{
    local Engine CurrentEngine;
    local KF2OptimizerTelemetryInteraction OfflineInteraction;
    local KF2OptimizerOnlineContextInteraction OnlineInteraction;
    local bool bOfflineRestored;

    if (Len(class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken) != 32 ||
        Token != class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken ||
        Sequence <= 0 || CurrentWorld == None ||
        CurrentWorld.NetMode != NM_Standalone ||
        !(Left(CurrentWorld.GetMapName(true), 10) ~= "KFMainMenu"))
    {
        return false;
    }
    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None || CurrentEngine.GameViewport != self)
    {
        return false;
    }
    OnlineInteraction = GetOnlineMonitor();
    if (OnlineInteraction == None)
    {
        return false;
    }
    // A first menu has neither an offline interaction nor a gameplay probe.
    // If a previous map created one, restore its retained process ownership.
    OfflineInteraction = KF2OptimizerTelemetryInteraction(FindObject(
        PathName(self)$".KF2OptimizerTelemetryInteraction",
        class'KF2OptimizerTelemetryInteraction'));
    bOfflineRestored = true;
    if (OfflineInteraction != None)
    {
        bOfflineRestored = class'KF2OptimizerAdaptiveGraphics'.static.
            RestoreOriginal(OfflineInteraction.ProcessAdaptiveGraphicsState);
        OfflineInteraction.bProcessGraphicsRestorePending = !bOfflineRestored;
        OfflineInteraction.ResetProcessGraphicsRestoreRetry();
    }
    if (!bOfflineRestored ||
        !OnlineInteraction.RestoreOnlineSessionState(CurrentWorld, "main_menu"))
    {
        return false;
    }
    if (OfflineInteraction != None)
    {
        OfflineInteraction.SetProcessAdaptiveRuntimeEnabled(false);
    }
    // Idempotent restoration needs no shared counter after an app restart and
    // never changes the FPS limit or its process-owned readback sequence.
    `log("KF2OPT_ADAPTIVE_BRIDGE state=restored boundary=main_menu"$
         " sequence="$Sequence$" readback=verified ownership=released");
    return true;
}

event bool Init(out string OutError)
{
    local KF2OptimizerGraphicsInteraction Monitor;

    if (!Super.Init(OutError))
    {
        return false;
    }
    Monitor = new(self, "KF2OptimizerGraphicsInteraction")
        class'KF2OptimizerGraphicsInteraction';
    if (Monitor == None || InsertInteraction(Monitor) == -1)
    {
        OutError = "KF2 Optimizer graphics monitor could not start";
        return false;
    }
    OnlineMonitor = new(self, "KF2OptimizerOnlineContextInteraction")
        class'KF2OptimizerOnlineContextInteraction';
    if (OnlineMonitor == None || InsertInteraction(OnlineMonitor) == -1)
    {
        OutError = "KF2 Optimizer online context monitor could not start";
        return false;
    }
    `log("KF2OPT_GFX_BOOTSTRAP schema=1 state=ready");
    return true;
}
