// Process-session snapshot for the live adaptive graphics actuator. The
// persistent viewport interaction owns it and shares it with each map probe;
// it is never persisted to the user's INIs.
class KF2OptimizerAdaptiveGraphicsState extends Object;

var bool bOriginalCaptured;
// Original restoration is distinct from rollback to a previous composition.
var bool bOriginalRestorePending;
var int GpuQuality;
var int CpuQuality;
var int VramQuality;
var int RamQuality;
var int OverdrawQuality;
var int EffectsQuality;
var int FixedOverdrawQuality;
var int FixedEffectsQuality;
// A failed verified rollback must survive map-probe replacement. The
// persistent viewport interaction owns this object for the whole process.
var bool bQualityStateKnown;
var bool bQualityRestorePending;
var int RestoreGpuQuality;
var int RestoreCpuQuality;
var int RestoreVramQuality;
var int RestoreRamQuality;
var int RestoreOverdrawQuality;
var int RestoreEffectsQuality;
var int OriginalMaxWholeSceneShadowResolution;
var int OriginalMaxShadowResolution;
var int OriginalShadowFadeResolution;
var int OriginalMinShadowResolution;
var float OriginalShadowTexelsPerPixel;
var float OriginalGlobalShadowDistanceScale;
var bool bOriginalWholeSceneDominantShadows;
var bool bOriginalDynamicShadows;
var bool bOriginalPerObjectShadows;
var bool bOriginalForegroundPreshadows;
var int OriginalBloomQuality;
var int OriginalMotionBlurQuality;
var int OriginalDepthOfFieldQuality;
var int OriginalDistanceFogQuality;
var bool bOriginalScreenSpaceReflections;
var bool bOriginalHBAO;
var bool bOriginalDepthOfField;
var bool bOriginalLightShafts;
var bool bOriginalLightCones;
var bool bOriginalLensFlares;
var bool bOriginalAmbientOcclusion;
var bool bOriginalBloom;
var bool bOriginalLightFunctions;
var bool bOriginalDistortion;
var bool bOriginalFilteredDistortion;
var bool bOriginalDropParticleDistortion;
var bool bOriginalSecondaryBloodEffects;
var bool bOriginalExplosionLights;
var bool bOriginalSprayActorLights;
var bool bOriginalPilotLights;
var bool bOriginalSubsurfaceScattering;
var bool bOriginalCorpseCollideWithDead;
var bool bOriginalCorpseCollideWithLiving;
var bool bOriginalCorpseCollideWithDeadAfterSleep;
var bool bOriginalBloodSplatterDecals;
var int OriginalDetailMode;
var float OriginalDestructionLifetimeScale;
var int OriginalSkeletalMeshLODBias;
var float OriginalKinematicUpdateScale;
var int OriginalParticleLODBias;
var int OriginalCharacterTextureBias;
var int OriginalWeapon1stTextureBias;
var int OriginalWeapon3rdTextureBias;
var int OriginalEnvironmentTextureBias;
var int OriginalFXTextureBias;
var int OriginalShadowmapTextureBias;
var int OriginalMaxAnisotropy;
var float OriginalEmitterPoolScale;
var float OriginalShellEjectLifetime;
var float OriginalGoreLifetimeMultiplier;
var int OriginalMaxImpactEffectDecals;
var int OriginalMaxExplosionDecals;
var int OriginalMaxBloodEffects;
var int OriginalMaxGoreEffects;
var int OriginalMaxPersistentSplatsPerFrame;
var int OriginalMaxBodyWoundDecals;

defaultproperties
{
    GpuQuality=100
    CpuQuality=100
    VramQuality=100
    RamQuality=100
    OverdrawQuality=100
    EffectsQuality=100
    FixedOverdrawQuality=100
    FixedEffectsQuality=100
    bQualityStateKnown=true
}
