// Live graphics actuator kept inside the KF2 graphics-options type hierarchy.
// This avoids exporting KF2's large native GFXSettings struct across classes.
class KF2OptimizerAdaptiveGraphics extends KFGFxOptionsMenu_Graphics;

static function ApplyGpu(out GFXSettings Requested, int Level)
{
    local int ShadowResolution;
    local float ShadowDensity;

    if (Level >= 3) return;
    if (Level == 2)
    {
        ShadowResolution = 2048;
        ShadowDensity = 2.0;
    }
    else if (Level == 1)
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
        Requested.Bloom.BloomQuality, Level == 2 ? 4 : 3);
    if (Level <= 1)
    {
        Requested.RealtimeReflections.bAllowScreenSpaceReflections = false;
        Requested.AmbientOcclusion.HBAO = false;
        Requested.DepthOfField.DepthOfField = false;
        Requested.LightShafts.bAllowLightShafts = false;
        Requested.VolumetricLighting.bAllowLightCones = false;
        Requested.LensFlares.bAllowLensFlares = false;
    }
    if (Level == 0)
    {
        Requested.AmbientOcclusion.AmbientOcclusion = false;
        Requested.Bloom.Bloom = false;
    }
}

static function ApplyCpu(out GFXSettings Requested, int Level)
{
    local float KinematicScale;

    if (Level >= 3) return;
    if (Level == 2) KinematicScale = 1.3;
    else if (Level == 1) KinematicScale = 2.0;
    else KinematicScale = 3.0;
    Requested.EnvironmentDetail.DetailMode = Min(
        Requested.EnvironmentDetail.DetailMode, Level);
    Requested.CharacterDetail.SkeletalMeshLODBias = Max(
        Requested.CharacterDetail.SkeletalMeshLODBias, 3 - Level);
    Requested.CharacterDetail.KinematicUpdateDistFactorScale = FMax(
        Requested.CharacterDetail.KinematicUpdateDistFactorScale,
        KinematicScale);
    Requested.FX.ParticleLODBias = Max(
        Requested.FX.ParticleLODBias, 3 - Level);
}

