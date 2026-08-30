// Live graphics actuator kept inside the KF2 graphics-options type hierarchy.
// This avoids exporting KF2's large native GFXSettings struct across classes.
class KF2OptimizerAdaptiveGraphics extends KFGFxOptionsMenu_Graphics;

static function ApplyGpu(out GFXSettings Requested, int Quality)
{
    local int ShadowResolution;
    local int ShadowFadeResolution;
    local int MinShadowResolution;
    local float ShadowDensity;
    local float ShadowDistance;
    local int MotionBlurQuality;
    local int DepthOfFieldQuality;
    local int DistanceFogQuality;

    if (Quality >= 100) return;
    if (Quality >= 90)
    {
        ShadowResolution = 4096;
        ShadowFadeResolution = 64;
        MinShadowResolution = 32;
        ShadowDensity = 2.25;
        ShadowDistance = 1.0;
        MotionBlurQuality = 1;
        DepthOfFieldQuality = 3;
        DistanceFogQuality = 1;
    }
    else if (Quality >= 80)
    {
        ShadowResolution = 2048;
        ShadowFadeResolution = 96;
        MinShadowResolution = 48;
        ShadowDensity = 2.0;
        ShadowDistance = 0.95;
        MotionBlurQuality = 1;
        DepthOfFieldQuality = 3;
        DistanceFogQuality = 1;
    }
    else if (Quality >= 70)
    {
        ShadowResolution = 2048;
        ShadowFadeResolution = 128;
        MinShadowResolution = 64;
        ShadowDensity = 1.75;
        ShadowDistance = 0.9;
        MotionBlurQuality = 1;
        DepthOfFieldQuality = 2;
        DistanceFogQuality = 1;
    }
    else if (Quality >= 60)
    {
        ShadowResolution = 1024;
        ShadowFadeResolution = 160;
        MinShadowResolution = 80;
        ShadowDensity = 1.5;
        ShadowDistance = 0.85;
        MotionBlurQuality = 0;
        DepthOfFieldQuality = 2;
        DistanceFogQuality = 1;
    }
    else if (Quality >= 40)
    {
        ShadowResolution = 1024;
        ShadowFadeResolution = 224;
        MinShadowResolution = 112;
        ShadowDensity = 1.25;
        ShadowDistance = 0.8;
        MotionBlurQuality = 0;
        DepthOfFieldQuality = 1;
        DistanceFogQuality = 0;
    }
    else
    {
        ShadowResolution = 512;
        ShadowFadeResolution = 256;
        MinShadowResolution = 128;
        ShadowDensity = 1.0;
        ShadowDistance = 0.75;
        MotionBlurQuality = 0;
        DepthOfFieldQuality = 0;
        DistanceFogQuality = 0;
    }
    Requested.Shadows.MaxWholeSceneDominantShadowResolution = Min(
        Requested.Shadows.MaxWholeSceneDominantShadowResolution,
        ShadowResolution);
    Requested.Shadows.MaxShadowResolution = Min(
        Requested.Shadows.MaxShadowResolution, ShadowResolution);
    Requested.Shadows.ShadowTexelsPerPixel = FMin(
        Requested.Shadows.ShadowTexelsPerPixel, ShadowDensity);
    Requested.Shadows.ShadowFadeResolution = Max(
        Requested.Shadows.ShadowFadeResolution, ShadowFadeResolution);
    Requested.Shadows.MinShadowResolution = Max(
        Requested.Shadows.MinShadowResolution, MinShadowResolution);
    Requested.Shadows.GlobalShadowDistanceScale = FMin(
        Requested.Shadows.GlobalShadowDistanceScale, ShadowDistance);
    Requested.Bloom.BloomQuality = Min(
        Requested.Bloom.BloomQuality,
        Quality >= 90 ? 5 : (Quality >= 70 ? 4 : 3));
    Requested.MotionBlur.MotionBlurQuality = Min(
        Requested.MotionBlur.MotionBlurQuality, MotionBlurQuality);
    Requested.DepthOfField.DepthOfFieldQuality = Min(
        Requested.DepthOfField.DepthOfFieldQuality, DepthOfFieldQuality);
    Requested.FX.DistanceFogQuality = Min(
        Requested.FX.DistanceFogQuality, DistanceFogQuality);
    if (Quality <= 80)
    {
        Requested.RealtimeReflections.bAllowScreenSpaceReflections = false;
    }
    if (Quality <= 70)
    {
        Requested.AmbientOcclusion.HBAO = false;
        Requested.CharacterDetail.AllowSubsurfaceScattering = false;
        Requested.FX.DropParticleDistortion = true;
    }
    if (Quality <= 60)
    {
        Requested.DepthOfField.DepthOfField = false;
        Requested.LightShafts.bAllowLightShafts = false;
        Requested.FX.FilteredDistortion = false;
    }
    if (Quality <= 50)
    {
        Requested.VolumetricLighting.bAllowLightCones = false;
        Requested.LensFlares.bAllowLensFlares = false;
        Requested.FX.AllowExplosionLights = false;
        Requested.FX.AllowSprayActorLights = false;
        Requested.FX.AllowPilotLights = false;
    }
    if (Quality <= 40)
    {
        Requested.FX.Distortion = false;
    }
    if (Quality <= 30)
    {
        Requested.AmbientOcclusion.AmbientOcclusion = false;
        Requested.Shadows.AllowForegroundPreshadows = false;
    }
    if (Quality <= 20)
    {
        Requested.Bloom.Bloom = false;
        Requested.Shadows.bAllowWholeSceneDominantShadows = false;
        Requested.Shadows.bAllowPerObjectShadows = false;
    }
    if (Quality <= 10)
    {
        Requested.Shadows.bAllowDynamicShadows = false;
    }
}

