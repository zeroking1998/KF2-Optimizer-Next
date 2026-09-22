// Neutral client-only class used while a pawn's real replicated weapon class
// is still unavailable. It is never spawned and never owns an attachment.
class KF2OptimizerWeaponFallback extends KFWeapon
    abstract;

static simulated function class<KFProjectile> GetKFProjectileClassByFiringMode(
    int FiringMode, KFPerk Perk)
{
    return None;
}
