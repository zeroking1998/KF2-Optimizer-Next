// Per-session snapshot for the live adaptive graphics actuator.
// It is owned by the telemetry probe and never persisted to the user's INIs.
class KF2OptimizerAdaptiveGraphicsState extends Object;

var bool bOriginalCaptured;
var int GpuLevel;
var int CpuLevel;
var int VramLevel;
var int RamLevel;
var int OriginalMaxWholeSceneShadowResolution;
var int OriginalMaxShadowResolution;
var float OriginalShadowTexelsPerPixel;
var int OriginalBloomQuality;
var bool bOriginalScreenSpaceReflections;
var bool bOriginalHBAO;
var bool bOriginalDepthOfField;
var bool bOriginalLightShafts;
var bool bOriginalLightCones;
var bool bOriginalLensFlares;
var bool bOriginalAmbientOcclusion;
var bool bOriginalBloom;
var int OriginalDetailMode;
var int OriginalSkeletalMeshLODBias;
var float OriginalKinematicUpdateScale;
var int OriginalParticleLODBias;
var int OriginalCharacterTextureBias;
var int OriginalWeapon1stTextureBias;
var int OriginalWeapon3rdTextureBias;
var int OriginalEnvironmentTextureBias;
var int OriginalFXTextureBias;
var int OriginalMaxAnisotropy;
var float OriginalEmitterPoolScale;
var float OriginalShellEjectLifetime;
var float OriginalGoreLifetimeMultiplier;
var int OriginalMaxImpactEffectDecals;
var int OriginalMaxExplosionDecals;
var int OriginalMaxBloodEffects;
var int OriginalMaxGoreEffects;
var int OriginalMaxPersistentSplatsPerFrame;

defaultproperties
{
    GpuLevel=3
    CpuLevel=3
    VramLevel=3
    RamLevel=3
}