// Reduces only renderer work that can stack repeatedly on the same pixels.
// This is separate from generic GPU quality so an overdraw correction does
// not unnecessarily lower textures, shadows, or geometry detail.
static function ApplyOverdraw(out GFXSettings Requested, int Quality)
{
    local int ParticleLodBias;

    if (Quality >= 100) return;
    if (Quality >= 90) ParticleLodBias = 0;
    else if (Quality >= 70) ParticleLodBias = 1;
    else if (Quality >= 50) ParticleLodBias = 2;
    else ParticleLodBias = 3;

    Requested.FX.ParticleLODBias = Max(
        Requested.FX.ParticleLODBias, ParticleLodBias);
    Requested.FX.MaxImpactEffectDecals = Min(
        Requested.FX.MaxImpactEffectDecals, Max(8, Quality / 4));
    Requested.FX.MaxExplosionDecals = Min(
        Requested.FX.MaxExplosionDecals, Max(8, Quality / 6));
    Requested.FX.MaxBloodEffects = Min(
        Requested.FX.MaxBloodEffects, Max(12, Quality / 3));
    Requested.FX.MaxGoreEffects = Min(
        Requested.FX.MaxGoreEffects, Max(8, Quality / 8));
    Requested.FX.MaxPersistentSplatsPerFrame = Min(
        Requested.FX.MaxPersistentSplatsPerFrame, Max(25, Quality));
    Requested.CharacterDetail.MaxBodyWoundDecals = Min(
        Requested.CharacterDetail.MaxBodyWoundDecals,
        Max(2, Quality / 20));
    if (Quality <= 90)
    {
        Requested.FX.DropParticleDistortion = true;
    }
    if (Quality <= 70)
    {
        Requested.FX.FilteredDistortion = false;
    }
    if (Quality <= 50)
    {
        Requested.FX.AllowSecondaryBloodEffects = false;
    }
    if (Quality <= 40)
    {
        Requested.FX.Distortion = false;
    }
    if (Quality <= 30)
    {
        Requested.FX.AllowBloodSplatterDecals = false;
    }
}

static function ApplyCpu(out GFXSettings Requested, int Quality)
{
    local int DetailMode;
    local int LodBias;
    local float KinematicScale;

    if (Quality >= 100) return;
    if (Quality >= 90)
    {
        DetailMode = 2;
        LodBias = 0;
        KinematicScale = 1.1;
    }
    else if (Quality >= 80)
    {
        DetailMode = 2;
        LodBias = 1;
        KinematicScale = 1.2;
    }
    else if (Quality >= 70)
    {
        DetailMode = 2;
        LodBias = 1;
        KinematicScale = 1.3;
    }
    else if (Quality >= 60)
    {
        DetailMode = 1;
        LodBias = 1;
        KinematicScale = 1.5;
    }
    else if (Quality >= 50)
    {
        DetailMode = 1;
        LodBias = 2;
        KinematicScale = 1.75;
    }
    else if (Quality >= 40)
    {
        DetailMode = 1;
        LodBias = 2;
        KinematicScale = 2.0;
    }
    else if (Quality >= 30)
    {
        DetailMode = 0;
        LodBias = 2;
        KinematicScale = 2.3;
    }
    else
    {
        DetailMode = 0;
        LodBias = 3;
        KinematicScale = 3.0;
    }
    Requested.EnvironmentDetail.DetailMode = Min(
        Requested.EnvironmentDetail.DetailMode, DetailMode);
    Requested.CharacterDetail.SkeletalMeshLODBias = Max(
        Requested.CharacterDetail.SkeletalMeshLODBias, LodBias);
    Requested.CharacterDetail.KinematicUpdateDistFactorScale = FMax(
        Requested.CharacterDetail.KinematicUpdateDistFactorScale,
        KinematicScale);
    Requested.FX.ParticleLODBias = Max(
        Requested.FX.ParticleLODBias, LodBias);
    if (Quality <= 80)
    {
        Requested.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep = false;
    }
    if (Quality <= 60)
    {
        Requested.EnvironmentDetail.AllowLightFunctions = false;
        Requested.CharacterDetail.ShouldCorpseCollideWithDead = false;
    }
    if (Quality <= 40)
    {
        Requested.CharacterDetail.ShouldCorpseCollideWithLiving = false;
    }
}

