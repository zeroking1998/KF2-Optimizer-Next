// World-owned controller for bounded, client-local corpse physics reductions.
// It is destroyed with the current World, so no corpse reference can survive
// server travel. It never performs a replicated write.
class KF2OptimizerOnlineCorpseController extends Actor;

struct OnlineFrozenCorpseState
{
    var KFPawn Corpse;
    var bool bOriginalTickDisabled;
    var bool bOriginalCollideActors;
    var bool bOriginalBlockActors;
    var bool bOriginalIgnoreEncroachers;
    var bool bHadCollisionComponent;
    var bool bOriginalBlockRigidBody;
};

var array<OnlineFrozenCorpseState> FrozenCorpses;
var int FreezeScanCursor;
var float LastPhysicsMutationRealTime;
var bool bFreezeReceiptReported;
var bool bRestoreReceiptReported;

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

function bool RestoreOneOnlineCorpse()
{
    local int Index;
    local KFPawn Candidate;
    local OnlineFrozenCorpseState Original;

    for (Index = FrozenCorpses.Length - 1; Index >= 0; --Index)
    {
        Candidate = FrozenCorpses[Index].Corpse;
        if (Candidate == None || Candidate.bDeleteMe)
        {
            FrozenCorpses.Remove(Index, 1);
            continue;
        }
        Original = FrozenCorpses[Index];
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
        if (Candidate.Physics != PHYS_RigidBody ||
            Candidate.bCollideActors != Original.bOriginalCollideActors ||
            Candidate.bBlockActors != Original.bOriginalBlockActors ||
            Candidate.bTickIsDisabled != Original.bOriginalTickDisabled ||
            (Original.bHadCollisionComponent &&
             Candidate.CollisionComponent != None &&
             Candidate.CollisionComponent.BlockRigidBody !=
                 Original.bOriginalBlockRigidBody))
        {
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=failed action=restore"$
                 " corpse_id="$string(Candidate.Name)$
                 " reason=readback_mismatch local_only=true");
            return false;
        }
        FrozenCorpses.Remove(Index, 1);
        LastPhysicsMutationRealTime = WorldInfo.RealTimeSeconds;
        if (!bRestoreReceiptReported)
        {
            bRestoreReceiptReported = true;
            `log("KF2OPT_ONLINE_CORPSE_ACTION state=restored corpse_id="$
                 string(Candidate.Name)$
                 " physics=PHYS_RigidBody collision=original"$
                 " tick=original local_only=true readback=verified");
        }
        return true;
    }
    return false;
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

event Tick(float DeltaTime)
{
    local KF2OptimizerOnlineContextInteraction CurrentInteraction;

    Super.Tick(DeltaTime);
    CurrentInteraction = GetOnlineInteraction();
    if (CurrentInteraction != None &&
        CurrentInteraction.IsOnlineAdaptiveEnabled())
    {
        bRestoreReceiptReported = false;
        FreezeOneOnlineCorpse();
    }
    else if (WorldInfo != None &&
             WorldInfo.RealTimeSeconds - LastPhysicsMutationRealTime >= 0.45)
    {
        RestoreOneOnlineCorpse();
    }
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    RemoteRole=ROLE_None
}
