// Live graphics actuator kept inside the KF2 graphics-options type hierarchy.
// This avoids exporting KF2's large native GFXSettings struct across classes.
class KF2OptimizerAdaptiveGraphics extends KFGFxOptionsMenu_Graphics;

// A fixed session baseline keeps the most expensive transient effect work
// bounded without waiting for an attributed Adaptive pressure event.  It is
// intentionally independent from the Governor and is restored with the
// captured user graphics at the session boundary.
const FixedSessionEffectsQuality=50;

static function int GetFixedSessionEffectsQuality()
{
    return FixedSessionEffectsQuality;
}

// This is the wire vocabulary shared by both authenticated endpoints. Keep it
// aligned with AdaptiveResourceControl in the native client.
static function bool IsAdaptiveControlResource(string Resource)
{
    return (Resource ~= "cpu") || (Resource ~= "gpu") ||
        (Resource ~= "vram") || (Resource ~= "ram") ||
        (Resource ~= "overdraw") || (Resource ~= "effects") ||
        (Resource ~= "mixed") || (Resource ~= "recover") ||
        (Resource ~= "enable") || (Resource ~= "disable");
}

static function bool IsAdaptiveQualityResource(string Resource)
{
    return IsAdaptiveControlResource(Resource) &&
        !(Resource ~= "enable") && !(Resource ~= "disable");
}

// Joined servers expose only capabilities with independent client-local
// evidence. Live effect-manager parity for overdraw/effects remains tracked in
// issue #205, so those known protocol values receive an explicit capability
// rejection instead of being mistaken for malformed input.
static function bool IsOnlineAdaptiveQualityResource(string Resource)
{
    return (Resource ~= "cpu") || (Resource ~= "gpu") ||
        (Resource ~= "vram") || (Resource ~= "ram") ||
        (Resource ~= "mixed") || (Resource ~= "recover");
}

static function int GetEffectiveOverdrawQuality(
    KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    return Min(Snapshot.OverdrawQuality, Snapshot.FixedOverdrawQuality);
}

static function int GetEffectiveEffectsQuality(
    KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    return Min(Snapshot.EffectsQuality, Snapshot.FixedEffectsQuality);
}