static function ApplyVram(out GFXSettings Requested, int Quality)
{
    local int Bias;
    local int Anisotropy;

    if (Quality >= 100) return;
    if (Quality >= 90)
    {
        Bias = 0;
        Anisotropy = 8;
    }
    else if (Quality >= 80)
    {
        Bias = 1;
        Anisotropy = 8;
    }
    else if (Quality >= 60)
    {
        Bias = 1;
        Anisotropy = 4;
    }
    else if (Quality >= 40)
    {
        Bias = 2;
        Anisotropy = 4;
    }
    else if (Quality >= 20)
    {
        Bias = 2;
        Anisotropy = 2;
    }
    else
    {
        Bias = 3;
        Anisotropy = 2;
    }
    Requested.TextureResolution.CharacterBias = Max(
        Requested.TextureResolution.CharacterBias, Bias);
    Requested.TextureResolution.Weapon1stBias = Max(
        Requested.TextureResolution.Weapon1stBias, Bias);
    Requested.TextureResolution.Weapon3rdBias = Max(
        Requested.TextureResolution.Weapon3rdBias, Bias);
    Requested.TextureResolution.EnvironmentBias = Max(
        Requested.TextureResolution.EnvironmentBias, Bias);
    Requested.TextureResolution.FXBias = Max(
        Requested.TextureResolution.FXBias, Bias);
    Requested.TextureResolution.ShadowmapBias = Max(
        Requested.TextureResolution.ShadowmapBias, Bias);
    Requested.TextureFiltering.MaxAnisotropy = Min(
        Requested.TextureFiltering.MaxAnisotropy, Anisotropy);
}

static function ApplyRam(out GFXSettings Requested, int Quality)
{
    if (Quality >= 100) return;
    Requested.FX.EmitterPoolScale = FMin(
        Requested.FX.EmitterPoolScale,
        FMax(0.25, float(Quality) / 100.0));
    Requested.FX.ShellEjectLifetime = FMin(
        Requested.FX.ShellEjectLifetime,
        FMax(2.0, float(Quality) / 5.0));
    Requested.FX.GoreFXLifetimeMultiplier = FMin(
        Requested.FX.GoreFXLifetimeMultiplier,
        FMax(0.5, float(Quality) / 100.0));
    Requested.FX.MaxImpactEffectDecals = Min(
        Requested.FX.MaxImpactEffectDecals, Max(8, Quality / 4));
    Requested.FX.MaxExplosionDecals = Min(
        Requested.FX.MaxExplosionDecals, Max(8, Quality / 6));
    Requested.FX.MaxBloodEffects = Min(
        Requested.FX.MaxBloodEffects, Max(12, Quality / 3));
    Requested.FX.MaxGoreEffects = Min(
        Requested.FX.MaxGoreEffects, Max(8, Quality / 8));
    Requested.FX.MaxPersistentSplatsPerFrame = Min(
        Requested.FX.MaxPersistentSplatsPerFrame, Max(25, Quality));
    Requested.CharacterDetail.MaxBodyWoundDecals = Min(
        Requested.CharacterDetail.MaxBodyWoundDecals, Max(2, Quality / 20));
    Requested.EnvironmentDetail.DestructionLifetimeScale = FMin(
        Requested.EnvironmentDetail.DestructionLifetimeScale,
        FMax(0.25, float(Quality) / 100.0));
    if (Quality <= 50)
    {
        Requested.FX.AllowSecondaryBloodEffects = false;
    }
    if (Quality <= 30)
    {
        Requested.FX.AllowBloodSplatterDecals = false;
    }
}

