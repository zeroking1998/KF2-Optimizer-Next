// Persistent viewport interaction inserted into KF2's native viewport by the
// Published mutator. It creates the protected provider only in a standalone
// gameplay world and draws optional corpse IDs.
class KF2OptimizerTelemetryInteraction extends Interaction
    within GameViewportClient;

var string OptimizerContextState;
var string OptimizerProbeState;
var bool bGameSessionEnding;

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

event PostRender(Canvas MarkerCanvas)
{
    local KF2OptimizerTelemetryProbe CurrentProbe;
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

function NotifyGameSessionEnded()
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local KF2OptimizerTelemetryProbe CurrentProbe;

    // GameViewportClient calls this before unloading the current map. Stop all
    // Tick/PostRender access immediately so the persistent interaction cannot
    // touch a controller, world or render object while UE3 tears them down.
    bGameSessionEnding = true;
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
                CurrentProbe.QuiesceForWorldTeardown();
            }
        }
    }
    OptimizerContextState = "";
    OptimizerProbeState = "";
    `log("KF2OPT_INTERACTION schema=1 state=session_ended");
}

function NotifyPlayerAdded(int PlayerIndex, LocalPlayer AddedPlayer)
{
    // A player added by the next map is the engine-owned rearm boundary. The
    // next Tick performs all normal standalone/gameplay validation again.
    bGameSessionEnding = false;
    OptimizerContextState = "";
    OptimizerProbeState = "";
}

defaultproperties
{
}