static function ApplyGpu(out GFXSettings Requested, int Quality)
{
    local int ShadowResolution;
    local int ShadowFadeResolution;
    local int MinShadowResolution;
    local float ShadowDensity;
    local float ShadowDistance;
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

// Controls effect lifetime, pool size and spawn budgets independently from
// pixel overdraw. This remains transient, reversible and read back through the
// same native GFX settings contract as every other adaptive resource group.
static function ApplyEffects(out GFXSettings Requested, int Quality)
{
    local int ParticleLodBias;

    if (Quality >= 100) return;
    if (Quality >= 80) ParticleLodBias = 1;
    else if (Quality >= 60) ParticleLodBias = 2;
    else if (Quality >= 40) ParticleLodBias = 3;
    else if (Quality >= 20) ParticleLodBias = 4;
    else ParticleLodBias = 5;

    Requested.FX.ParticleLODBias = Max(
        Requested.FX.ParticleLODBias, ParticleLodBias);
    Requested.FX.EmitterPoolScale = FMin(
        Requested.FX.EmitterPoolScale,
        FMax(0.15, float(Quality) / 100.0));
    Requested.FX.ShellEjectLifetime = FMin(
        Requested.FX.ShellEjectLifetime,
        FMax(1.0, float(Quality) / 5.0));
    Requested.FX.GoreFXLifetimeMultiplier = FMin(
        Requested.FX.GoreFXLifetimeMultiplier,
        FMax(0.2, float(Quality) / 100.0));
    Requested.EnvironmentDetail.DestructionLifetimeScale = FMin(
        Requested.EnvironmentDetail.DestructionLifetimeScale,
        FMax(0.2, float(Quality) / 100.0));
    Requested.FX.MaxImpactEffectDecals = Min(
        Requested.FX.MaxImpactEffectDecals, Max(1, Quality / 10));
    Requested.FX.MaxExplosionDecals = Min(
        Requested.FX.MaxExplosionDecals, Max(1, Quality / 10));
    Requested.FX.MaxBloodEffects = Min(
        Requested.FX.MaxBloodEffects, Max(2, Quality / 5));
    Requested.FX.MaxGoreEffects = Min(
        Requested.FX.MaxGoreEffects, Max(2, Quality / 10));
    Requested.FX.MaxPersistentSplatsPerFrame = Min(
        Requested.FX.MaxPersistentSplatsPerFrame, Max(5, Quality / 2));
    Requested.CharacterDetail.MaxBodyWoundDecals = Min(
        Requested.CharacterDetail.MaxBodyWoundDecals,
        Max(0, Quality / 25));
    if (Quality <= 80)
    {
        Requested.FX.DropParticleDistortion = true;
    }
    if (Quality <= 60)
    {
        Requested.FX.FilteredDistortion = false;
        Requested.FX.AllowSecondaryBloodEffects = false;
    }
    if (Quality <= 40)
    {
        Requested.FX.Distortion = false;
        Requested.FX.AllowBloodSplatterDecals = false;
    }
    if (Quality <= 20)
    {
        Requested.FX.AllowExplosionLights = false;
        Requested.FX.AllowSprayActorLights = false;
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

static function CopyOwnedSettings(
    KF2OptimizerAdaptiveGraphicsState Snapshot, GFXSettings Current)
{
    if (Snapshot == None) return;
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
}

static function bool OwnedSettingsDiffer(
    KF2OptimizerAdaptiveGraphicsState Previous,
    KF2OptimizerAdaptiveGraphicsState Current)
{
    if (Previous == None || Current == None) return false;
    return Previous.OriginalMaxWholeSceneShadowResolution !=
               Current.OriginalMaxWholeSceneShadowResolution ||
           Previous.OriginalMaxShadowResolution !=
               Current.OriginalMaxShadowResolution ||
           Previous.OriginalShadowFadeResolution !=
               Current.OriginalShadowFadeResolution ||
           Previous.OriginalMinShadowResolution !=
               Current.OriginalMinShadowResolution ||
           Previous.OriginalShadowTexelsPerPixel !=
               Current.OriginalShadowTexelsPerPixel ||
           Previous.OriginalGlobalShadowDistanceScale !=
               Current.OriginalGlobalShadowDistanceScale ||
           Previous.bOriginalWholeSceneDominantShadows !=
               Current.bOriginalWholeSceneDominantShadows ||
           Previous.bOriginalDynamicShadows !=
               Current.bOriginalDynamicShadows ||
           Previous.bOriginalPerObjectShadows !=
               Current.bOriginalPerObjectShadows ||
           Previous.bOriginalForegroundPreshadows !=
               Current.bOriginalForegroundPreshadows ||
           Previous.OriginalBloomQuality != Current.OriginalBloomQuality ||
           Previous.OriginalDepthOfFieldQuality !=
               Current.OriginalDepthOfFieldQuality ||
           Previous.OriginalDistanceFogQuality !=
               Current.OriginalDistanceFogQuality ||
           Previous.bOriginalScreenSpaceReflections !=
               Current.bOriginalScreenSpaceReflections ||
           Previous.bOriginalHBAO != Current.bOriginalHBAO ||
           Previous.bOriginalDepthOfField !=
               Current.bOriginalDepthOfField ||
           Previous.bOriginalLightShafts != Current.bOriginalLightShafts ||
           Previous.bOriginalLightCones != Current.bOriginalLightCones ||
           Previous.bOriginalLensFlares != Current.bOriginalLensFlares ||
           Previous.bOriginalAmbientOcclusion !=
               Current.bOriginalAmbientOcclusion ||
           Previous.bOriginalBloom != Current.bOriginalBloom ||
           Previous.bOriginalDistortion != Current.bOriginalDistortion ||
           Previous.bOriginalFilteredDistortion !=
               Current.bOriginalFilteredDistortion ||
           Previous.bOriginalDropParticleDistortion !=
               Current.bOriginalDropParticleDistortion ||
           Previous.bOriginalSecondaryBloodEffects !=
               Current.bOriginalSecondaryBloodEffects ||
           Previous.bOriginalExplosionLights !=
               Current.bOriginalExplosionLights ||
           Previous.bOriginalSprayActorLights !=
               Current.bOriginalSprayActorLights ||
           Previous.bOriginalPilotLights != Current.bOriginalPilotLights ||
           Previous.bOriginalBloodSplatterDecals !=
               Current.bOriginalBloodSplatterDecals ||
           Previous.bOriginalSubsurfaceScattering !=
               Current.bOriginalSubsurfaceScattering ||
           Previous.bOriginalCorpseCollideWithDead !=
               Current.bOriginalCorpseCollideWithDead ||
           Previous.bOriginalCorpseCollideWithLiving !=
               Current.bOriginalCorpseCollideWithLiving ||
           Previous.bOriginalCorpseCollideWithDeadAfterSleep !=
               Current.bOriginalCorpseCollideWithDeadAfterSleep ||
           Previous.bOriginalLightFunctions !=
               Current.bOriginalLightFunctions ||
           Previous.OriginalDetailMode != Current.OriginalDetailMode ||
           Previous.OriginalDestructionLifetimeScale !=
               Current.OriginalDestructionLifetimeScale ||
           Previous.OriginalSkeletalMeshLODBias !=
               Current.OriginalSkeletalMeshLODBias ||
           Previous.OriginalKinematicUpdateScale !=
               Current.OriginalKinematicUpdateScale ||
           Previous.OriginalParticleLODBias !=
               Current.OriginalParticleLODBias ||
           Previous.OriginalCharacterTextureBias !=
               Current.OriginalCharacterTextureBias ||
           Previous.OriginalWeapon1stTextureBias !=
               Current.OriginalWeapon1stTextureBias ||
           Previous.OriginalWeapon3rdTextureBias !=
               Current.OriginalWeapon3rdTextureBias ||
           Previous.OriginalEnvironmentTextureBias !=
               Current.OriginalEnvironmentTextureBias ||
           Previous.OriginalFXTextureBias != Current.OriginalFXTextureBias ||
           Previous.OriginalShadowmapTextureBias !=
               Current.OriginalShadowmapTextureBias ||
           Previous.OriginalMaxAnisotropy != Current.OriginalMaxAnisotropy ||
           Previous.OriginalEmitterPoolScale !=
               Current.OriginalEmitterPoolScale ||
           Previous.OriginalShellEjectLifetime !=
               Current.OriginalShellEjectLifetime ||
           Previous.OriginalGoreLifetimeMultiplier !=
               Current.OriginalGoreLifetimeMultiplier ||
           Previous.OriginalMaxImpactEffectDecals !=
               Current.OriginalMaxImpactEffectDecals ||
           Previous.OriginalMaxExplosionDecals !=
               Current.OriginalMaxExplosionDecals ||
           Previous.OriginalMaxBloodEffects !=
               Current.OriginalMaxBloodEffects ||
           Previous.OriginalMaxGoreEffects !=
               Current.OriginalMaxGoreEffects ||
           Previous.OriginalMaxPersistentSplatsPerFrame !=
               Current.OriginalMaxPersistentSplatsPerFrame ||
           Previous.OriginalMaxBodyWoundDecals !=
               Current.OriginalMaxBodyWoundDecals;
}

// Rebase only values that changed while KF2's graphics menu was active. This
// preserves untouched Adaptive reductions as temporary composition state.
static function bool RebaseOriginalFromMenuChange(
    KF2OptimizerAdaptiveGraphicsState Snapshot,
    KF2OptimizerAdaptiveGraphicsState Previous,
    KF2OptimizerAdaptiveGraphicsState Current)
{
    if (Snapshot == None || !Snapshot.bOriginalCaptured ||
        Previous == None || Current == None ||
        !OwnedSettingsDiffer(Previous, Current))
    {
        return false;
    }
    if (Previous.OriginalMaxWholeSceneShadowResolution !=
        Current.OriginalMaxWholeSceneShadowResolution)
        Snapshot.OriginalMaxWholeSceneShadowResolution =
            Current.OriginalMaxWholeSceneShadowResolution;
    if (Previous.OriginalMaxShadowResolution !=
        Current.OriginalMaxShadowResolution)
        Snapshot.OriginalMaxShadowResolution =
            Current.OriginalMaxShadowResolution;
    if (Previous.OriginalShadowFadeResolution !=
        Current.OriginalShadowFadeResolution)
        Snapshot.OriginalShadowFadeResolution =
            Current.OriginalShadowFadeResolution;
    if (Previous.OriginalMinShadowResolution !=
        Current.OriginalMinShadowResolution)
        Snapshot.OriginalMinShadowResolution =
            Current.OriginalMinShadowResolution;
    if (Previous.OriginalShadowTexelsPerPixel !=
        Current.OriginalShadowTexelsPerPixel)
        Snapshot.OriginalShadowTexelsPerPixel =
            Current.OriginalShadowTexelsPerPixel;
    if (Previous.OriginalGlobalShadowDistanceScale !=
        Current.OriginalGlobalShadowDistanceScale)
        Snapshot.OriginalGlobalShadowDistanceScale =
            Current.OriginalGlobalShadowDistanceScale;
    if (Previous.bOriginalWholeSceneDominantShadows !=
        Current.bOriginalWholeSceneDominantShadows)
        Snapshot.bOriginalWholeSceneDominantShadows =
            Current.bOriginalWholeSceneDominantShadows;
    if (Previous.bOriginalDynamicShadows != Current.bOriginalDynamicShadows)
        Snapshot.bOriginalDynamicShadows = Current.bOriginalDynamicShadows;
    if (Previous.bOriginalPerObjectShadows !=
        Current.bOriginalPerObjectShadows)
        Snapshot.bOriginalPerObjectShadows =
            Current.bOriginalPerObjectShadows;
    if (Previous.bOriginalForegroundPreshadows !=
        Current.bOriginalForegroundPreshadows)
        Snapshot.bOriginalForegroundPreshadows =
            Current.bOriginalForegroundPreshadows;
    if (Previous.OriginalBloomQuality != Current.OriginalBloomQuality)
        Snapshot.OriginalBloomQuality = Current.OriginalBloomQuality;
    if (Previous.OriginalDepthOfFieldQuality !=
        Current.OriginalDepthOfFieldQuality)
        Snapshot.OriginalDepthOfFieldQuality =
            Current.OriginalDepthOfFieldQuality;
    if (Previous.OriginalDistanceFogQuality !=
        Current.OriginalDistanceFogQuality)
        Snapshot.OriginalDistanceFogQuality =
            Current.OriginalDistanceFogQuality;
    if (Previous.bOriginalScreenSpaceReflections !=
        Current.bOriginalScreenSpaceReflections)
        Snapshot.bOriginalScreenSpaceReflections =
            Current.bOriginalScreenSpaceReflections;
    if (Previous.bOriginalHBAO != Current.bOriginalHBAO)
        Snapshot.bOriginalHBAO = Current.bOriginalHBAO;
    if (Previous.bOriginalDepthOfField != Current.bOriginalDepthOfField)
        Snapshot.bOriginalDepthOfField = Current.bOriginalDepthOfField;
    if (Previous.bOriginalLightShafts != Current.bOriginalLightShafts)
        Snapshot.bOriginalLightShafts = Current.bOriginalLightShafts;
    if (Previous.bOriginalLightCones != Current.bOriginalLightCones)
        Snapshot.bOriginalLightCones = Current.bOriginalLightCones;
    if (Previous.bOriginalLensFlares != Current.bOriginalLensFlares)
        Snapshot.bOriginalLensFlares = Current.bOriginalLensFlares;
    if (Previous.bOriginalAmbientOcclusion !=
        Current.bOriginalAmbientOcclusion)
        Snapshot.bOriginalAmbientOcclusion =
            Current.bOriginalAmbientOcclusion;
    if (Previous.bOriginalBloom != Current.bOriginalBloom)
        Snapshot.bOriginalBloom = Current.bOriginalBloom;
    if (Previous.bOriginalDistortion != Current.bOriginalDistortion)
        Snapshot.bOriginalDistortion = Current.bOriginalDistortion;
    if (Previous.bOriginalFilteredDistortion !=
        Current.bOriginalFilteredDistortion)
        Snapshot.bOriginalFilteredDistortion =
            Current.bOriginalFilteredDistortion;
    if (Previous.bOriginalDropParticleDistortion !=
        Current.bOriginalDropParticleDistortion)
        Snapshot.bOriginalDropParticleDistortion =
            Current.bOriginalDropParticleDistortion;
    if (Previous.bOriginalSecondaryBloodEffects !=
        Current.bOriginalSecondaryBloodEffects)
        Snapshot.bOriginalSecondaryBloodEffects =
            Current.bOriginalSecondaryBloodEffects;
    if (Previous.bOriginalExplosionLights != Current.bOriginalExplosionLights)
        Snapshot.bOriginalExplosionLights = Current.bOriginalExplosionLights;
    if (Previous.bOriginalSprayActorLights !=
        Current.bOriginalSprayActorLights)
        Snapshot.bOriginalSprayActorLights =
            Current.bOriginalSprayActorLights;
    if (Previous.bOriginalPilotLights != Current.bOriginalPilotLights)
        Snapshot.bOriginalPilotLights = Current.bOriginalPilotLights;
    if (Previous.bOriginalBloodSplatterDecals !=
        Current.bOriginalBloodSplatterDecals)
        Snapshot.bOriginalBloodSplatterDecals =
            Current.bOriginalBloodSplatterDecals;
    if (Previous.bOriginalSubsurfaceScattering !=
        Current.bOriginalSubsurfaceScattering)
        Snapshot.bOriginalSubsurfaceScattering =
            Current.bOriginalSubsurfaceScattering;
    if (Previous.bOriginalCorpseCollideWithDead !=
        Current.bOriginalCorpseCollideWithDead)
        Snapshot.bOriginalCorpseCollideWithDead =
            Current.bOriginalCorpseCollideWithDead;
    if (Previous.bOriginalCorpseCollideWithLiving !=
        Current.bOriginalCorpseCollideWithLiving)
        Snapshot.bOriginalCorpseCollideWithLiving =
            Current.bOriginalCorpseCollideWithLiving;
    if (Previous.bOriginalCorpseCollideWithDeadAfterSleep !=
        Current.bOriginalCorpseCollideWithDeadAfterSleep)
        Snapshot.bOriginalCorpseCollideWithDeadAfterSleep =
            Current.bOriginalCorpseCollideWithDeadAfterSleep;
    if (Previous.bOriginalLightFunctions != Current.bOriginalLightFunctions)
        Snapshot.bOriginalLightFunctions = Current.bOriginalLightFunctions;
    if (Previous.OriginalDetailMode != Current.OriginalDetailMode)
        Snapshot.OriginalDetailMode = Current.OriginalDetailMode;
    if (Previous.OriginalDestructionLifetimeScale !=
        Current.OriginalDestructionLifetimeScale)
        Snapshot.OriginalDestructionLifetimeScale =
            Current.OriginalDestructionLifetimeScale;
    if (Previous.OriginalSkeletalMeshLODBias !=
        Current.OriginalSkeletalMeshLODBias)
        Snapshot.OriginalSkeletalMeshLODBias =
            Current.OriginalSkeletalMeshLODBias;
    if (Previous.OriginalKinematicUpdateScale !=
        Current.OriginalKinematicUpdateScale)
        Snapshot.OriginalKinematicUpdateScale =
            Current.OriginalKinematicUpdateScale;
    if (Previous.OriginalParticleLODBias != Current.OriginalParticleLODBias)
        Snapshot.OriginalParticleLODBias = Current.OriginalParticleLODBias;
    if (Previous.OriginalCharacterTextureBias !=
        Current.OriginalCharacterTextureBias)
        Snapshot.OriginalCharacterTextureBias =
            Current.OriginalCharacterTextureBias;
    if (Previous.OriginalWeapon1stTextureBias !=
        Current.OriginalWeapon1stTextureBias)
        Snapshot.OriginalWeapon1stTextureBias =
            Current.OriginalWeapon1stTextureBias;
    if (Previous.OriginalWeapon3rdTextureBias !=
        Current.OriginalWeapon3rdTextureBias)
        Snapshot.OriginalWeapon3rdTextureBias =
            Current.OriginalWeapon3rdTextureBias;
    if (Previous.OriginalEnvironmentTextureBias !=
        Current.OriginalEnvironmentTextureBias)
        Snapshot.OriginalEnvironmentTextureBias =
            Current.OriginalEnvironmentTextureBias;
    if (Previous.OriginalFXTextureBias != Current.OriginalFXTextureBias)
        Snapshot.OriginalFXTextureBias = Current.OriginalFXTextureBias;
    if (Previous.OriginalShadowmapTextureBias !=
        Current.OriginalShadowmapTextureBias)
        Snapshot.OriginalShadowmapTextureBias =
            Current.OriginalShadowmapTextureBias;
    if (Previous.OriginalMaxAnisotropy != Current.OriginalMaxAnisotropy)
        Snapshot.OriginalMaxAnisotropy = Current.OriginalMaxAnisotropy;
    if (Previous.OriginalEmitterPoolScale != Current.OriginalEmitterPoolScale)
        Snapshot.OriginalEmitterPoolScale = Current.OriginalEmitterPoolScale;
    if (Previous.OriginalShellEjectLifetime !=
        Current.OriginalShellEjectLifetime)
        Snapshot.OriginalShellEjectLifetime =
            Current.OriginalShellEjectLifetime;
    if (Previous.OriginalGoreLifetimeMultiplier !=
        Current.OriginalGoreLifetimeMultiplier)
        Snapshot.OriginalGoreLifetimeMultiplier =
            Current.OriginalGoreLifetimeMultiplier;
    if (Previous.OriginalMaxImpactEffectDecals !=
        Current.OriginalMaxImpactEffectDecals)
        Snapshot.OriginalMaxImpactEffectDecals =
            Current.OriginalMaxImpactEffectDecals;
    if (Previous.OriginalMaxExplosionDecals !=
        Current.OriginalMaxExplosionDecals)
        Snapshot.OriginalMaxExplosionDecals =
            Current.OriginalMaxExplosionDecals;
    if (Previous.OriginalMaxBloodEffects != Current.OriginalMaxBloodEffects)
        Snapshot.OriginalMaxBloodEffects = Current.OriginalMaxBloodEffects;
    if (Previous.OriginalMaxGoreEffects != Current.OriginalMaxGoreEffects)
        Snapshot.OriginalMaxGoreEffects = Current.OriginalMaxGoreEffects;
    if (Previous.OriginalMaxPersistentSplatsPerFrame !=
        Current.OriginalMaxPersistentSplatsPerFrame)
        Snapshot.OriginalMaxPersistentSplatsPerFrame =
            Current.OriginalMaxPersistentSplatsPerFrame;
    if (Previous.OriginalMaxBodyWoundDecals !=
        Current.OriginalMaxBodyWoundDecals)
        Snapshot.OriginalMaxBodyWoundDecals =
            Current.OriginalMaxBodyWoundDecals;
    return true;
}

static function CaptureOriginal(
    KF2OptimizerAdaptiveGraphicsState Snapshot, GFXSettings Current)
{
    CopyOwnedSettings(Snapshot, Current);
    Snapshot.GpuQuality = 100;
    Snapshot.CpuQuality = 100;
    Snapshot.VramQuality = 100;
    Snapshot.RamQuality = 100;
    Snapshot.OverdrawQuality = 100;
    Snapshot.EffectsQuality = 100;
    Snapshot.FixedOverdrawQuality = 100;
    Snapshot.FixedEffectsQuality = 100;
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

static function bool NativeReadbackMatches(
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
               Requested.TextureFiltering.MaxAnisotropy;
}

static function bool ScriptReadbackMatches(
    GFXSettings Observed, GFXSettings Requested)
{
    return Observed.FX.AllowExplosionLights ==
               Requested.FX.AllowExplosionLights &&
           Observed.FX.AllowSprayActorLights ==
               Requested.FX.AllowSprayActorLights &&
           Observed.FX.AllowPilotLights == Requested.FX.AllowPilotLights &&
           Observed.FX.AllowBloodSplatterDecals ==
               Requested.FX.AllowBloodSplatterDecals &&
           Abs(Observed.EnvironmentDetail.DestructionLifetimeScale -
               Requested.EnvironmentDetail.DestructionLifetimeScale) <
               0.001 &&
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

static function bool ReadbackMatches(
    GFXSettings Observed, GFXSettings Requested)
{
    return NativeReadbackMatches(Observed, Requested) &&
        ScriptReadbackMatches(Observed, Requested);
}

// The stock menu setters persist several config classes on every call.
// Adaptive owns only these temporary defaults; live-manager readback and
// session restoration remain the caller's responsibility.
static function ApplyTransientScriptSettings(GFXSettings Requested)
{
    class'WorldInfo'.default.DestructionLifetimeScale =
        Requested.EnvironmentDetail.DestructionLifetimeScale;
    class'WorldInfo'.default.EmitterPoolScale = Requested.FX.EmitterPoolScale;
    class'KFMuzzleFlash'.default.ShellEjectLifetime =
        Requested.FX.ShellEjectLifetime;
    class'WorldInfo'.default.bAllowExplosionLights =
        Requested.FX.AllowExplosionLights;
    class'KFSprayActor'.default.bAllowSprayLights =
        Requested.FX.AllowSprayActorLights;
    class'KFWeap_FlameBase'.default.bArePilotLightsAllowed =
        Requested.FX.AllowPilotLights;
    class'KFGoreManager'.default.bAllowBloodSplatterDecals =
        Requested.FX.AllowBloodSplatterDecals;
    class'KFImpactEffectManager'.default.MaxImpactEffectDecals =
        Requested.FX.MaxImpactEffectDecals;
    class'WorldInfo'.default.MaxExplosionDecals =
        Requested.FX.MaxExplosionDecals;
    class'KFGoreManager'.default.GoreFXLifetimeMultiplier =
        Requested.FX.GoreFXLifetimeMultiplier;
    class'KFGoreManager'.default.MaxBloodEffects = Requested.FX.MaxBloodEffects;
    class'KFGoreManager'.default.MaxGoreEffects = Requested.FX.MaxGoreEffects;
    class'KFGoreManager'.default.MaxPersistentSplatsPerFrame =
        Requested.FX.MaxPersistentSplatsPerFrame;
    class'KFGoreManager'.default.MaxBodyWoundDecals =
        Requested.CharacterDetail.MaxBodyWoundDecals;
}

static function ApplyChangedSettings(
    GFXSettings Current, GFXSettings Requested, out GFXSettings Observed)
{
    local bool bNativeChanged;
    local bool bScriptChanged;

    bNativeChanged = !NativeReadbackMatches(Current, Requested);
    bScriptChanged = !ScriptReadbackMatches(Current, Requested);
    // Pause actual writes through the graphics-menu closing edge. The caller
    // keeps its existing readback failure/retry path; no false APPLIED receipt.
    if ((bNativeChanged || bScriptChanged) &&
        class'KF2OptimizerGraphicsInteraction'.static.
            IsGraphicsMenuTransactionOpen())
    {
        GetCurrentGFXSettings(Observed);
        return;
    }
    // KF2's native setter can refresh render/streaming state. Do not invoke it
    // for matching native values, even when script-owned budgets changed.
    if (bNativeChanged)
    {
        SetNativeSettings(Requested);
    }
    if (bScriptChanged)
    {
        ApplyTransientScriptSettings(Requested);
    }
    // A skipped write is not a cached acknowledgement: verify actual state.
    GetCurrentGFXSettings(Observed);
}

static function SetQualityRestoreDebt(
    KF2OptimizerAdaptiveGraphicsState Snapshot,
    int GpuQuality, int CpuQuality, int VramQuality, int RamQuality,
    int OverdrawQuality, int EffectsQuality)
{
    if (Snapshot == None) return;
    Snapshot.RestoreGpuQuality = GpuQuality;
    Snapshot.RestoreCpuQuality = CpuQuality;
    Snapshot.RestoreVramQuality = VramQuality;
    Snapshot.RestoreRamQuality = RamQuality;
    Snapshot.RestoreOverdrawQuality = OverdrawQuality;
    Snapshot.RestoreEffectsQuality = EffectsQuality;
    Snapshot.bQualityRestorePending = true;
    Snapshot.bQualityStateKnown = false;
}

static function ClearQualityRestoreDebt(
    KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    if (Snapshot == None) return;
    Snapshot.bQualityRestorePending = false;
    Snapshot.bQualityStateKnown = true;
}

// Writes one exact six-group composition and verifies it with one readback.
// The caller owns the restore debt until graphics and runtime effects both
// verify; this function never substitutes the rejected composition.
static function bool ApplyQualityComposition(
    KF2OptimizerAdaptiveGraphicsState Snapshot,
    int GpuQuality, int CpuQuality, int VramQuality, int RamQuality,
    int OverdrawQuality, int EffectsQuality)
{
    local GFXSettings Current;
    local GFXSettings Requested;
    local GFXSettings Observed;

    if (Snapshot == None ||
        GpuQuality < 10 || GpuQuality > 100 ||
        CpuQuality < 10 || CpuQuality > 100 ||
        VramQuality < 10 || VramQuality > 100 ||
        RamQuality < 10 || RamQuality > 100 ||
        OverdrawQuality < 10 || OverdrawQuality > 100 ||
        EffectsQuality < 10 || EffectsQuality > 100)
    {
        return false;
    }
    GetCurrentGFXSettings(Current);
    if (!Snapshot.bOriginalCaptured) CaptureOriginal(Snapshot, Current);
    Snapshot.GpuQuality = GpuQuality;
    Snapshot.CpuQuality = CpuQuality;
    Snapshot.VramQuality = VramQuality;
    Snapshot.RamQuality = RamQuality;
    Snapshot.OverdrawQuality = OverdrawQuality;
    Snapshot.EffectsQuality = EffectsQuality;
    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, GpuQuality);
    ApplyCpu(Requested, CpuQuality);
    ApplyVram(Requested, VramQuality);
    ApplyRam(Requested, RamQuality);
    ApplyOverdraw(Requested, GetEffectiveOverdrawQuality(Snapshot));
    ApplyEffects(Requested, GetEffectiveEffectsQuality(Snapshot));
    ApplyChangedSettings(Current, Requested, Observed);
    return ReadbackMatches(Observed, Requested);
}

static function bool ApplyQualityRestoreDebt(
    KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    return Snapshot != None && Snapshot.bQualityRestorePending &&
        ApplyQualityComposition(
            Snapshot, Snapshot.RestoreGpuQuality, Snapshot.RestoreCpuQuality,
            Snapshot.RestoreVramQuality, Snapshot.RestoreRamQuality,
            Snapshot.RestoreOverdrawQuality, Snapshot.RestoreEffectsQuality);
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
    local int PreviousEffectsQuality;
    local int PreviousFixedOverdrawQuality;
    local int PreviousFixedEffectsQuality;

    if (Snapshot == None || Quality < 10 || Quality > 100) return false;
    GetCurrentGFXSettings(Current);
    if (!Snapshot.bOriginalCaptured) CaptureOriginal(Snapshot, Current);
    PreviousGpuQuality = Snapshot.GpuQuality;
    PreviousCpuQuality = Snapshot.CpuQuality;
    PreviousVramQuality = Snapshot.VramQuality;
    PreviousRamQuality = Snapshot.RamQuality;
    PreviousOverdrawQuality = Snapshot.OverdrawQuality;
    PreviousEffectsQuality = Snapshot.EffectsQuality;
    PreviousFixedOverdrawQuality = Snapshot.FixedOverdrawQuality;
    PreviousFixedEffectsQuality = Snapshot.FixedEffectsQuality;

    if (Resource ~= "recover")
    {
        // Recovery must never lower a resource group that was not degraded.
        Snapshot.GpuQuality = Max(Snapshot.GpuQuality, Quality);
        Snapshot.CpuQuality = Max(Snapshot.CpuQuality, Quality);
        Snapshot.VramQuality = Max(Snapshot.VramQuality, Quality);
        Snapshot.RamQuality = Max(Snapshot.RamQuality, Quality);
        Snapshot.OverdrawQuality = Max(Snapshot.OverdrawQuality, Quality);
        Snapshot.EffectsQuality = Max(Snapshot.EffectsQuality, Quality);
    }
    else if (Resource ~= "gpu") Snapshot.GpuQuality = Quality;
    else if (Resource ~= "cpu") Snapshot.CpuQuality = Quality;
    else if (Resource ~= "vram") Snapshot.VramQuality = Quality;
    else if (Resource ~= "ram") Snapshot.RamQuality = Quality;
    else if (Resource ~= "overdraw") Snapshot.OverdrawQuality = Quality;
    else if (Resource ~= "effects") Snapshot.EffectsQuality = Quality;
    else if (Resource ~= "mixed")
    {
        Snapshot.GpuQuality = Quality;
        Snapshot.CpuQuality = Quality;
        Snapshot.VramQuality = Quality;
        Snapshot.RamQuality = Quality;
    }
    else if (Resource ~= "fixed")
    {
        Snapshot.FixedOverdrawQuality = Quality;
        Snapshot.FixedEffectsQuality = Quality;
    }
    else return false;

    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
    ApplyOverdraw(Requested, GetEffectiveOverdrawQuality(Snapshot));
    ApplyEffects(Requested, GetEffectiveEffectsQuality(Snapshot));
    ApplyChangedSettings(Current, Requested, Observed);
    if (ReadbackMatches(Observed, Requested))
    {
        Snapshot.bQualityStateKnown = true;
        return true;
    }

    Snapshot.GpuQuality = PreviousGpuQuality;
    Snapshot.CpuQuality = PreviousCpuQuality;
    Snapshot.VramQuality = PreviousVramQuality;
    Snapshot.RamQuality = PreviousRamQuality;
    Snapshot.OverdrawQuality = PreviousOverdrawQuality;
    Snapshot.EffectsQuality = PreviousEffectsQuality;
    Snapshot.FixedOverdrawQuality = PreviousFixedOverdrawQuality;
    Snapshot.FixedEffectsQuality = PreviousFixedEffectsQuality;
    Current = Observed;
    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
    ApplyOverdraw(Requested, GetEffectiveOverdrawQuality(Snapshot));
    ApplyEffects(Requested, GetEffectiveEffectsQuality(Snapshot));
    ApplyChangedSettings(Current, Requested, Observed);
    if (ReadbackMatches(Observed, Requested))
    {
        Snapshot.bQualityStateKnown = true;
        `log("KF2OPT_ADAPTIVE_ROLLBACK state=applied reason=readback_mismatch");
    }
    else
    {
        SetQualityRestoreDebt(
            Snapshot, PreviousGpuQuality, PreviousCpuQuality,
            PreviousVramQuality, PreviousRamQuality,
            PreviousOverdrawQuality, PreviousEffectsQuality);
        `log("KF2OPT_ADAPTIVE_ROLLBACK state=failed reason=readback_mismatch");
    }
    return false;
}

static function bool ApplyFixedSessionEffects(
    KF2OptimizerAdaptiveGraphicsState Snapshot)
{
    // Apply both groups through one composition and one verified readback.
    // ApplyResource restores the previous qualities if that readback fails.
    return ApplyResource(Snapshot, "fixed", FixedSessionEffectsQuality);
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
    ApplyChangedSettings(Current, Requested, Observed);
    if (!ReadbackMatches(Observed, Requested)) return false;
    Snapshot.GpuQuality = 100;
    Snapshot.CpuQuality = 100;
    Snapshot.VramQuality = 100;
    Snapshot.RamQuality = 100;
    Snapshot.OverdrawQuality = 100;
    Snapshot.EffectsQuality = 100;
    Snapshot.FixedOverdrawQuality = 100;
    Snapshot.FixedEffectsQuality = 100;
    Snapshot.bOriginalCaptured = false;
    ClearQualityRestoreDebt(Snapshot);
    return true;
}

// Uses the same composite preset comparison as KF2's own options menu.
// INDEX_NONE (-1) is a real INI-override state, not a guessed quality tier.
static function string MenuReadback(
    optional KF2OptimizerAdaptiveGraphicsState ObservedState)
{
    local GFXSettings Current;
    local float FilmGrainRange;
    local int FilmGrainPercent;

    GetCurrentGFXSettings(Current);
    if (ObservedState != None)
    {
        CopyOwnedSettings(ObservedState, Current);
    }
    FilmGrainRange = default.FilmGrainMinMaxPreset[1].FilmGrainScale -
        default.FilmGrainMinMaxPreset[0].FilmGrainScale;
    if (FilmGrainRange <= 0.0)
    {
        return "";
    }
    FilmGrainPercent = Clamp(int(100.0 *
        (Current.FilmGrain.FilmGrainScale -
            default.FilmGrainMinMaxPreset[0].FilmGrainScale) /
        FilmGrainRange + 0.5), 0, 100);
    return "resx=" $ Current.Resolution.ResX $
        " resy=" $ Current.Resolution.ResY $
        " display_full=" $ int(Current.Display.Fullscreen) $
        " display_borderless=" $ int(Current.Display.BorderlessWindow) $
        " vsync=" $ int(Current.VSync.VSync) $
        " variable_fps=" $ int(Current.VariableFPS.VariableFramerate) $
        " film_grain=" $ FilmGrainPercent $
        " environment=" $ FindEnvironmentDetailIndex(
            Current.EnvironmentDetail, default.EnvironmentDetailPresets) $
        " character=" $ FindCharacterDetailIndex(
            Current.CharacterDetail, default.CharacterDetailPresets) $
        " fx=" $ FindFXQualityIndex(Current.FX, default.FXQualityPresets) $
        " texture_resolution=" $ FindTextureResolutionSettingIndex(
            Current.TextureResolution, default.TextureResolutionPresets) $
        " texture_filtering=" $ FindTextureFilterSettingIndex(
            Current.TextureFiltering, default.TextureFilterPresets) $
        " shadows=" $ FindShadowQualityIndex(
            Current.Shadows, default.ShadowQualityPresets) $
        " reflections=" $ FindReflectionsSettingIndex(
            Current.RealtimeReflections, default.RealtimeReflectionsPresets) $
        " aa=" $ FindAntiAliasingSettingIndex(
            Current.AntiAliasing, default.AntiAliasingPresets) $
        " bloom=" $ FindBloomSettingIndex(Current.Bloom, default.BloomPresets) $
        " motion_blur=" $ FindMotionBlurSettingIndex(
            Current.MotionBlur, default.MotionBlurPresets) $
        " ao=" $ FindAmbientOcclusionSettingIndex(
            Current.AmbientOcclusion, default.AmbientOcclusionPresets) $
        " dof=" $ FindDOFSettingIndex(
            Current.DepthOfField, default.DOFPresets) $
        " volumetric=" $ FindVolumetricLightingSettingIndex(
            Current.VolumetricLighting, default.VolumetricLightingPresets) $
        " lens_flares=" $ FindLensFlareSettingIndex(
            Current.LensFlares, default.LensFlarePresets) $
        " light_shafts=" $ FindLightShaftsSettingIndex(
            Current.LightShafts, default.LightShaftsPresets) $
        " flex=" $ FindFlexSettingIndex(Current.Flex, default.FlexPresets);
}

defaultproperties
{
}