static function CaptureOriginal(
    KF2OptimizerAdaptiveGraphicsState Snapshot, GFXSettings Current)
{
    Snapshot.OriginalMaxWholeSceneShadowResolution =
        Current.Shadows.MaxWholeSceneDominantShadowResolution;
    Snapshot.OriginalMaxShadowResolution = Current.Shadows.MaxShadowResolution;
    Snapshot.OriginalShadowFadeResolution =
        Current.Shadows.ShadowFadeResolution;
    Snapshot.OriginalMinShadowResolution =
        Current.Shadows.MinShadowResolution;
    Snapshot.OriginalShadowTexelsPerPixel = Current.Shadows.ShadowTexelsPerPixel;
    Snapshot.OriginalGlobalShadowDistanceScale =
        Current.Shadows.GlobalShadowDistanceScale;
    Snapshot.bOriginalWholeSceneDominantShadows =
        Current.Shadows.bAllowWholeSceneDominantShadows;
    Snapshot.bOriginalDynamicShadows = Current.Shadows.bAllowDynamicShadows;
    Snapshot.bOriginalPerObjectShadows =
        Current.Shadows.bAllowPerObjectShadows;
    Snapshot.bOriginalForegroundPreshadows =
        Current.Shadows.AllowForegroundPreshadows;
    Snapshot.OriginalBloomQuality = Current.Bloom.BloomQuality;
    Snapshot.OriginalMotionBlurQuality = Current.MotionBlur.MotionBlurQuality;
    Snapshot.OriginalDepthOfFieldQuality =
        Current.DepthOfField.DepthOfFieldQuality;
    Snapshot.OriginalDistanceFogQuality = Current.FX.DistanceFogQuality;
    Snapshot.bOriginalScreenSpaceReflections =
        Current.RealtimeReflections.bAllowScreenSpaceReflections;
    Snapshot.bOriginalHBAO = Current.AmbientOcclusion.HBAO;
    Snapshot.bOriginalDepthOfField = Current.DepthOfField.DepthOfField;
    Snapshot.bOriginalLightShafts = Current.LightShafts.bAllowLightShafts;
    Snapshot.bOriginalLightCones = Current.VolumetricLighting.bAllowLightCones;
    Snapshot.bOriginalLensFlares = Current.LensFlares.bAllowLensFlares;
    Snapshot.bOriginalAmbientOcclusion =
        Current.AmbientOcclusion.AmbientOcclusion;
    Snapshot.bOriginalBloom = Current.Bloom.Bloom;
    Snapshot.bOriginalDistortion = Current.FX.Distortion;
    Snapshot.bOriginalFilteredDistortion = Current.FX.FilteredDistortion;
    Snapshot.bOriginalDropParticleDistortion =
        Current.FX.DropParticleDistortion;
    Snapshot.bOriginalSecondaryBloodEffects =
        Current.FX.AllowSecondaryBloodEffects;
    Snapshot.bOriginalExplosionLights = Current.FX.AllowExplosionLights;
    Snapshot.bOriginalSprayActorLights = Current.FX.AllowSprayActorLights;
    Snapshot.bOriginalPilotLights = Current.FX.AllowPilotLights;
    Snapshot.bOriginalBloodSplatterDecals =
        Current.FX.AllowBloodSplatterDecals;
    Snapshot.bOriginalSubsurfaceScattering =
        Current.CharacterDetail.AllowSubsurfaceScattering;
    Snapshot.bOriginalCorpseCollideWithDead =
        Current.CharacterDetail.ShouldCorpseCollideWithDead;
    Snapshot.bOriginalCorpseCollideWithLiving =
        Current.CharacterDetail.ShouldCorpseCollideWithLiving;
    Snapshot.bOriginalCorpseCollideWithDeadAfterSleep =
        Current.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep;
    Snapshot.bOriginalLightFunctions =
        Current.EnvironmentDetail.AllowLightFunctions;
    Snapshot.OriginalDetailMode = Current.EnvironmentDetail.DetailMode;
    Snapshot.OriginalDestructionLifetimeScale =
        Current.EnvironmentDetail.DestructionLifetimeScale;
    Snapshot.OriginalSkeletalMeshLODBias =
        Current.CharacterDetail.SkeletalMeshLODBias;
    Snapshot.OriginalKinematicUpdateScale =
        Current.CharacterDetail.KinematicUpdateDistFactorScale;
    Snapshot.OriginalParticleLODBias = Current.FX.ParticleLODBias;
    Snapshot.OriginalCharacterTextureBias =
        Current.TextureResolution.CharacterBias;
    Snapshot.OriginalWeapon1stTextureBias =
        Current.TextureResolution.Weapon1stBias;
    Snapshot.OriginalWeapon3rdTextureBias =
        Current.TextureResolution.Weapon3rdBias;
    Snapshot.OriginalEnvironmentTextureBias =
        Current.TextureResolution.EnvironmentBias;
    Snapshot.OriginalFXTextureBias = Current.TextureResolution.FXBias;
    Snapshot.OriginalShadowmapTextureBias =
        Current.TextureResolution.ShadowmapBias;
    Snapshot.OriginalMaxAnisotropy = Current.TextureFiltering.MaxAnisotropy;
    Snapshot.OriginalEmitterPoolScale = Current.FX.EmitterPoolScale;
    Snapshot.OriginalShellEjectLifetime = Current.FX.ShellEjectLifetime;
    Snapshot.OriginalGoreLifetimeMultiplier =
        Current.FX.GoreFXLifetimeMultiplier;
    Snapshot.OriginalMaxImpactEffectDecals = Current.FX.MaxImpactEffectDecals;
    Snapshot.OriginalMaxExplosionDecals = Current.FX.MaxExplosionDecals;
    Snapshot.OriginalMaxBloodEffects = Current.FX.MaxBloodEffects;
    Snapshot.OriginalMaxGoreEffects = Current.FX.MaxGoreEffects;
    Snapshot.OriginalMaxPersistentSplatsPerFrame =
        Current.FX.MaxPersistentSplatsPerFrame;
    Snapshot.OriginalMaxBodyWoundDecals =
        Current.CharacterDetail.MaxBodyWoundDecals;
    Snapshot.GpuQuality = 100;
    Snapshot.CpuQuality = 100;
    Snapshot.VramQuality = 100;
    Snapshot.RamQuality = 100;
    Snapshot.OverdrawQuality = 100;
    Snapshot.bOriginalCaptured = true;
}