static function ApplyVram(out GFXSettings Requested, int Level)
{
    local int Bias;
    local int Anisotropy;

    if (Level >= 3) return;
    Bias = 3 - Level;
    if (Level == 2) Anisotropy = 8;
    else if (Level == 1) Anisotropy = 4;
    else Anisotropy = 2;
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

static function ApplyRam(out GFXSettings Requested, int Level)
{
    if (Level >= 3) return;
    if (Level == 2)
    {
        Requested.FX.EmitterPoolScale = FMin(
            Requested.FX.EmitterPoolScale, 1.0);
        Requested.FX.ShellEjectLifetime = FMin(
            Requested.FX.ShellEjectLifetime, 10.0);
        Requested.FX.GoreFXLifetimeMultiplier = FMin(
            Requested.FX.GoreFXLifetimeMultiplier, 1.0);
        Requested.FX.MaxImpactEffectDecals = Min(
            Requested.FX.MaxImpactEffectDecals, 20);
        Requested.FX.MaxExplosionDecals = Min(
            Requested.FX.MaxExplosionDecals, 15);
        Requested.FX.MaxBloodEffects = Min(
            Requested.FX.MaxBloodEffects, 25);
        Requested.FX.MaxGoreEffects = Min(
            Requested.FX.MaxGoreEffects, 10);
        Requested.FX.MaxPersistentSplatsPerFrame = Min(
            Requested.FX.MaxPersistentSplatsPerFrame, 75);
        return;
    }
    if (Level == 1)
    {
        Requested.FX.EmitterPoolScale = FMin(
            Requested.FX.EmitterPoolScale, 0.5);
        Requested.FX.ShellEjectLifetime = FMin(
            Requested.FX.ShellEjectLifetime, 5.0);
        Requested.FX.GoreFXLifetimeMultiplier = FMin(
            Requested.FX.GoreFXLifetimeMultiplier, 0.75);
        Requested.FX.MaxImpactEffectDecals = Min(
            Requested.FX.MaxImpactEffectDecals, 15);
        Requested.FX.MaxExplosionDecals = Min(
            Requested.FX.MaxExplosionDecals, 12);
        Requested.FX.MaxBloodEffects = Min(
            Requested.FX.MaxBloodEffects, 15);
        Requested.FX.MaxGoreEffects = Min(
            Requested.FX.MaxGoreEffects, 8);
        Requested.FX.MaxPersistentSplatsPerFrame = Min(
            Requested.FX.MaxPersistentSplatsPerFrame, 50);
        return;
    }
    Requested.FX.EmitterPoolScale = FMin(
        Requested.FX.EmitterPoolScale, 0.25);
    Requested.FX.ShellEjectLifetime = FMin(
        Requested.FX.ShellEjectLifetime, 2.0);
    Requested.FX.GoreFXLifetimeMultiplier = FMin(
        Requested.FX.GoreFXLifetimeMultiplier, 0.5);
    Requested.FX.MaxImpactEffectDecals = Min(
        Requested.FX.MaxImpactEffectDecals, 8);
    Requested.FX.MaxExplosionDecals = Min(
        Requested.FX.MaxExplosionDecals, 8);
    Requested.FX.MaxBloodEffects = Min(
        Requested.FX.MaxBloodEffects, 12);
    Requested.FX.MaxGoreEffects = Min(
        Requested.FX.MaxGoreEffects, 8);
    Requested.FX.MaxPersistentSplatsPerFrame = Min(
        Requested.FX.MaxPersistentSplatsPerFrame, 25);
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
    Snapshot.GpuLevel = 3;
    Snapshot.CpuLevel = 3;
    Snapshot.VramLevel = 3;
    Snapshot.RamLevel = 3;
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
    KF2OptimizerAdaptiveGraphicsState Snapshot, string Resource, int Level)
{
    local GFXSettings Current;
    local GFXSettings Requested;
    local GFXSettings Observed;
    local int PreviousGpuLevel;
    local int PreviousCpuLevel;
    local int PreviousVramLevel;
    local int PreviousRamLevel;

    if (Snapshot == None || Level < 0 || Level > 3) return false;
    GetCurrentGFXSettings(Current);
    if (!Snapshot.bOriginalCaptured) CaptureOriginal(Snapshot, Current);
    PreviousGpuLevel = Snapshot.GpuLevel;
    PreviousCpuLevel = Snapshot.CpuLevel;
    PreviousVramLevel = Snapshot.VramLevel;
    PreviousRamLevel = Snapshot.RamLevel;

    if (Resource ~= "recover")
    {
        // Recovery must never lower a resource group that was not degraded.
        Snapshot.GpuLevel = Max(Snapshot.GpuLevel, Level);
        Snapshot.CpuLevel = Max(Snapshot.CpuLevel, Level);
        Snapshot.VramLevel = Max(Snapshot.VramLevel, Level);
        Snapshot.RamLevel = Max(Snapshot.RamLevel, Level);
    }
    else if (Resource ~= "gpu") Snapshot.GpuLevel = Level;
    else if (Resource ~= "cpu") Snapshot.CpuLevel = Level;
    else if (Resource ~= "vram") Snapshot.VramLevel = Level;
    else if (Resource ~= "ram") Snapshot.RamLevel = Level;
    else if (Resource ~= "mixed")
    {
        Snapshot.GpuLevel = Level;
        Snapshot.CpuLevel = Level;
        Snapshot.VramLevel = Level;
        Snapshot.RamLevel = Level;
    }
    else return false;

    Requested = Current;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuLevel);
    ApplyCpu(Requested, Snapshot.CpuLevel);
    ApplyVram(Requested, Snapshot.VramLevel);
    ApplyRam(Requested, Snapshot.RamLevel);
    SetNativeSettings(Requested);
    SetScriptSettings(Requested);
    GetCurrentGFXSettings(Observed);
    if (ReadbackMatches(Observed, Requested)) return true;

    Snapshot.GpuLevel = PreviousGpuLevel;
    Snapshot.CpuLevel = PreviousCpuLevel;
    Snapshot.VramLevel = PreviousVramLevel;
    Snapshot.RamLevel = PreviousRamLevel;
    Requested = Observed;
    RestoreOwnedSettings(Snapshot, Requested);
    ApplyGpu(Requested, Snapshot.GpuLevel);
    ApplyCpu(Requested, Snapshot.CpuLevel);
    ApplyVram(Requested, Snapshot.VramLevel);
    ApplyRam(Requested, Snapshot.RamLevel);
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
    Snapshot.GpuLevel = 3;
    Snapshot.CpuLevel = 3;
    Snapshot.VramLevel = 3;
    Snapshot.RamLevel = 3;
    Snapshot.bOriginalCaptured = false;
    return true;
}

defaultproperties
{
}
