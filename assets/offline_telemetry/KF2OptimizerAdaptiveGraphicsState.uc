// Per-session snapshot for the live adaptive graphics actuator.
// It is owned by the telemetry probe and never persisted to the user's INIs.
class KF2OptimizerAdaptiveGraphicsState extends Object;

var bool bOriginalCaptured;
var int GpuQuality;
var int CpuQuality;
var int VramQuality;
var int RamQuality;
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
}
