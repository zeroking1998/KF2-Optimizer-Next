// Protected-session viewport bridge for the KF2 Optimizer telemetry probe.
// It preserves KF2's viewport behavior, creates the aggregate probe only in a
// standalone gameplay world, and draws optional diagnostic corpse markers.
// World and actor references remain local so this root-owned viewport cannot
// retain an obsolete map during server travel.
class KF2OptimizerTelemetryViewport extends KFGameViewportClient;

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
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;

    Super.Tick(DeltaTime);

    if (!GetStandaloneGameplayContext(PrimaryController, CurrentWorld))
    {
        return;
    }

    foreach CurrentWorld.DynamicActors(
        class'KF2OptimizerTelemetryProbe', CurrentProbe)
    {
        if (CurrentProbe != None && !CurrentProbe.bDeleteMe)
        {
            return;
        }
    }

    PrimaryController.Spawn(class'KF2OptimizerTelemetryProbe');
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
