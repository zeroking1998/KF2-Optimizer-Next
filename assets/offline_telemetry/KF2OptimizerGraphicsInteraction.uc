// Reports KF2's applied graphics state and exact map choices from its UI.
// Staged graphics selections are deliberately not reported until KF2 applies them.
class KF2OptimizerGraphicsInteraction extends Interaction
    within GameViewportClient;

const RuntimeGuardInitialSeconds=0.05;
const RuntimeGuardMaximumSeconds=0.25;

var float NextReadRealTime;
var float LastObservedRealTime;
var string LastReadback;
var KF2OptimizerAdaptiveGraphicsState PreviousMenuGraphicsState;
var KF2OptimizerAdaptiveGraphicsState CurrentMenuGraphicsState;
var bool bMenuGraphicsStateInitialized;
var bool bGraphicsMenuWasOpen;
var string LastSelectedMap;
var string LastVotedMap;
var string LastRuntimeGuardMapName;
var float NextRuntimeGuardRealTime;
var float RuntimeGuardIntervalSeconds;
var bool bFireAfflictionGuardReported;
var bool bWeaponClassFallbackGuardReported;
var bool bMenuFrameRateListenerReady;
var float NextMenuFrameRateListenerRealTime;
var float MenuFrameRateListenerRetryDelay;

