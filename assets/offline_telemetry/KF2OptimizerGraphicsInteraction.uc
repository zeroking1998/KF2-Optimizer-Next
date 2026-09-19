// Reports KF2's applied graphics state and exact map choices from its UI.
// Staged graphics selections are deliberately not reported until KF2 applies them.
class KF2OptimizerGraphicsInteraction extends Interaction
    within GameViewportClient;

var float NextReadRealTime;
var float LastObservedRealTime;
var string LastReadback;
var string LastSelectedMap;
var string LastVotedMap;

event Tick(float DeltaTime)
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local KFPlayerController KFPC;
    local string Readback;
    local string SelectedMap;

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
    if (CurrentWorld == None || CurrentWorld.NetMode != NM_Standalone)
    {
        return;
    }
    // RealTimeSeconds starts at zero for each newly loaded world. Detect that
    // clock reset without retaining the old WorldInfo, which would prevent
    // Unreal's garbage collector from releasing the previous map.
    if (CurrentWorld.RealTimeSeconds < LastObservedRealTime)
    {
        NextReadRealTime = 0.0;
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    if (CurrentWorld.RealTimeSeconds < NextReadRealTime)
    {
        return;
    }
    NextReadRealTime = CurrentWorld.RealTimeSeconds + 0.5;
    KFPC = KFPlayerController(PrimaryController);

    if (CurrentWorld.GetMapName(true) ~= "KFMainMenu")
    {
        LastVotedMap = "";
        if (KFPC != None && KFPC.MyGFxManager != None &&
            KFPC.MyGFxManager.StartMenu != None &&
            KFPC.MyGFxManager.StartMenu.OptionsComponent != None)
        {
            SelectedMap = KFPC.MyGFxManager.StartMenu.OptionsComponent.GetMapName();
            if (SelectedMap != "" && !(SelectedMap ~= LastSelectedMap))
            {
                LastSelectedMap = SelectedMap;
                `log("KF2OPT_MAP_SELECTION schema=1 state=menu map="$SelectedMap);
            }
        }
        Readback = class'KF2OptimizerAdaptiveGraphics'.static.MenuReadback();
        if (Readback != "" && Readback != LastReadback)
        {
            LastReadback = Readback;
            `log("KF2OPT_GFX_MENU schema=1 state=applied " $ Readback);
        }
        return;
    }
    LastSelectedMap = "";
    if (KFPC != None && KFPC.MyGFxManager != None &&
        KFPC.MyGFxManager.PostGameMenu != None &&
        KFPC.MyGFxManager.PostGameMenu.CurrentTopVoteObject.Map1Votes > 0 &&
        KFPC.MyGFxManager.PostGameMenu.CurrentTopVoteObject.Map1Votes < 255)
    {
        SelectedMap = KFPC.MyGFxManager.PostGameMenu.CurrentTopVoteObject.Map1Name;
        if (SelectedMap != "" && !(SelectedMap ~= LastVotedMap))
        {
            LastVotedMap = SelectedMap;
            `log("KF2OPT_MAP_SELECTION schema=1 state=vote map="$SelectedMap);
        }
    }
}
