// Reads KF2's applied graphics state in its standalone main menu. Staged menu
// selections are deliberately not reported until KF2 has applied them.
class KF2OptimizerGraphicsInteraction extends Interaction
    within GameViewportClient;

var float NextReadRealTime;
var string LastReadback;

event Tick(float DeltaTime)
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local string Readback;

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
    if (CurrentWorld == None || CurrentWorld.NetMode != NM_Standalone ||
        !(CurrentWorld.GetMapName(true) ~= "KFMainMenu") ||
        CurrentWorld.RealTimeSeconds < NextReadRealTime)
    {
        return;
    }
    NextReadRealTime = CurrentWorld.RealTimeSeconds + 0.5;
    Readback = class'KF2OptimizerAdaptiveGraphics'.static.MenuReadback();
    if (Readback == "" || Readback == LastReadback)
    {
        return;
    }
    LastReadback = Readback;
    `log("KF2OPT_GFX_MENU schema=1 state=applied " $ Readback);
}
