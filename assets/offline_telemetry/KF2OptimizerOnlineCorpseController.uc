// World-owned controller for bounded, client-local corpse physics reductions.
// It is destroyed with the current World, so no corpse reference can survive
// server travel. It never performs a replicated write.
class KF2OptimizerOnlineCorpseController extends Actor;

struct OnlineFrozenCorpseState
{
    var KFPawn Corpse;
    var string CorpseId;
    var bool bOriginalTickDisabled;
    var bool bOriginalCollideActors;
    var bool bOriginalBlockActors;
    var bool bOriginalIgnoreEncroachers;
    var bool bHadCollisionComponent;
    var bool bOriginalBlockRigidBody;
};

var array<OnlineFrozenCorpseState> FrozenCorpses;
var int FreezeScanCursor;
var int ReleaseScanCursor;
var int VisualScanCursor;
var int VisualControlPhase;
var float LastPhysicsMutationRealTime;
var float LastVisualMutationRealTime;
var bool bFreezeReceiptReported;
var bool bRestoreReceiptReported;
var bool bLodReceiptReported;
var bool bSkeletonReceiptReported;
var float LastReleaseFailureRealTime;

function KF2OptimizerOnlineContextInteraction GetOnlineInteraction()
{
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local string InteractionPath;

    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None || CurrentEngine.GameViewport == None)
    {
        return None;
    }
    CurrentViewport = CurrentEngine.GameViewport;
    InteractionPath = PathName(CurrentViewport)$
        ".KF2OptimizerOnlineContextInteraction";
    return KF2OptimizerOnlineContextInteraction(
        FindObject(InteractionPath,
            class'KF2OptimizerOnlineContextInteraction'));
}

function int FindFrozenCorpse(KFPawn Candidate)
{
    local int Index;

    for (Index = 0; Index < FrozenCorpses.Length; ++Index)
    {
        if (FrozenCorpses[Index].Corpse == Candidate)
        {
            return Index;
        }
    }
    return -1;
}

function string GetOnlineCorpseId(KFPawn Candidate)
{
    if (Candidate == None)
    {
        return "none";
    }
    return string(Candidate.Name)$":"$
        int(Candidate.TimeOfDeath * 1000.0);
}

function bool IsOnlineCorpseInPool(
    KFPawn Candidate, KFGoreManager GoreManager)
{
    return Candidate != None && GoreManager != None &&
        GoreManager.CorpsePool.Find(Candidate) >= 0;
}

function bool IsOnlineCorpseRecycledStateSafe(KFPawn Candidate)
{
    if (Candidate == None || Candidate.bDeleteMe)
    {
        return false;
    }
    return Candidate.Physics != PHYS_None &&
        !Candidate.bTickIsDisabled &&
        (Candidate.bCollideActors || Candidate.bBlockActors ||
         (Candidate.CollisionComponent != None &&
          Candidate.CollisionComponent.BlockRigidBody));
}

function LogOnlineCorpseReleaseFailure(string CorpseId, string Reason)
{
    if (WorldInfo != None &&
        WorldInfo.RealTimeSeconds - LastReleaseFailureRealTime < 1.0)
    {
        return;
    }
    if (WorldInfo != None)
    {
        LastReleaseFailureRealTime = WorldInfo.RealTimeSeconds;
    }
    `log("KF2OPT_ONLINE_CORPSE_ACTION state=release_failed corpse_id="$
         CorpseId$" reason="$Reason$
         " ownership=retained local_only=true");
}

function bool TryRestoreOnlineCorpse(int Index, string Reason)
{
    local KFPawn Candidate;
    local OnlineFrozenCorpseState Original;

    if (Index < 0 || Index >= FrozenCorpses.Length)
    {
        return false;
    }
    Original = FrozenCorpses[Index];
    Candidate = Original.Corpse;
    if (Candidate == None || Candidate.bDeleteMe ||
        GetOnlineCorpseId(Candidate) != Original.CorpseId)
    {
        LogOnlineCorpseReleaseFailure(
            Original.CorpseId, "identity_not_owned");
        return false;
    }
    Candidate.SetPhysics(PHYS_RigidBody);
    Candidate.SetCollision(
        Original.bOriginalCollideActors,
        Original.bOriginalBlockActors,
        Original.bOriginalIgnoreEncroachers);
    if (Original.bHadCollisionComponent)
    {
        if (Candidate.CollisionComponent == None)
        {
            LogOnlineCorpseReleaseFailure(
                Original.CorpseId, "collision_component_missing");
            return false;
        }
        Candidate.CollisionComponent.SetBlockRigidBody(
            Original.bOriginalBlockRigidBody);
    }
    Candidate.SetTickIsDisabled(Original.bOriginalTickDisabled);
    if (Candidate.Physics != PHYS_RigidBody ||
        Candidate.bCollideActors != Original.bOriginalCollideActors ||
        Candidate.bBlockActors != Original.bOriginalBlockActors ||
        Candidate.bIgnoreEncroachers != Original.bOriginalIgnoreEncroachers ||
        Candidate.bTickIsDisabled != Original.bOriginalTickDisabled ||
        (Original.bHadCollisionComponent &&
         Candidate.CollisionComponent.BlockRigidBody !=
             Original.bOriginalBlockRigidBody))
    {
        LogOnlineCorpseReleaseFailure(
            Original.CorpseId, "restore_readback_mismatch");
        return false;
    }
    LastPhysicsMutationRealTime = WorldInfo.RealTimeSeconds;
    if (!bRestoreReceiptReported)
    {
        bRestoreReceiptReported = true;
        `log("KF2OPT_ONLINE_CORPSE_ACTION state=restored corpse_id="$
             Original.CorpseId$" reason="$Reason$
             " physics=PHYS_RigidBody collision=original"$
             " tick=original local_only=true readback=verified");
    }
    return true;
}

