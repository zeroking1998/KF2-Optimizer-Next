// Protected-session viewport bridge for the KF2 Optimizer telemetry probe.
// It preserves KF2's viewport behavior, creates the aggregate probe only in a
// standalone gameplay world, and draws optional diagnostic corpse markers.
// World and actor references remain local so this root-owned viewport cannot
// retain an obsolete map during server travel.
class KF2OptimizerTelemetryViewport extends KFGameViewportClient;

var transient int RuntimeTargetFPS;

function bool ApplyAdaptiveTargetFPS(int TargetFPS)
{
    local Engine CurrentEngine;
    local bool PreviousSmoothFrameRate;
    local float PreviousMinSmoothedFrameRate;
    local float PreviousMaxSmoothedFrameRate;

    if (TargetFPS < 30 || TargetFPS > 240)
    {
        return false;
    }
    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None)
    {
        return false;
    }
    PreviousSmoothFrameRate = CurrentEngine.bSmoothFrameRate;
    PreviousMinSmoothedFrameRate = CurrentEngine.MinSmoothedFrameRate;
    PreviousMaxSmoothedFrameRate = CurrentEngine.MaxSmoothedFrameRate;
    CurrentEngine.bSmoothFrameRate = true;
    CurrentEngine.MinSmoothedFrameRate = 22.0;
    CurrentEngine.MaxSmoothedFrameRate = float(TargetFPS);
    if (!CurrentEngine.bSmoothFrameRate ||
        Abs(CurrentEngine.MinSmoothedFrameRate - 22.0) > 0.01 ||
        Abs(CurrentEngine.MaxSmoothedFrameRate - float(TargetFPS)) > 0.01)
    {
        CurrentEngine.bSmoothFrameRate = PreviousSmoothFrameRate;
        CurrentEngine.MinSmoothedFrameRate = PreviousMinSmoothedFrameRate;
        CurrentEngine.MaxSmoothedFrameRate = PreviousMaxSmoothedFrameRate;
        `log("KF2OPT_TARGET_FPS state=failed target="$TargetFPS$
             " reason=readback_mismatch");
        return false;
    }

    RuntimeTargetFPS = TargetFPS;
    `log("KF2OPT_TARGET_FPS state=configured target="$TargetFPS$
         " effective="$int(CurrentEngine.MaxSmoothedFrameRate)$
         " minimum="$int(CurrentEngine.MinSmoothedFrameRate)$
         " provider=engine_smoothing readback=property_only");
    return true;
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
        return false;
    }

    PrimaryPlayer = GamePlayers[0];
    if (PrimaryPlayer != None)
    {
        PrimaryController = PrimaryPlayer.Actor;
    }
    if (PrimaryController != None)
    {
        CurrentWorld = PrimaryController.WorldInfo;
    }

    return CurrentWorld != None &&
        CurrentWorld.NetMode == NM_Standalone &&
        !(CurrentWorld.GetMapName(true) ~= "KFMainMenu");
}

event Tick(float DeltaTime)
{
    local KF2OptimizerTelemetryProbe CurrentProbe;
    local KF2OptimizerAdaptiveControlListener CurrentListener;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;

    Super.Tick(DeltaTime);

    if (RuntimeTargetFPS == 0 &&
        class'KF2OptimizerTelemetryProbe'.default.AdaptiveTargetFPS >= 30 &&
        class'KF2OptimizerTelemetryProbe'.default.AdaptiveTargetFPS <= 240)
    {
        ApplyAdaptiveTargetFPS(
            class'KF2OptimizerTelemetryProbe'.default.AdaptiveTargetFPS);
    }
    else if (RuntimeTargetFPS >= 30 && RuntimeTargetFPS <= 240 &&
             (!class'Engine'.static.GetEngine().bSmoothFrameRate ||
              Abs(class'Engine'.static.GetEngine().MinSmoothedFrameRate -
                  22.0) > 0.01 ||
              Abs(class'Engine'.static.GetEngine().MaxSmoothedFrameRate -
                  float(RuntimeTargetFPS)) > 0.01))
    {
        // KF2's profile can rewrite this after startup. Reassert only when
        // the exact owned value changed, then verify it again.
        ApplyAdaptiveTargetFPS(RuntimeTargetFPS);
    }

    if (!GetStandaloneGameplayContext(PrimaryController, CurrentWorld))
    {
        return;
    }

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
    if (CurrentProbe == None || CurrentProbe.bDeleteMe ||
        Len(CurrentProbe.AdaptiveControlToken) < 32)
    {
        return;
    }

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

event PostRender(Canvas MarkerCanvas)
{
    local KF2OptimizerTelemetryProbe CurrentProbe;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;

    Super.PostRender(MarkerCanvas);

    if (!GetStandaloneGameplayContext(PrimaryController, CurrentWorld))
    {
        return;
    }

    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerTelemetryProbe', CurrentProbe)
    {
        if (CurrentProbe != None && !CurrentProbe.bDeleteMe)
        {
            CurrentProbe.DrawAdaptiveCorpseDebugMarkers(MarkerCanvas);
            return;
        }
    }
}

defaultproperties
{
}
