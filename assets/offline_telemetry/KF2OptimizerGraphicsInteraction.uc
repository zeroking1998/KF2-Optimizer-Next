// Reports KF2's applied graphics state and exact map choices from its UI.
// Staged graphics selections are deliberately not reported until KF2 applies them.
class KF2OptimizerGraphicsInteraction extends Interaction
    within GameViewportClient;

var float NextReadRealTime;
var float LastObservedRealTime;
var string LastReadback;
var string LastSelectedMap;
var string LastVotedMap;
var float NextWeaponMaterialGuardRealTime;

function bool EnsureTurretWeaponMaterial(KFWeapon Weapon)
{
    local MaterialInstanceConstant LedMaterial;
    local KFWeap_HRG_Warthog Warthog;
    local KFWeap_AutoTurret AutoTurret;

    if (Weapon == None || Weapon.bDeleteMe || Weapon.Mesh == None ||
        !Weapon.WeaponContentLoaded || Weapon.Mesh.GetNumElements() <= 2)
    {
        return false;
    }
    Warthog = KFWeap_HRG_Warthog(Weapon);
    AutoTurret = KFWeap_AutoTurret(Weapon);
    if (Warthog == None && AutoTurret == None)
    {
        return false;
    }
    if (Weapon.WeaponMICs.Length > 2 && Weapon.WeaponMICs[2] != None)
    {
        return false;
    }

    if (Weapon.WeaponMICs.Length < 3)
    {
        Weapon.WeaponMICs.Length = 3;
    }
    LedMaterial = Weapon.Mesh.CreateAndSetMaterialInstanceConstant(2);
    if (LedMaterial == None)
    {
        return false;
    }
    Weapon.WeaponMICs[2] = LedMaterial;
    if (Weapon.WeaponMICs[2] != LedMaterial)
    {
        return false;
    }

    if (Warthog != None)
    {
        Warthog.UpdateMaterialColor(Warthog.CurrentAmmoPercentage);
    }
    else
    {
        AutoTurret.UpdateMaterialColor(AutoTurret.CurrentAmmoPercentage);
    }
    `log("KF2OPT_WEAPON_MIC state=repaired weapon="$PathName(Weapon)$
         " material=2 local_only=true readback=verified");
    return true;
}

function GuardTurretWeaponMaterials(WorldInfo CurrentWorld)
{
    local KFWeap_HRG_Warthog Warthog;
    local KFWeap_AutoTurret AutoTurret;

    if (CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds < NextWeaponMaterialGuardRealTime)
    {
        return;
    }
    NextWeaponMaterialGuardRealTime = CurrentWorld.RealTimeSeconds + 0.10;
    // Deployed turret throwers are detached world actors, not members of the
    // local pawn's inventory chain. Cover every locally replicated instance so
    // remote and local Warthogs receive the missing third MIC before their
    // replicated ammo callbacks use it.
    foreach CurrentWorld.DynamicActors(class'KFWeap_HRG_Warthog', Warthog)
    {
        EnsureTurretWeaponMaterial(Warthog);
    }
    foreach CurrentWorld.DynamicActors(class'KFWeap_AutoTurret', AutoTurret)
    {
        EnsureTurretWeaponMaterial(AutoTurret);
    }
}

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
    if (CurrentWorld == None)
    {
        return;
    }
    // RealTimeSeconds starts at zero for each newly loaded world. Detect that
    // clock reset without retaining the old WorldInfo, which would prevent
    // Unreal's garbage collector from releasing the previous map.
    if (CurrentWorld.RealTimeSeconds < LastObservedRealTime)
    {
        NextReadRealTime = 0.0;
        NextWeaponMaterialGuardRealTime = 0.0;
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    GuardTurretWeaponMaterials(CurrentWorld);
    if (CurrentWorld.NetMode != NM_Standalone)
    {
        return;
    }
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