function EnsureMenuFrameRateListener(WorldInfo CurrentWorld, PlayerController Controller)
{
    local KF2OptimizerAdaptiveControlListener Listener;

    if (bMenuFrameRateListenerReady || CurrentWorld == None || Controller == None ||
        CurrentWorld.NetMode != NM_Standalone ||
        !(CurrentWorld.GetMapName(true) ~= "KFMainMenu") ||
        Len(class'KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken) != 32 ||
        CurrentWorld.RealTimeSeconds < NextMenuFrameRateListenerRealTime)
    {
        return;
    }
    foreach CurrentWorld.DynamicActors(class'KF2OptimizerAdaptiveControlListener', Listener)
    {
        if (Listener != None && !Listener.bDeleteMe && Listener.LinkState == STATE_Listening)
        {
            bMenuFrameRateListenerReady = true;
            return;
        }
    }
    Listener = Controller.Spawn(class'KF2OptimizerAdaptiveControlListener');
    bMenuFrameRateListenerReady = Listener != None && !Listener.bDeleteMe &&
        Listener.LinkState == STATE_Listening;
    if (!bMenuFrameRateListenerReady)
    {
        if (MenuFrameRateListenerRetryDelay <= 0.0) MenuFrameRateListenerRetryDelay = 0.5;
        NextMenuFrameRateListenerRealTime = CurrentWorld.RealTimeSeconds +
            MenuFrameRateListenerRetryDelay;
        MenuFrameRateListenerRetryDelay = FMin(8.0, MenuFrameRateListenerRetryDelay * 2.0);
    }
}

function bool EnsureTurretWeaponMaterial(KFWeapon Weapon)
{
    local MaterialInstanceConstant LedMaterial;
    local KFWeap_HRG_Warthog Warthog;
    local KFWeap_AutoTurret AutoTurret;

    if (Weapon == None || Weapon.bDeleteMe)
    {
        return false;
    }
    Warthog = KFWeap_HRG_Warthog(Weapon);
    AutoTurret = KFWeap_AutoTurret(Weapon);
    if (Warthog == None && AutoTurret == None)
    {
        return false;
    }
    if (Weapon.Mesh == None || !Weapon.WeaponContentLoaded ||
        Weapon.Mesh.GetNumElements() <= 2)
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

function bool ReplaceExistingFireAffliction(KFPawn Pawn)
{
    local KFAfflictionBase ExistingBase;
    local KFAffliction_Fire ExistingFire;
    local KF2OptimizerFireAffliction Replacement;
    local int TickIndex;

    if (Pawn == None || Pawn.AfflictionHandler == None ||
        Pawn.AfflictionHandler.Afflictions.Length <= AF_FirePanic)
    {
        return false;
    }
    ExistingBase = Pawn.AfflictionHandler.Afflictions[AF_FirePanic];
    if (ExistingBase == None ||
        ExistingBase.Class != class'KFAffliction_Fire')
    {
        return false;
    }
    ExistingFire = KFAffliction_Fire(ExistingBase);
    Replacement = new(Pawn) class'KF2OptimizerFireAffliction';
    if (Replacement == None || !Replacement.AdoptExisting(ExistingFire))
    {
        return false;
    }

    TickIndex = Pawn.AfflictionHandler.AfflictionTickArray.Find(ExistingBase);
    if (TickIndex != INDEX_NONE)
    {
        Pawn.AfflictionHandler.AfflictionTickArray[TickIndex] = Replacement;
    }
    else if (Replacement.bNeedsTick && Replacement.DissipationRate > 0.0)
    {
        Pawn.AfflictionHandler.AfflictionTickArray.AddItem(Replacement);
    }
    Pawn.AfflictionHandler.Afflictions[AF_FirePanic] = Replacement;
    return Pawn.AfflictionHandler.Afflictions[AF_FirePanic] == Replacement;
}

function ResetRuntimeGuardCadence()
{
    NextRuntimeGuardRealTime = 0.0;
    RuntimeGuardIntervalSeconds = RuntimeGuardInitialSeconds;
}

function GuardRuntimeActors(WorldInfo CurrentWorld)
{
    local KFWeapon Weapon;
    local KFPawn Pawn;
    local int UpdatedWeaponMaterialCount;
    local int UpdatedAfflictionCount;
    local int ReplacedAfflictionCount;
    local int UpdatedWeaponClassCount;
    local bool bChanged;

    if (CurrentWorld == None ||
        CurrentWorld.RealTimeSeconds < NextRuntimeGuardRealTime)
    {
        return;
    }
    if (RuntimeGuardIntervalSeconds < RuntimeGuardInitialSeconds)
    {
        RuntimeGuardIntervalSeconds = RuntimeGuardInitialSeconds;
    }

    // Let native iterators filter the two disjoint domains. Unrelated actors
    // need no script-level pawn/weapon casts; newly spawned weapons stay visible.
    foreach CurrentWorld.AllPawns(class'KFPawn', Pawn)
    {
        if (EnsureWeaponClassFallback(Pawn))
        {
            ++UpdatedWeaponClassCount;
        }
        if (ReplaceExistingFireAffliction(Pawn))
        {
            ++ReplacedAfflictionCount;
        }
        if (Pawn.bDeleteMe ||
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
    foreach CurrentWorld.DynamicActors(class'KFWeapon', Weapon)
    {
        if (EnsureTurretWeaponMaterial(Weapon))
        {
            ++UpdatedWeaponMaterialCount;
        }
    }
    bChanged = UpdatedWeaponMaterialCount > 0 ||
        UpdatedWeaponClassCount > 0 || UpdatedAfflictionCount > 0 ||
        ReplacedAfflictionCount > 0;
    if (bChanged)
    {
        RuntimeGuardIntervalSeconds = RuntimeGuardInitialSeconds;
    }
    else
    {
        RuntimeGuardIntervalSeconds = FMin(
            RuntimeGuardMaximumSeconds,
            RuntimeGuardIntervalSeconds * 2.0);
    }
    NextRuntimeGuardRealTime = CurrentWorld.RealTimeSeconds +
        RuntimeGuardIntervalSeconds;
    if (UpdatedWeaponClassCount > 0 && !bWeaponClassFallbackGuardReported)
    {
        bWeaponClassFallbackGuardReported = true;
        `log("KF2OPT_WEAPON_CLASS_FALLBACK state=active initial_pawns="$
             UpdatedWeaponClassCount$
             " local_only=true exact_weapon_preferred=true");
    }
    if ((UpdatedAfflictionCount > 0 || ReplacedAfflictionCount > 0) &&
        !bFireAfflictionGuardReported)
    {
        bFireAfflictionGuardReported = true;
        `log("KF2OPT_FIRE_AFFLICTION state=active initial_pawns="$
             UpdatedAfflictionCount$
             " replaced_instances="$ReplacedAfflictionCount$
             " local_only=true behavior=preserved warning=removed");
    }
}

function bool IsGraphicsMenuOpen(KFPlayerController KFPC)
{
    return KFPC != None && KFPC.MyGFxManager != None &&
        KFPC.MyGFxManager.bMenusOpen &&
        KFPC.MyGFxManager.OptionsGraphicsMenu != None &&
        KFPC.MyGFxManager.CurrentMenu ==
            KFPC.MyGFxManager.OptionsGraphicsMenu;
}

function ResetMenuGraphicsObservation()
{
    bMenuGraphicsStateInitialized = false;
    bGraphicsMenuWasOpen = false;
}

function string ObserveMenuGraphicsAndRebase()
{
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local KF2OptimizerTelemetryInteraction TelemetryInteraction;
    local KF2OptimizerOnlineContextInteraction OnlineInteraction;
    local KF2OptimizerAdaptiveGraphicsState SwapState;
    local string InteractionPath;
    local string Readback;
    local bool bOfflineRebased;
    local bool bOnlineRebased;

    if (PreviousMenuGraphicsState == None)
    {
        PreviousMenuGraphicsState = new(self)
            class'KF2OptimizerAdaptiveGraphicsState';
    }
    if (CurrentMenuGraphicsState == None)
    {
        CurrentMenuGraphicsState = new(self)
            class'KF2OptimizerAdaptiveGraphicsState';
    }
    if (PreviousMenuGraphicsState == None ||
        CurrentMenuGraphicsState == None)
    {
        return "";
    }

    Readback = class'KF2OptimizerAdaptiveGraphics'.static.
        MenuReadback(CurrentMenuGraphicsState);
    if (bMenuGraphicsStateInitialized &&
        class'KF2OptimizerAdaptiveGraphics'.static.OwnedSettingsDiffer(
            PreviousMenuGraphicsState, CurrentMenuGraphicsState))
    {
        CurrentEngine = class'Engine'.static.GetEngine();
        if (CurrentEngine != None && CurrentEngine.GameViewport != None)
        {
            CurrentViewport = CurrentEngine.GameViewport;
            InteractionPath = PathName(CurrentViewport)$
                ".KF2OptimizerTelemetryInteraction";
            TelemetryInteraction = KF2OptimizerTelemetryInteraction(
                FindObject(InteractionPath,
                    class'KF2OptimizerTelemetryInteraction'));
            InteractionPath = PathName(CurrentViewport)$
                ".KF2OptimizerOnlineContextInteraction";
            OnlineInteraction = KF2OptimizerOnlineContextInteraction(
                FindObject(InteractionPath,
                    class'KF2OptimizerOnlineContextInteraction'));
        }
        if (TelemetryInteraction != None)
        {
            bOfflineRebased = class'KF2OptimizerAdaptiveGraphics'.static.
                RebaseOriginalFromMenuChange(
                    TelemetryInteraction.
                        PeekProcessAdaptiveGraphicsState(),
                    PreviousMenuGraphicsState, CurrentMenuGraphicsState);
        }
        if (OnlineInteraction != None)
        {
            bOnlineRebased = class'KF2OptimizerAdaptiveGraphics'.static.
                RebaseOriginalFromMenuChange(
                    OnlineInteraction.PeekOnlineGraphicsState(),
                    PreviousMenuGraphicsState, CurrentMenuGraphicsState);
        }
        if (bOfflineRebased || bOnlineRebased)
        {
            `log("KF2OPT_ADAPTIVE_BASELINE state=rebased"$
                 " source=graphics_menu offline="$int(bOfflineRebased)$
                 " online="$int(bOnlineRebased)$
                 " composition=preserved");
        }
    }
    SwapState = PreviousMenuGraphicsState;
    PreviousMenuGraphicsState = CurrentMenuGraphicsState;
    CurrentMenuGraphicsState = SwapState;
    bMenuGraphicsStateInitialized = true;
    return Readback;
}

event Tick(float DeltaTime)
{
    local LocalPlayer PrimaryPlayer;
    local PlayerController PrimaryController;
    local WorldInfo CurrentWorld;
    local KFPlayerController KFPC;
    local string Readback;
    local string SelectedMap;
    local string CurrentMapName;
    local bool bGraphicsMenuOpen;
    local KF2OptimizerGraphicsViewport FrameRateViewport;

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
    CurrentMapName = CurrentWorld.GetMapName(true);
    if (CurrentWorld.RealTimeSeconds < LastObservedRealTime ||
        (Len(LastRuntimeGuardMapName) > 0 &&
         !(CurrentMapName ~= LastRuntimeGuardMapName)))
    {
        NextReadRealTime = 0.0;
        LastVotedMap = "";
        ResetRuntimeGuardCadence();
        ResetMenuGraphicsObservation();
        bFireAfflictionGuardReported = false;
        bWeaponClassFallbackGuardReported = false;
        bMenuFrameRateListenerReady = false;
        NextMenuFrameRateListenerRealTime = 0.0;
        MenuFrameRateListenerRetryDelay = 0.5;
    }
    LastObservedRealTime = CurrentWorld.RealTimeSeconds;
    LastRuntimeGuardMapName = CurrentMapName;
    GuardRuntimeActors(CurrentWorld);
    KFPC = KFPlayerController(PrimaryController);
    bGraphicsMenuOpen = IsGraphicsMenuOpen(KFPC);
    if (bGraphicsMenuOpen != bGraphicsMenuWasOpen)
    {
        // Capture the exact state at both menu boundaries before the normal
        // half-second reporting cadence can miss a quick Apply/Close action.
        NextReadRealTime = 0.0;
    }
    if (CurrentWorld.NetMode != NM_Standalone &&
        !bGraphicsMenuOpen && !bGraphicsMenuWasOpen)
    {
        ResetMenuGraphicsObservation();
        return;
    }
    if (CurrentWorld.RealTimeSeconds < NextReadRealTime)
    {
        return;
    }
    NextReadRealTime = CurrentWorld.RealTimeSeconds + 0.5;

    if ((CurrentMapName ~= "KFMainMenu") ||
        bGraphicsMenuOpen || bGraphicsMenuWasOpen)
    {
        Readback = ObserveMenuGraphicsAndRebase();
    }
    else
    {
        ResetMenuGraphicsObservation();
    }
    bGraphicsMenuWasOpen = bGraphicsMenuOpen;
    // Reuse the bounded native menu observation in every net mode. Include
    // the viewport sequence so old logs cannot invalidate a fresh receipt.
    if (Readback != "")
    {
        FrameRateViewport = KF2OptimizerGraphicsViewport(Outer);
        if (FrameRateViewport != None) Readback $= FrameRateViewport.FrameRateReadback();
        else Readback $= " frame_rate_sequence=0 frame_rate_limit=0";
        if (Readback != LastReadback)
        {
            LastReadback = Readback;
            `log("KF2OPT_GFX_MENU schema=3 state=applied " $ Readback);
        }
    }
    if (CurrentWorld.NetMode != NM_Standalone)
    {
        return;
    }

    if (CurrentMapName ~= "KFMainMenu")
    {
        EnsureMenuFrameRateListener(CurrentWorld, PrimaryController);
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
