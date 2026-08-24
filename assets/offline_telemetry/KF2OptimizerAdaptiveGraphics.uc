// Live graphics actuator kept inside the KF2 graphics-options type hierarchy.
// This avoids exporting KF2's large native GFXSettings struct across classes.
class KF2OptimizerAdaptiveGraphics extends KFGFxOptionsMenu_Graphics;

static function ApplyGpu(out GFXSettings Requested, int Quality)
{
    local int ShadowResolution;
    local float ShadowDensity;

    if (Quality >= 100) return;
    if (Quality >= 90)
    {
        ShadowResolution = 4096;
        ShadowDensity = 2.25;
    }
    else if (Quality >= 80)
    {
        ShadowResolution = 2048;
        ShadowDensity = 2.0;
    }
    else if (Quality >= 70)
    {
        ShadowResolution = 2048;
        ShadowDensity = 1.75;
    }
    else if (Quality >= 60)
    {
        ShadowResolution = 1024;
        ShadowDensity = 1.5;
    }
    else if (Quality >= 40)
    {
        ShadowResolution = 1024;
        ShadowDensity = 1.25;
    }
    else
    {
        ShadowResolution = 512;
        ShadowDensity = 1.0;
    }
    Requested.Shadows.MaxWholeSceneDominantShadowResolution = Min(
        Requested.Shadows.MaxWholeSceneDominantShadowResolution,
        ShadowResolution);
    Requested.Shadows.MaxShadowResolution = Min(
        Requested.Shadows.MaxShadowResolution, ShadowResolution);
    Requested.Shadows.ShadowTexelsPerPixel = FMin(
        Requested.Shadows.ShadowTexelsPerPixel, ShadowDensity);
    Requested.Bloom.BloomQuality = Min(
        Requested.Bloom.BloomQuality,
        Quality >= 90 ? 5 : (Quality >= 70 ? 4 : 3));
    if (Quality <= 80)
    {
        Requested.RealtimeReflections.bAllowScreenSpaceReflections = false;
    }
    if (Quality <= 70)
    {
        Requested.AmbientOcclusion.HBAO = false;
    }
    if (Quality <= 60)
    {
        Requested.DepthOfField.DepthOfField = false;
        Requested.LightShafts.bAllowLightShafts = false;
    }
    if (Quality <= 50)
    {
        Requested.VolumetricLighting.bAllowLightCones = false;
        Requested.LensFlares.bAllowLensFlares = false;
    }
    if (Quality <= 30)
    {
        Requested.AmbientOcclusion.AmbientOcclusion = false;
    }
    if (Quality <= 20)
    {
        Requested.Bloom.Bloom = false;
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
}

static function CaptureOriginal(
    KF2OptimizerAdaptiveGraphicsState Snapshot, GFXSettings Current)
{
    Snapshot.OriginalMaxWholeSceneShadowResolution =
        Current.Shadows.MaxWholeSceneDominantShadowResolution;
    Snapshot.OriginalMaxShadowResolution = Current.Shadows.MaxShadowResolution;
    Snapshot.OriginalShadowTexelsPerPixel = Current.Shadows.ShadowTexelsPerPixel;
    Snapshot.OriginalBloomQuality = Current.Bloom.BloomQuality;
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
    Snapshot.OriginalDetailMode = Current.EnvironmentDetail.DetailMode;
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
    Snapshot.GpuQuality = 100;
    Snapshot.CpuQuality = 100;
    Snapshot.VramQuality = 100;
    Snapshot.RamQuality = 100;
    Snapshot.bOriginalCaptured = true;
}

static function RestoreOwnedSettings(
    KF2OptimizerAdaptiveGraphicsState Snapshot, out GFXSettings Requested)
{
    Requested.Shadows.MaxWholeSceneDominantShadowResolution =
        Snapshot.OriginalMaxWholeSceneShadowResolution;
    Requested.Shadows.MaxShadowResolution = Snapshot.OriginalMaxShadowResolution;
    Requested.Shadows.ShadowTexelsPerPixel =
        Snapshot.OriginalShadowTexelsPerPixel;
    Requested.Bloom.BloomQuality = Snapshot.OriginalBloomQuality;
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
    Requested.EnvironmentDetail.DetailMode = Snapshot.OriginalDetailMode;
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
}

static function bool ReadbackMatches(
    GFXSettings Observed, GFXSettings Requested)
{
    return Observed.Shadows.MaxWholeSceneDominantShadowResolution ==
               Requested.Shadows.MaxWholeSceneDominantShadowResolution &&
           Observed.Shadows.MaxShadowResolution ==
               Requested.Shadows.MaxShadowResolution &&
           Abs(Observed.Shadows.ShadowTexelsPerPixel -
               Requested.Shadows.ShadowTexelsPerPixel) < 0.001 &&
           Observed.Bloom.BloomQuality == Requested.Bloom.BloomQuality &&
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
               Requested.FX.MaxPersistentSplatsPerFrame;
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

    if (Snapshot == None || Quality < 10 || Quality > 100) return false;
    GetCurrentGFXSettings(Current);
    if (!Snapshot.bOriginalCaptured) CaptureOriginal(Snapshot, Current);
    PreviousGpuQuality = Snapshot.GpuQuality;
    PreviousCpuQuality = Snapshot.CpuQuality;
    PreviousVramQuality = Snapshot.VramQuality;
    PreviousRamQuality = Snapshot.RamQuality;

    if (Resource ~= "recover")
    {
        // Recovery must never lower a resource group that was not degraded.
        Snapshot.GpuQuality = Max(Snapshot.GpuQuality, Quality);
        Snapshot.CpuQuality = Max(Snapshot.CpuQuality, Quality);
        Snapshot.VramQuality = Max(Snapshot.VramQuality, Quality);
        Snapshot.RamQuality = Max(Snapshot.RamQuality, Quality);
    }
    else if (Resource ~= "gpu") Snapshot.GpuQuality = Quality;
    else if (Resource ~= "cpu") Snapshot.CpuQuality = Quality;
    else if (Resource ~= "vram") Snapshot.VramQuality = Quality;
    else if (Resource ~= "ram") Snapshot.RamQuality = Quality;
    else if (Resource ~= "mixed")
    {
        Snapshot.GpuQuality = Quality;
        Snapshot.CpuQuality = Quality;
        Snapshot.VramQuality = Quality;
        Snapshot.RamQuality = Quality;
    }
    else return false;

    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
    SetNativeSettings(Requested);
    SetScriptSettings(Requested);
    GetCurrentGFXSettings(Observed);
    if (ReadbackMatches(Observed, Requested)) return true;

    Snapshot.GpuQuality = PreviousGpuQuality;
    Snapshot.CpuQuality = PreviousCpuQuality;
    Snapshot.VramQuality = PreviousVramQuality;
    Snapshot.RamQuality = PreviousRamQuality;
    Requested = Observed;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuQuality);
    ApplyCpu(Requested, Snapshot.CpuQuality);
    ApplyVram(Requested, Snapshot.VramQuality);
    ApplyRam(Requested, Snapshot.RamQuality);
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
    Snapshot.bOriginalCaptured = false;
    return true;
}

defaultproperties
{
}