function bool ReleaseOneOnlineCorpse(bool bRestoreAll)
{
    local bool bRemoved;
    local int Index;
    local int Scanned;
    local string CurrentId;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;

    if (WorldInfo == None ||
        WorldInfo.RealTimeSeconds - LastPhysicsMutationRealTime < 0.45)
    {
        return false;
    }
    if (FrozenCorpses.Length <= 0)
    {
        ReleaseScanCursor = 0;
        return false;
    }
    if (!bRestoreAll)
    {
        GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
        if (GoreManager == None)
        {
            return false;
        }
    }
    ReleaseScanCursor = Clamp(
        ReleaseScanCursor, 0, FrozenCorpses.Length - 1);
    while (FrozenCorpses.Length > 0 && Scanned < 8)
    {
        Index = Clamp(ReleaseScanCursor, 0, FrozenCorpses.Length - 1);
        Candidate = FrozenCorpses[Index].Corpse;
        bRemoved = false;
        ++Scanned;
        if (Candidate == None || Candidate.bDeleteMe)
        {
            FrozenCorpses.Remove(Index, 1);
            bRemoved = true;
        }
        else
        {
            CurrentId = GetOnlineCorpseId(Candidate);
            if (CurrentId != FrozenCorpses[Index].CorpseId)
            {
                if (IsOnlineCorpseRecycledStateSafe(Candidate))
                {
                    `log("KF2OPT_ONLINE_CORPSE_ACTION"$
                         " state=ownership_released"$
                         " reason=reused_state_verified old_corpse_id="$
                         FrozenCorpses[Index].CorpseId$
                         " current_corpse_id="$CurrentId$
                         " local_only=true readback=verified");
                    FrozenCorpses.Remove(Index, 1);
                    bRemoved = true;
                }
                else
                {
                    LogOnlineCorpseReleaseFailure(
                        FrozenCorpses[Index].CorpseId,
                        "reused_state_unverified");
                }
            }
            else if ((bRestoreAll ||
                      !IsOnlineCorpseInPool(Candidate, GoreManager)) &&
                     TryRestoreOnlineCorpse(Index, bRestoreAll ?
                         "adaptive_disabled" : "removed_from_pool"))
            {
                FrozenCorpses.Remove(Index, 1);
                ReleaseScanCursor = FrozenCorpses.Length > 0 ?
                    Index % FrozenCorpses.Length : 0;
                return true;
            }
        }
        ReleaseScanCursor = FrozenCorpses.Length > 0 ?
            (bRemoved ? Index % FrozenCorpses.Length :
             (Index + 1) % FrozenCorpses.Length) : 0;
    }
    return false;
}

function bool RestoreOneOnlineCorpse()
{
    return ReleaseOneOnlineCorpse(true);
}

function bool PruneOneOnlineFrozenCorpse()
{
    return ReleaseOneOnlineCorpse(false);
}

function bool FreezeOneOnlineCorpse()
{
    local int Index;
    local int Offset;
    local int PoolLength;
    local int ScanCount;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;
    local PlayerController LocalPC;
    local OnlineFrozenCorpseState Original;

    if (WorldInfo == None ||
        WorldInfo.RealTimeSeconds - LastPhysicsMutationRealTime < 0.45)
    {
        return false;
    }
    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    LocalPC = GetALocalPlayerController();
    if (GoreManager == None || LocalPC == None || LocalPC.Pawn == None)
    {
        return false;
    }
    PoolLength = GoreManager.CorpsePool.Length;
    if (PoolLength <= 0)
    {
        FreezeScanCursor = 0;
        return false;
    }
    FreezeScanCursor = Clamp(FreezeScanCursor, 0, PoolLength - 1);
    ScanCount = Min(8, PoolLength);
    for (Offset = 0; Offset < ScanCount; ++Offset)
    {
        Index = (FreezeScanCursor + Offset) % PoolLength;
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.IsAliveAndWell() || Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 10.0 ||
            Candidate.SpecialMove == SM_DeathAnim ||
            Candidate.Physics != PHYS_RigidBody ||
            Candidate.Mesh.RigidBodyIsAwake() ||
            VSizeSq(Candidate.Location - LocalPC.Pawn.Location) < 640000.0 ||
            FindFrozenCorpse(Candidate) >= 0)
        {
            continue;
        }
        Original.Corpse = Candidate;
        Original.CorpseId = GetOnlineCorpseId(Candidate);
        Original.bOriginalTickDisabled = Candidate.bTickIsDisabled;
        Original.bOriginalCollideActors = Candidate.bCollideActors;
        Original.bOriginalBlockActors = Candidate.bBlockActors;
        Original.bOriginalIgnoreEncroachers = Candidate.bIgnoreEncroachers;
        Original.bHadCollisionComponent = Candidate.CollisionComponent != None;
        if (Candidate.CollisionComponent != None)
        {
            Original.bOriginalBlockRigidBody =
                Candidate.CollisionComponent.BlockRigidBody;
        }
        Candidate.SetCollision(false, false, Candidate.bIgnoreEncroachers);
        if (Candidate.CollisionComponent != None)
        {
            Candidate.CollisionComponent.SetBlockRigidBody(false);
        }
        Candidate.SetTickIsDisabled(true);
        if (Candidate.bCollideActors || Candidate.bBlockActors ||
            !Candidate.bTickIsDisabled ||
            (Candidate.CollisionComponent != None &&
             Candidate.CollisionComponent.BlockRigidBody))
        {
            Candidate.SetCollision(
                Original.bOriginalCollideActors,
                Original.bOriginalBlockActors,
                Original.bOriginalIgnoreEncroachers);
            if (Original.bHadCollisionComponent &&
                Candidate.CollisionComponent != None)
            {
                Candidate.CollisionComponent.SetBlockRigidBody(
                    Original.bOriginalBlockRigidBody);
            }
            Candidate.SetTickIsDisabled(Original.bOriginalTickDisabled);
            return false;
        }
        Candidate.SetPhysics(PHYS_None);
        if (Candidate.Physics != PHYS_None)
        {
            Candidate.SetPhysics(PHYS_RigidBody);
            Candidate.SetCollision(
                Original.bOriginalCollideActors,
                Original.bOriginalBlockActors,
                Original.bOriginalIgnoreEncroachers);
            if (Original.bHadCollisionComponent &&
                Candidate.CollisionComponent != None)
            {
                Candidate.CollisionComponent.SetBlockRigidBody(
                    Original.bOriginalBlockRigidBody);
            }
            Candidate.SetTickIsDisabled(Original.bOriginalTickDisabled);
            return false;
        }
        FrozenCorpses.AddItem(Original);
        LastPhysicsMutationRealTime = WorldInfo.RealTimeSeconds;
        if (!bFreezeReceiptReported)
        {
            bFreezeReceiptReported = true;
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=freeze corpse_id="$
                 string(Candidate.Name)$
                 " physics=PHYS_None collision=false"$
                 " rigid_body_block=false tick_disabled=true"$
                 " local_only=true readback=verified");
        }
        FreezeScanCursor = (Index + 1) % PoolLength;
        return true;
    }
    FreezeScanCursor = (FreezeScanCursor + ScanCount) % PoolLength;
    return false;
}

function bool ApplyOneFixedMinimumCorpseLod()
{
    local int Index;
    local int Offset;
    local int PoolLength;
    local int ScanCount;
    local int TargetMinLod;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;

    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return false;
    }
    PoolLength = GoreManager.CorpsePool.Length;
    if (PoolLength <= 0)
    {
        VisualScanCursor = 0;
        return false;
    }
    VisualScanCursor = Clamp(VisualScanCursor, 0, PoolLength - 1);
    ScanCount = Min(8, PoolLength);
    for (Offset = 0; Offset < ScanCount; ++Offset)
    {
        Index = (VisualScanCursor + Offset) % PoolLength;
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.Mesh.SkeletalMesh == None ||
            Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2 ||
            Candidate.Mesh.ForcedLodModel != 0 ||
            Candidate.IsAliveAndWell() || Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 0.75 ||
            Candidate.SpecialMove == SM_DeathAnim)
        {
            continue;
        }
        TargetMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1;
        if (Candidate.Mesh.MinLodModel >= TargetMinLod)
        {
            continue;
        }
        Candidate.Mesh.MinLodModel = TargetMinLod;
        if (Candidate.Mesh.MinLodModel != TargetMinLod)
        {
            return false;
        }
        LastVisualMutationRealTime = WorldInfo.RealTimeSeconds;
        VisualScanCursor = (Index + 1) % PoolLength;
        if (!bLodReceiptReported)
        {
            bLodReceiptReported = true;
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=lod corpse_id="$
                 string(Candidate.Name)$" target_lod="$TargetMinLod$
                 " fixed_minimum=true local_only=true readback=verified");
        }
        return true;
    }
    VisualScanCursor = (VisualScanCursor + ScanCount) % PoolLength;
    return false;
}

function bool ApplyOneSleepingCorpseSkeletonMinimum()
{
    local int Index;
    local int Offset;
    local int PoolLength;
    local int ScanCount;
    local KFGoreManager GoreManager;
    local KFPawn Candidate;

    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return false;
    }
    PoolLength = GoreManager.CorpsePool.Length;
    if (PoolLength <= 0)
    {
        VisualScanCursor = 0;
        return false;
    }
    VisualScanCursor = Clamp(VisualScanCursor, 0, PoolLength - 1);
    ScanCount = Min(8, PoolLength);
    for (Offset = 0; Offset < ScanCount; ++Offset)
    {
        Index = (VisualScanCursor + Offset) % PoolLength;
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.IsAliveAndWell() || Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 0.75 ||
            Candidate.SpecialMove == SM_DeathAnim ||
            (Candidate.Physics != PHYS_None &&
             (Candidate.Physics != PHYS_RigidBody ||
              Candidate.Mesh.RigidBodyIsAwake())) ||
            (Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep &&
             Candidate.Mesh.bNoSkeletonUpdate))
        {
            continue;
        }
        Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
        Candidate.Mesh.bNoSkeletonUpdate = true;
        if (!Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep ||
            !Candidate.Mesh.bNoSkeletonUpdate)
        {
            return false;
        }
        LastVisualMutationRealTime = WorldInfo.RealTimeSeconds;
        VisualScanCursor = (Index + 1) % PoolLength;
        if (!bSkeletonReceiptReported)
        {
            bSkeletonReceiptReported = true;
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=skeleton corpse_id="$
                 string(Candidate.Name)$
                 " skip_asleep=true no_skeleton_update=true"$
                 " fixed_minimum=true local_only=true readback=verified");
        }
        return true;
    }
    VisualScanCursor = (VisualScanCursor + ScanCount) % PoolLength;
    return false;
}

function bool RunOneFixedMinimumVisualAction()
{
    local bool bActionTaken;

    if (WorldInfo == None ||
        WorldInfo.RealTimeSeconds - LastVisualMutationRealTime < 0.20)
    {
        return false;
    }
    if (VisualControlPhase == 0)
    {
        bActionTaken = ApplyOneFixedMinimumCorpseLod();
    }
    else
    {
        bActionTaken = ApplyOneSleepingCorpseSkeletonMinimum();
    }
    VisualControlPhase = (VisualControlPhase + 1) % 2;
    return bActionTaken;
}

event Tick(float DeltaTime)
{
    local KF2OptimizerOnlineContextInteraction CurrentInteraction;

    Super.Tick(DeltaTime);
    if (RunOneFixedMinimumVisualAction())
    {
        return;
    }
    CurrentInteraction = GetOnlineInteraction();
    if (CurrentInteraction != None &&
        CurrentInteraction.IsOnlineAdaptiveEnabled())
    {
        bRestoreReceiptReported = false;
        if (PruneOneOnlineFrozenCorpse())
        {
            return;
        }
        FreezeOneOnlineCorpse();
    }
    else if (WorldInfo != None &&
             WorldInfo.RealTimeSeconds - LastPhysicsMutationRealTime >= 0.45)
    {
        RestoreOneOnlineCorpse();
    }
}

event Destroyed()
{
    if (FrozenCorpses.Length > 0)
    {
        `log("KF2OPT_ONLINE_CORPSE_ACTION state=ownership_released"$
             " reason=world_teardown count="$FrozenCorpses.Length$
             " safe_boundary=world_destroy local_only=true");
    }
    FrozenCorpses.Length = 0;
    Super.Destroyed();
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    LastReleaseFailureRealTime=-1.0
    RemoteRole=ROLE_None
}
