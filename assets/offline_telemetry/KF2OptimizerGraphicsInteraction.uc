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
var float NextPawnRuntimeGuardRealTime;
var bool bFireAfflictionGuardReported;
var bool bWeaponClassFallbackGuardReported;

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

function bool EnsureWeaponClassFallback(KFPawn Pawn)
{
    local KFWeapon CurrentWeapon;

    if (Pawn == None || Pawn.bDeleteMe ||
        KFPawn_Human(Pawn) == None ||
        Pawn.WeaponClassForAttachmentTemplate != None)
    {
        return false;
    }

    // A remote fire notification can arrive before the replicated weapon
    // class. KFPawn.WeaponFired dereferences that class without a null guard.
    // Prefer the exact local weapon class when it exists. Otherwise the base
    // neutral fallback always returns no projectile, which preserves KF2's
    // generic-impact fallback until replication supplies the real class. Do
    // not rebuild the attachment from this temporary value.
    CurrentWeapon = KFWeapon(Pawn.Weapon);
    if (CurrentWeapon != None)
    {
        Pawn.WeaponClassForAttachmentTemplate = CurrentWeapon.Class;
    }
    else
    {
        Pawn.WeaponClassForAttachmentTemplate =
            class'KF2OptimizerWeaponFallback';
    }
    return Pawn.WeaponClassForAttachmentTemplate != None;
}

function GuardPawnRuntimeClasses(WorldInfo CurrentWorld)
{
    local KFPawn Pawn;
    local int UpdatedAfflictionCount;
    local int UpdatedWeaponClassCount;

    if (CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds < NextPawnRuntimeGuardRealTime)
    {
        return;
    }
    NextPawnRuntimeGuardRealTime = CurrentWorld.RealTimeSeconds + 0.10;

    foreach CurrentWorld.DynamicActors(class'KFPawn', Pawn)
    {
        if (EnsureWeaponClassFallback(Pawn))
        {
            ++UpdatedWeaponClassCount;
        }
        if (Pawn == None || Pawn.bDeleteMe ||
            Pawn.AfflictionHandler == None ||
            Pawn.AfflictionHandler.AfflictionClasses.Length <= AF_FirePanic ||
            Pawn.AfflictionHandler.AfflictionClasses[AF_FirePanic] ==
                class'KF2OptimizerFireAffliction')
        {
            continue;
        }
        Pawn.AfflictionHandler.AfflictionClasses[AF_FirePanic] =
            class'KF2OptimizerFireAffliction';
        if (Pawn.AfflictionHandler.AfflictionClasses[AF_FirePanic] ==
            class'KF2OptimizerFireAffliction')
        {
            ++UpdatedAfflictionCount;
        }
    }
    if (UpdatedWeaponClassCount > 0 && !bWeaponClassFallbackGuardReported)
    {
        bWeaponClassFallbackGuardReported = true;
        `log("KF2OPT_WEAPON_CLASS_FALLBACK state=active initial_pawns="$
             UpdatedWeaponClassCount$
             " local_only=true exact_weapon_preferred=true");
    }
    if (UpdatedAfflictionCount > 0 && !bFireAfflictionGuardReported)
    {
        bFireAfflictionGuardReported = true;
        `log("KF2OPT_FIRE_AFFLICTION state=active initial_pawns="$
             UpdatedAfflictionCount$
             " local_only=true behavior=preserved warning=removed");
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
        NextPawnRuntimeGuardRealTime = 0.0;
        bFireAfflictionGuardReported = false;
        bWeaponClassFallbackGuardReported = false;
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    GuardTurretWeaponMaterials(CurrentWorld);
    GuardPawnRuntimeClasses(CurrentWorld);
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