static function RestoreOwnedSettings(
    KF2OptimizerAdaptiveGraphicsState Snapshot, out GFXSettings Requested)
{
    Requested.Shadows.MaxWholeSceneDominantShadowResolution =
        Snapshot.OriginalMaxWholeSceneShadowResolution;
    Requested.Shadows.MaxShadowResolution = Snapshot.OriginalMaxShadowResolution;
    Requested.Shadows.ShadowFadeResolution =
        Snapshot.OriginalShadowFadeResolution;
    Requested.Shadows.MinShadowResolution =
        Snapshot.OriginalMinShadowResolution;
    Requested.Shadows.ShadowTexelsPerPixel =
        Snapshot.OriginalShadowTexelsPerPixel;
    Requested.Shadows.GlobalShadowDistanceScale =
        Snapshot.OriginalGlobalShadowDistanceScale;
    Requested.Shadows.bAllowWholeSceneDominantShadows =
        Snapshot.bOriginalWholeSceneDominantShadows;
    Requested.Shadows.bAllowDynamicShadows =
        Snapshot.bOriginalDynamicShadows;
    Requested.Shadows.bAllowPerObjectShadows =
        Snapshot.bOriginalPerObjectShadows;
    Requested.Shadows.AllowForegroundPreshadows =
        Snapshot.bOriginalForegroundPreshadows;
    Requested.Bloom.BloomQuality = Snapshot.OriginalBloomQuality;
    Requested.MotionBlur.MotionBlurQuality =
        Snapshot.OriginalMotionBlurQuality;
    Requested.DepthOfField.DepthOfFieldQuality =
        Snapshot.OriginalDepthOfFieldQuality;
    Requested.FX.DistanceFogQuality = Snapshot.OriginalDistanceFogQuality;
    Requested.RealtimeReflections.bAllowScreenSpaceReflections =
        Snapshot.bOriginalScreenSpaceReflections;
    Requested.AmbientOcclusion.HBAO = Snapshot.bOriginalHBAO;
    Requested.DepthOfField.DepthOfField = Snapshot.bOriginalDepthOfField;
    Requested.LightShafts.bAllowLightShafts = Snapshot.bOriginalLightShafts;
    Requested.VolumetricLighting.bAllowLightCones =
        Snapshot.bOriginalLightCones;
    Requested.LensFlares.bAllowLensFlares = Snapshot.bOriginalLensFlares;
    Requested.AmbientOcclusion.AmbientOcclusion =
        Snapshot.bOriginalAmbientOcclusion;
    Requested.Bloom.Bloom = Snapshot.bOriginalBloom;
    Requested.FX.Distortion = Snapshot.bOriginalDistortion;
    Requested.FX.FilteredDistortion = Snapshot.bOriginalFilteredDistortion;
    Requested.FX.DropParticleDistortion =
        Snapshot.bOriginalDropParticleDistortion;
    Requested.FX.AllowSecondaryBloodEffects =
        Snapshot.bOriginalSecondaryBloodEffects;
    Requested.FX.AllowExplosionLights = Snapshot.bOriginalExplosionLights;
    Requested.FX.AllowSprayActorLights = Snapshot.bOriginalSprayActorLights;
    Requested.FX.AllowPilotLights = Snapshot.bOriginalPilotLights;
    Requested.FX.AllowBloodSplatterDecals =
        Snapshot.bOriginalBloodSplatterDecals;
    Requested.CharacterDetail.AllowSubsurfaceScattering =
        Snapshot.bOriginalSubsurfaceScattering;
    Requested.CharacterDetail.ShouldCorpseCollideWithDead =
        Snapshot.bOriginalCorpseCollideWithDead;
    Requested.CharacterDetail.ShouldCorpseCollideWithLiving =
        Snapshot.bOriginalCorpseCollideWithLiving;
    Requested.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep =
        Snapshot.bOriginalCorpseCollideWithDeadAfterSleep;
    Requested.EnvironmentDetail.AllowLightFunctions =
        Snapshot.bOriginalLightFunctions;
    Requested.EnvironmentDetail.DetailMode = Snapshot.OriginalDetailMode;
    Requested.EnvironmentDetail.DestructionLifetimeScale =
        Snapshot.OriginalDestructionLifetimeScale;
    Requested.CharacterDetail.SkeletalMeshLODBias =
        Snapshot.OriginalSkeletalMeshLODBias;
    Requested.CharacterDetail.KinematicUpdateDistFactorScale =
        Snapshot.OriginalKinematicUpdateScale;
    Requested.FX.ParticleLODBias = Snapshot.OriginalParticleLODBias;
    Requested.TextureResolution.CharacterBias =
        Snapshot.OriginalCharacterTextureBias;
    Requested.TextureResolution.Weapon1stBias =
        Snapshot.OriginalWeapon1stTextureBias;
    Requested.TextureResolution.Weapon3rdBias =
        Snapshot.OriginalWeapon3rdTextureBias;
    Requested.TextureResolution.EnvironmentBias =
        Snapshot.OriginalEnvironmentTextureBias;
    Requested.TextureResolution.FXBias = Snapshot.OriginalFXTextureBias;
    Requested.TextureResolution.ShadowmapBias =
        Snapshot.OriginalShadowmapTextureBias;
    Requested.TextureFiltering.MaxAnisotropy = Snapshot.OriginalMaxAnisotropy;
    Requested.FX.EmitterPoolScale = Snapshot.OriginalEmitterPoolScale;
    Requested.FX.ShellEjectLifetime = Snapshot.OriginalShellEjectLifetime;
    Requested.FX.GoreFXLifetimeMultiplier =
        Snapshot.OriginalGoreLifetimeMultiplier;
    Requested.FX.MaxImpactEffectDecals = Snapshot.OriginalMaxImpactEffectDecals;
    Requested.FX.MaxExplosionDecals = Snapshot.OriginalMaxExplosionDecals;
    Requested.FX.MaxBloodEffects = Snapshot.OriginalMaxBloodEffects;
    Requested.FX.MaxGoreEffects = Snapshot.OriginalMaxGoreEffects;
    Requested.FX.MaxPersistentSplatsPerFrame =
        Snapshot.OriginalMaxPersistentSplatsPerFrame;
    Requested.CharacterDetail.MaxBodyWoundDecals =
        Snapshot.OriginalMaxBodyWoundDecals;
}

static function bool ReadbackMatches(
    GFXSettings Observed, GFXSettings Requested)
{
    return Observed.Shadows.MaxWholeSceneDominantShadowResolution ==
               Requested.Shadows.MaxWholeSceneDominantShadowResolution &&
           Observed.Shadows.MaxShadowResolution ==
               Requested.Shadows.MaxShadowResolution &&
           Observed.Shadows.ShadowFadeResolution ==
               Requested.Shadows.ShadowFadeResolution &&
           Observed.Shadows.MinShadowResolution ==
               Requested.Shadows.MinShadowResolution &&
           Abs(Observed.Shadows.ShadowTexelsPerPixel -
               Requested.Shadows.ShadowTexelsPerPixel) < 0.001 &&
           Abs(Observed.Shadows.GlobalShadowDistanceScale -
               Requested.Shadows.GlobalShadowDistanceScale) < 0.001 &&
           Observed.Shadows.bAllowWholeSceneDominantShadows ==
               Requested.Shadows.bAllowWholeSceneDominantShadows &&
           Observed.Shadows.bAllowDynamicShadows ==
               Requested.Shadows.bAllowDynamicShadows &&
           Observed.Shadows.bAllowPerObjectShadows ==
               Requested.Shadows.bAllowPerObjectShadows &&
           Observed.Shadows.AllowForegroundPreshadows ==
               Requested.Shadows.AllowForegroundPreshadows &&
           Observed.Bloom.BloomQuality == Requested.Bloom.BloomQuality &&
           Observed.MotionBlur.MotionBlurQuality ==
               Requested.MotionBlur.MotionBlurQuality &&
           Observed.DepthOfField.DepthOfFieldQuality ==
               Requested.DepthOfField.DepthOfFieldQuality &&
           Observed.FX.DistanceFogQuality == Requested.FX.DistanceFogQuality &&
           Observed.RealtimeReflections.bAllowScreenSpaceReflections ==
               Requested.RealtimeReflections.bAllowScreenSpaceReflections &&
           Observed.AmbientOcclusion.HBAO == Requested.AmbientOcclusion.HBAO &&
           Observed.DepthOfField.DepthOfField ==
               Requested.DepthOfField.DepthOfField &&
           Observed.LightShafts.bAllowLightShafts ==
               Requested.LightShafts.bAllowLightShafts &&
           Observed.VolumetricLighting.bAllowLightCones ==
               Requested.VolumetricLighting.bAllowLightCones &&
           Observed.LensFlares.bAllowLensFlares ==
               Requested.LensFlares.bAllowLensFlares &&
           Observed.AmbientOcclusion.AmbientOcclusion ==
               Requested.AmbientOcclusion.AmbientOcclusion &&
           Observed.Bloom.Bloom == Requested.Bloom.Bloom &&
           Observed.FX.Distortion == Requested.FX.Distortion &&
           Observed.FX.FilteredDistortion == Requested.FX.FilteredDistortion &&
           Observed.FX.DropParticleDistortion ==
               Requested.FX.DropParticleDistortion &&
           Observed.FX.AllowSecondaryBloodEffects ==
               Requested.FX.AllowSecondaryBloodEffects &&
           Observed.FX.AllowExplosionLights ==
               Requested.FX.AllowExplosionLights &&
           Observed.FX.AllowSprayActorLights ==
               Requested.FX.AllowSprayActorLights &&
           Observed.FX.AllowPilotLights == Requested.FX.AllowPilotLights &&
           Observed.FX.AllowBloodSplatterDecals ==
               Requested.FX.AllowBloodSplatterDecals &&
           Observed.CharacterDetail.AllowSubsurfaceScattering ==
               Requested.CharacterDetail.AllowSubsurfaceScattering &&
           Observed.CharacterDetail.ShouldCorpseCollideWithDead ==
               Requested.CharacterDetail.ShouldCorpseCollideWithDead &&
           Observed.CharacterDetail.ShouldCorpseCollideWithLiving ==
               Requested.CharacterDetail.ShouldCorpseCollideWithLiving &&
           Observed.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep ==
               Requested.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep &&
           Observed.EnvironmentDetail.AllowLightFunctions ==
               Requested.EnvironmentDetail.AllowLightFunctions &&
           Observed.EnvironmentDetail.DetailMode ==
               Requested.EnvironmentDetail.DetailMode &&
           Abs(Observed.EnvironmentDetail.DestructionLifetimeScale -
               Requested.EnvironmentDetail.DestructionLifetimeScale) <
               0.001 &&
           Observed.CharacterDetail.SkeletalMeshLODBias ==
               Requested.CharacterDetail.SkeletalMeshLODBias &&
           Abs(Observed.CharacterDetail.KinematicUpdateDistFactorScale -
               Requested.CharacterDetail.KinematicUpdateDistFactorScale) <
               0.001 &&
           Observed.FX.ParticleLODBias == Requested.FX.ParticleLODBias &&
           Observed.TextureResolution.CharacterBias ==
               Requested.TextureResolution.CharacterBias &&
           Observed.TextureResolution.Weapon1stBias ==
               Requested.TextureResolution.Weapon1stBias &&
           Observed.TextureResolution.Weapon3rdBias ==
               Requested.TextureResolution.Weapon3rdBias &&
           Observed.TextureResolution.EnvironmentBias ==
               Requested.TextureResolution.EnvironmentBias &&
           Observed.TextureResolution.FXBias ==
               Requested.TextureResolution.FXBias &&
           Observed.TextureResolution.ShadowmapBias ==
               Requested.TextureResolution.ShadowmapBias &&
           Observed.TextureFiltering.MaxAnisotropy ==
               Requested.TextureFiltering.MaxAnisotropy &&
           Abs(Observed.FX.EmitterPoolScale -
               Requested.FX.EmitterPoolScale) < 0.001 &&
           Abs(Observed.FX.ShellEjectLifetime -
               Requested.FX.ShellEjectLifetime) < 0.001 &&
           Abs(Observed.FX.GoreFXLifetimeMultiplier -
               Requested.FX.GoreFXLifetimeMultiplier) < 0.001 &&
           Observed.FX.MaxImpactEffectDecals ==
               Requested.FX.MaxImpactEffectDecals &&
           Observed.FX.MaxExplosionDecals ==
               Requested.FX.MaxExplosionDecals &&
           Observed.FX.MaxBloodEffects == Requested.FX.MaxBloodEffects &&
           Observed.FX.MaxGoreEffects == Requested.FX.MaxGoreEffects &&
           Observed.FX.MaxPersistentSplatsPerFrame ==
               Requested.FX.MaxPersistentSplatsPerFrame &&
           Observed.CharacterDetail.MaxBodyWoundDecals ==
               Requested.CharacterDetail.MaxBodyWoundDecals;
}

static function bool ApplyResource(
    KF2OptimizerAdaptiveGraphicsState Snapshot, string Resource, int Quality)
{
    local GFXSettings Current;
    local GFXSettings Requested;
    local GFXSettings Observed;
    local int PreviousGpuQuality;
    local int PreviousCpuQuality;
    local int PreviousVramQuality;
    local int PreviousRamQuality;
    local int PreviousOverdrawQuality;

    if (Snapshot == None || Quality < 10 || Quality > 100) return false;
    GetCurrentGFXSettings(Current);
    if (!Snapshot.bOriginalCaptured) CaptureOriginal(Snapshot, Current);
    PreviousGpuQuality = Snapshot.GpuQuality;
    PreviousCpuQuality = Snapshot.CpuQuality;
    PreviousVramQuality = Snapshot.VramQuality;
    PreviousRamQuality = Snapshot.RamQuality;
    PreviousOverdrawQuality = Snapshot.OverdrawQuality;

    if (Resource ~= "recover")
    {
        // Recovery must never lower a resource group that was not degraded.
        Snapshot.GpuQuality = Max(Snapshot.GpuQuality, Quality);
        Snapshot.CpuQuality = Max(Snapshot.CpuQuality, Quality);
        Snapshot.VramQuality = Max(Snapshot.VramQuality, Quality);
        Snapshot.RamQuality = Max(Snapshot.RamQuality, Quality);
        Snapshot.OverdrawQuality = Max(
            Snapshot.OverdrawQuality, Quality);
    }
    else if (Resource ~= "gpu") Snapshot.GpuQuality = Quality;
    else if (Resource ~= "cpu") Snapshot.CpuQuality = Quality;
    else if (Resource ~= "vram") Snapshot.VramQuality = Quality;
    else if (Resource ~= "ram") Snapshot.RamQuality = Quality;
    else if (Resource ~= "overdraw") Snapshot.OverdrawQuality = Quality;
    else if (Resource ~= "mixed")
    {
        Snapshot.GpuQuality = Quality;
        Snapshot.CpuQuality = Quality;
        Snapshot.VramQuality = Quality;
        Snapshot.RamQuality = Quality;
        Snapshot.OverdrawQuality = Quality;
    }
    else return false;

    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
    ApplyOverdraw(Requested, Snapshot.OverdrawQuality);
    SetNativeSettings(Requested);
    SetScriptSettings(Requested);
    GetCurrentGFXSettings(Observed);
    if (ReadbackMatches(Observed, Requested)) return true;

    Snapshot.GpuQuality = PreviousGpuQuality;
    Snapshot.CpuQuality = PreviousCpuQuality;
    Snapshot.VramQuality = PreviousVramQuality;
    Snapshot.RamQuality = PreviousRamQuality;
    Snapshot.OverdrawQuality = PreviousOverdrawQuality;
    Requested = Observed;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
    ApplyOverdraw(Requested, Snapshot.OverdrawQuality);
    SetNativeSettings(Requested);
    SetScriptSettings(Requested);
    GetCurrentGFXSettings(Observed);
    if (ReadbackMatches(Observed, Requested))
    {
        `log("KF2OPT_ADAPTIVE_ROLLBACK state=applied reason=readback_mismatch");
    }
    else
    {
        `log("KF2OPT_ADAPTIVE_ROLLBACK state=failed reason=readback_mismatch");
    }
    return false;
}

static function bool RestoreOriginal(KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    local GFXSettings Current;
    local GFXSettings Requested;
    local GFXSettings Observed;

    if (Snapshot == None || !Snapshot.bOriginalCaptured) return true;
    GetCurrentGFXSettings(Current);
    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    SetNativeSettings(Requested);
    SetScriptSettings(Requested);
    GetCurrentGFXSettings(Observed);
    if (!ReadbackMatches(Observed, Requested)) return false;
    Snapshot.GpuQuality = 100;
    Snapshot.CpuQuality = 100;
    Snapshot.VramQuality = 100;
    Snapshot.RamQuality = 100;
    Snapshot.OverdrawQuality = 100;
    Snapshot.bOriginalCaptured = false;
    return true;
}

defaultproperties
{
}
