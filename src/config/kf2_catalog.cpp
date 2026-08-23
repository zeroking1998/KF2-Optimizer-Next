#include "kf2/config/kf2_catalog.hpp"

#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <locale>
#include <sstream>

namespace kf2::config {
namespace {

constexpr std::array<int, 5> kAnisotropyLevels{1, 2, 4, 8, 16};
constexpr std::array<int, 5> kShadowResolutionLevels{
    256, 512, 1024, 2048, 4096};
constexpr std::array<int, 5> kWholeSceneShadowResolutionLevels{
    256, 512, 1280, 2048, 4096};
constexpr std::array<int, 3> kQualityLevels{0, 1, 2};
constexpr std::array<int, 4> kDepthOfFieldQualityLevels{0, 1, 2, 3};
constexpr std::array<int, 8> kMinShadowResolutionLevels{
    8, 16, 32, 64, 128, 256, 512, 1024};
constexpr std::array<int, 8> kShadowFadeResolutionLevels{
    8, 16, 32, 64, 128, 256, 512, 1024};
constexpr std::array<int, 4> kMultiSampleLevels{1, 2, 4, 8};
constexpr std::array<int, 3> kShadowFilterQualityLevels{-1, 0, 1};
constexpr std::array<int, 2> kMotionBlurSkinningLevels{0, 1};
constexpr std::array<int, 5> kPhysicsSubstepLevels{1, 2, 3, 4, 5};
constexpr std::array<int, 3> kGoreLevels{0, 1, 2};

const std::array<SettingDefinition, 213> kSettings{{
    {SettingId::target_fps, L"KFGame.ini", L"KFGame.KFGameEngine",
     L"MaxSmoothedFrameRate", SettingType::integer,
     optimizer::kTargetFpsMinimum, optimizer::kTargetFpsMaximum, true, {}, 1},
    {SettingId::minimum_smooth_frame_rate, L"KFGame.ini",
     L"KFGame.KFGameEngine", L"MinSmoothedFrameRate",
     SettingType::integer, 1, 360, true, {}, 1},
    {SettingId::smooth_frame_rate, L"KFGame.ini", L"KFGame.KFGameEngine",
     L"bSmoothFrameRate", SettingType::boolean, 0, 1},
    {SettingId::corpse_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxDeadBodies", SettingType::integer, 4, 2000},
    {SettingId::gore_effect_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxGoreEffects", SettingType::integer, 0, 128},
    {SettingId::explosion_decal_limit, L"KFGame.ini", L"Engine.WorldInfo",
     L"MaxExplosionDecals", SettingType::integer, 0, 128},
    {SettingId::impact_decal_limit, L"KFGame.ini", L"KFGame.KFImpactEffectManager",
     L"MaxImpactEffectDecals", SettingType::integer, 0, 128},
    {SettingId::wound_decal_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxBodyWoundDecals", SettingType::integer, 5, 64},
    {SettingId::blood_splatter_decal_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxBloodSplatterDecals", SettingType::integer, 0, 128},
    {SettingId::blood_pool_decal_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxBloodPoolDecals", SettingType::integer, 0, 128},
    {SettingId::blood_effect_limit, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxBloodEffects", SettingType::integer, 0, 128},
    {SettingId::body_wound_decal_lifetime, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"BodyWoundDecalLifetime", SettingType::integer, 0, 120},
    {SettingId::blood_splatter_lifetime, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"BloodSplatterLifetime", SettingType::integer, 0, 120},
    {SettingId::blood_pool_lifetime, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"BloodPoolLifetime", SettingType::integer, 0, 120},
    {SettingId::giblet_lifetime, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"GibletLifetime", SettingType::integer, 0, 120},
    {SettingId::gore_lifetime_multiplier, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"GoreFXLifetimeMultiplier", SettingType::real, 0.1, 2.0, true, {}, 0.05},
    {SettingId::persistent_splats_per_frame, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"MaxPersistentSplatsPerFrame", SettingType::integer, 1, 256},
    {SettingId::blood_splatter_decals, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"AllowBloodSplatterDecals", SettingType::boolean, 0, 1},
    {SettingId::secondary_blood_effects, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowSecondaryBloodEffects", SettingType::boolean, 0, 1},
    {SettingId::static_decals, L"KFSystemSettings.ini", L"SystemSettings",
     L"StaticDecals", SettingType::boolean, 0, 1},
    {SettingId::dynamic_decals, L"KFSystemSettings.ini", L"SystemSettings",
     L"DynamicDecals", SettingType::boolean, 0, 1},
    {SettingId::decal_cull_distance_scale, L"KFSystemSettings.ini", L"SystemSettings",
     L"DecalCullDistanceScale", SettingType::real, 0.1, 2.0, true, {}, 0.05},
    {SettingId::dynamic_shadows, L"KFSystemSettings.ini", L"SystemSettings",
     L"DynamicShadows", SettingType::boolean, 0, 1},
    {SettingId::light_environment_shadows, L"KFSystemSettings.ini", L"SystemSettings",
     L"LightEnvironmentShadows", SettingType::boolean, 0, 1},
    {SettingId::ambient_occlusion, L"KFSystemSettings.ini", L"SystemSettings",
     L"AmbientOcclusion", SettingType::boolean, 0, 1},
    {SettingId::bloom, L"KFSystemSettings.ini", L"SystemSettings",
     L"Bloom", SettingType::boolean, 0, 1},
    {SettingId::distortion, L"KFSystemSettings.ini", L"SystemSettings",
     L"Distortion", SettingType::boolean, 0, 1},
    {SettingId::drop_particle_distortion, L"KFSystemSettings.ini", L"SystemSettings",
     L"DropParticleDistortion", SettingType::boolean, 0, 1},
    {SettingId::high_quality_materials, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowHighQualityMaterials", SettingType::boolean, 0, 1},
    {SettingId::detail_mode, L"KFSystemSettings.ini", L"SystemSettings",
     L"DetailMode", SettingType::integer, 0, 2},
    {SettingId::max_shadow_resolution, L"KFSystemSettings.ini", L"SystemSettings",
     L"MaxShadowResolution", SettingType::integer, 256, 4096, true,
     kShadowResolutionLevels},
    {SettingId::max_whole_scene_shadow_resolution, L"KFSystemSettings.ini",
     L"SystemSettings", L"MaxWholeSceneDominantShadowResolution",
     SettingType::integer, 256, 4096, true,
     kWholeSceneShadowResolutionLevels},
    {SettingId::shadow_texels_per_pixel, L"KFSystemSettings.ini", L"SystemSettings",
     L"ShadowTexelsPerPixel", SettingType::real, 0.1, 4.0, true, {}, 0.1},
    {SettingId::fracture_cull_distance_scale, L"KFSystemSettings.ini", L"SystemSettings",
     L"FractureCullDistanceScale", SettingType::real, 0.1, 2.0, true, {}, 0.05},
    {SettingId::corpse_collision_with_dead, L"KFSystemSettings.ini", L"SystemSettings",
     L"ShouldCorpseCollideWithDead", SettingType::boolean, 0, 1, false},
    {SettingId::corpse_collision_with_living, L"KFSystemSettings.ini", L"SystemSettings",
     L"ShouldCorpseCollideWithLiving", SettingType::boolean, 0, 1, false},
    {SettingId::corpse_collision_after_sleep, L"KFSystemSettings.ini", L"SystemSettings",
     L"ShouldCorpseCollideWithDeadAfterSleep", SettingType::boolean, 0, 1, false},
    {SettingId::particle_lod_bias, L"KFSystemSettings.ini", L"SystemSettings",
     L"ParticleLODBias", SettingType::integer, 0, 1, true},
    {SettingId::skeletal_mesh_lod_bias, L"KFSystemSettings.ini", L"SystemSettings",
     L"SkeletalMeshLODBias", SettingType::integer, 0, 1, true},
    {SettingId::max_draw_distance_scale, L"KFSystemSettings.ini", L"SystemSettings",
     L"MaxDrawDistanceScale", SettingType::real, 0.8, 1.2, false, {}, 0.05},
    {SettingId::global_shadow_distance_scale, L"KFSystemSettings.ini", L"SystemSettings",
     L"GlobalShadowDistanceScale", SettingType::real, 0.75, 1.5, true, {}, 0.05},
    {SettingId::ragdoll_and_gore_on_dead_bodies, L"KFGame.ini", L"KFGame.KFPawn",
     L"bAllowRagdollAndGoreOnDeadBodies", SettingType::boolean, 0, 1, false},
    {SettingId::depth_of_field, L"KFSystemSettings.ini", L"SystemSettings",
     L"DepthOfField", SettingType::boolean, 0, 1, true},
    {SettingId::light_shafts, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowLightShafts", SettingType::boolean, 0, 1, true},
    {SettingId::lens_flares, L"KFSystemSettings.ini", L"SystemSettings",
     L"LensFlares", SettingType::boolean, 0, 1, true},
    {SettingId::radial_blur, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowRadialBlur", SettingType::boolean, 0, 1, true},
    {SettingId::separate_translucency, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowSeparateTranslucency", SettingType::boolean, 0, 1, false},
    {SettingId::fractured_damage, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowFracturedDamage", SettingType::boolean, 0, 1, true},
    {SettingId::motion_blur, L"KFSystemSettings.ini", L"SystemSettings",
     L"MotionBlur", SettingType::boolean, 0, 1, true},
    {SettingId::post_process_aa, L"KFSystemSettings.ini", L"SystemSettings",
     L"PostProcessAA", SettingType::boolean, 0, 1, true},
    {SettingId::distance_fog, L"KFSystemSettings.ini", L"SystemSettings",
     L"DistanceFog", SettingType::boolean, 0, 1, false},
    {SettingId::filtered_distortion, L"KFSystemSettings.ini", L"SystemSettings",
     L"FilteredDistortion", SettingType::boolean, 0, 1, true},
    {SettingId::unbatched_decals, L"KFSystemSettings.ini", L"SystemSettings",
     L"UnbatchedDecals", SettingType::boolean, 0, 1, true},
    {SettingId::whole_scene_dominant_shadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"bAllowWholeSceneDominantShadows",
     SettingType::boolean, 0, 1, true},
    {SettingId::conservative_shadow_bounds, L"KFSystemSettings.ini",
     L"SystemSettings", L"bUseConservativeShadowBounds",
     SettingType::boolean, 0, 1, true},
    {SettingId::max_anisotropy, L"KFSystemSettings.ini", L"SystemSettings",
     L"MaxAnisotropy", SettingType::integer, 1, 16, true,
     kAnisotropyLevels},
    {SettingId::post_process_mlaa, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowPostprocessMLAA", SettingType::boolean, 0, 1, true},
    {SettingId::persistent_splats, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowPersistentSplats", SettingType::boolean, 0, 1, false},
    {SettingId::screen_space_reflections, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowScreenSpaceReflections", SettingType::boolean, 0, 1, true},
    {SettingId::variable_blur_reflections, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowVariableBlurReflections", SettingType::boolean, 0, 1, false},
    {SettingId::subsurface_scattering, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowSubsurfaceScattering", SettingType::boolean, 0, 1, true},
    {SettingId::light_functions, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowLightFunctions", SettingType::boolean, 0, 1, true},
    {SettingId::light_cones, L"KFSystemSettings.ini", L"SystemSettings",
     L"LightCones", SettingType::boolean, 0, 1, true},
    {SettingId::destruction_lifetime_scale, L"KFGame.ini", L"Engine.WorldInfo",
     L"DestructionLifetimeScale", SettingType::real, 0.1, 2.0, true, {}, 0.05},
    {SettingId::explosion_lights, L"KFGame.ini", L"Engine.WorldInfo",
     L"bAllowExplosionLights", SettingType::boolean, 0, 1, true},
    {SettingId::blood_splat_size, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"BloodSplatSize", SettingType::real, 25.0, 200.0, false, {}, 5.0},
    {SettingId::blood_pool_size, L"KFGame.ini", L"KFGame.KFGoreManager",
     L"BloodPoolSize", SettingType::real, 25.0, 250.0, false, {}, 5.0},
    {SettingId::persistent_splat_trace_length, L"KFGame.ini",
     L"KFGame.KFGoreManager", L"PersistentSplatTraceLength",
     SettingType::real, 100.0, 2000.0, false, {}, 50.0},
    {SettingId::whole_scene_shadow_cutoff_distance, L"KFSystemSettings.ini",
     L"SystemSettings", L"WholeSceneShadowCutoffDistance",
     SettingType::real, 100.0, 5000.0, false, {}, 50.0},
    {SettingId::whole_scene_shadow_fade_distance, L"KFSystemSettings.ini",
     L"SystemSettings", L"WholeSceneShadowFadeOutDistance",
     SettingType::real, 100.0, 5000.0, false, {}, 50.0},
    {SettingId::grouped_per_object_shadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowGroupedPerObjectShadows",
     SettingType::boolean, 0, 1, false},
    {SettingId::grouped_shadow_min_radius, L"KFSystemSettings.ini",
     L"SystemSettings", L"GroupedPerObjectShadows_MinRadius",
     SettingType::real, 0.0, 1000.0, false, {}, 25.0},
    {SettingId::grouped_shadow_max_radius, L"KFSystemSettings.ini",
     L"SystemSettings", L"GroupedPerObjectShadows_MaxRadius",
     SettingType::real, 100.0, 10000.0, false, {}, 100.0},
    {SettingId::grouped_shadow_ramp_up_factor, L"KFSystemSettings.ini",
     L"SystemSettings", L"GroupedPerObjectShadows_RampUpFactor",
     SettingType::real, 0.1, 4.0, false, {}, 0.1},
    {SettingId::grouped_shadow_ramp_cutoff, L"KFSystemSettings.ini",
     L"SystemSettings", L"GroupedPerObjectShadows_RampCutoff",
     SettingType::real, 100.0, 10000.0, false, {}, 100.0},
    {SettingId::max_overlapping_lights, L"KFSystemSettings.ini",
     L"SystemSettings", L"MaxOverlappingLights", SettingType::integer,
     1, 32, false, {}, 1},
    {SettingId::light_occlusion_queries, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowLightOcclusionQueries",
     SettingType::boolean, 0, 1, false},
    {SettingId::depth_prepass, L"KFSystemSettings.ini", L"SystemSettings",
     L"DepthPrepass", SettingType::boolean, 0, 1, false},
    {SettingId::reflection_downsample_factor, L"KFSystemSettings.ini",
     L"SystemSettings", L"ScreenSpaceReflectionDownsampleFactor",
     SettingType::integer, 1, 4, false, {}, 1},
    {SettingId::image_grain_scale, L"KFSystemSettings.ini", L"SystemSettings",
     L"ImageGrainScaler", SettingType::real, 0.0, 2.0, false, {}, 0.05},
    {SettingId::depth_of_field_quality, L"KFSystemSettings.ini",
     L"SystemSettings", L"DepthOfFieldQuality", SettingType::integer,
     0, 3, true, kDepthOfFieldQualityLevels},
    {SettingId::bloom_quality, L"KFSystemSettings.ini", L"SystemSettings",
     L"BloomQuality", SettingType::integer, 0, 2, true, kQualityLevels},
    {SettingId::distance_fog_quality, L"KFSystemSettings.ini", L"SystemSettings",
     L"DistanceFogQuality", SettingType::integer, 0, 2, true, kQualityLevels},
    {SettingId::volumetric_lighting_mode, L"KFSystemSettings.ini",
     L"SystemSettings", L"VolumetricLightingMode", SettingType::integer,
     0, 2, false, kQualityLevels},
    {SettingId::physx_level, L"KFEngine.ini", L"Engine.Engine",
     L"PhysXLevel", SettingType::integer, 0, 2, true, kGoreLevels},
    {SettingId::flex_invisible_frames_before_sleep, L"KFSystemSettings.ini",
     L"SystemSettings", L"FlexInvisibleFramesBeforeSleep",
     SettingType::integer, 0, 600, false, {}, 5},
    {SettingId::flex_distance_before_sleep, L"KFSystemSettings.ini",
     L"SystemSettings", L"FlexDistanceBeforeSleep", SettingType::real,
     0.0, 10000.0, false, {}, 100.0},
    {SettingId::sph_fluid_mipmap, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowSPHFluidMipmap", SettingType::boolean,
     0, 1, false},
    {SettingId::flex_rigid_collision_high_level, L"KFSystemSettings.ini",
     L"SystemSettings", L"FlexRigidBodiesCollisionAtHighLevel",
     SettingType::boolean, 0, 1, false},
    {SettingId::vertical_sync, L"KFSystemSettings.ini", L"SystemSettings",
     L"UseVsync", SettingType::boolean, 0, 1, false},
    {SettingId::one_frame_thread_lag, L"KFSystemSettings.ini",
     L"SystemSettings", L"OneFrameThreadLag", SettingType::boolean,
     0, 1, false},
    {SettingId::per_frame_sleep, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowPerFrameSleep", SettingType::boolean, 0, 1, false},
    {SettingId::per_frame_yield, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowPerFrameYield", SettingType::boolean, 0, 1, false},
    {SettingId::force_affinity, L"KFSystemSettings.ini", L"SystemSettings",
     L"ForceAffinity", SettingType::boolean, 0, 1, false},
    {SettingId::screen_percentage, L"KFSystemSettings.ini", L"SystemSettings",
     L"ScreenPercentage", SettingType::real, 50.0, 200.0, false, {}, 5.0},
    {SettingId::motion_blur_quality, L"KFSystemSettings.ini",
     L"SystemSettings", L"MotionBlurQuality", SettingType::integer,
     0, 2, true, kQualityLevels},
    {SettingId::compute_bloom, L"KFSystemSettings.ini", L"SystemSettings",
     L"UseComputeBloom", SettingType::boolean, 0, 1, false},
    {SettingId::compute_depth_of_field, L"KFSystemSettings.ini",
     L"SystemSettings", L"UseComputeDepthOfField", SettingType::boolean,
     0, 1, false},
    {SettingId::compute_motion_blur, L"KFSystemSettings.ini",
     L"SystemSettings", L"UseComputeMotionBlur", SettingType::boolean,
     0, 1, false},
    {SettingId::compute_ssao, L"KFSystemSettings.ini", L"SystemSettings",
     L"UseComputeSSAO", SettingType::boolean, 0, 1, false},
    {SettingId::compute_ssr, L"KFSystemSettings.ini", L"SystemSettings",
     L"UseComputeSSR", SettingType::boolean, 0, 1, false},
    {SettingId::hbao, L"KFSystemSettings.ini", L"SystemSettings",
     L"HBAO", SettingType::boolean, 0, 1, true},
    {SettingId::new_depth_of_field, L"KFSystemSettings.ini",
     L"SystemSettings", L"UseNewDOF", SettingType::boolean, 0, 1, false},
    {SettingId::per_object_shadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowPerObjectShadows", SettingType::boolean,
     0, 1, true},
    {SettingId::hardware_shadow_filtering, L"KFSystemSettings.ini",
     L"SystemSettings", L"bAllowHardwareShadowFiltering",
     SettingType::boolean, 0, 1, false},
    {SettingId::foreground_shadows_on_world, L"KFSystemSettings.ini",
     L"SystemSettings", L"bEnableForegroundShadowsOnWorld",
     SettingType::boolean, 0, 1, false},
    {SettingId::min_shadow_resolution, L"KFSystemSettings.ini",
     L"SystemSettings", L"MinShadowResolution", SettingType::integer,
     8, 1024, true, kMinShadowResolutionLevels},
    {SettingId::shadow_fade_resolution, L"KFSystemSettings.ini",
     L"SystemSettings", L"ShadowFadeResolution", SettingType::integer,
     8, 1024, true, kShadowFadeResolutionLevels},
    {SettingId::shadow_depth_bias, L"KFSystemSettings.ini", L"SystemSettings",
     L"ShadowDepthBias", SettingType::real, 0.0, 1.0, false, {}, 0.005},
    {SettingId::image_reflections, L"KFSystemSettings.ini", L"SystemSettings",
     L"AllowImageReflections", SettingType::boolean, 0, 1, false},
    {SettingId::image_reflection_shadowing, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowImageReflectionShadowing",
     SettingType::boolean, 0, 1, false},
    {SettingId::fractured_parts_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"NumFracturedPartsScale", SettingType::real,
     0.0, 2.0, false, {}, 0.05},
    {SettingId::fracture_direct_spawn_chance, L"KFSystemSettings.ini",
     L"SystemSettings", L"FractureDirectSpawnChanceScale", SettingType::real,
     0.0, 2.0, false, {}, 0.05},
    {SettingId::fracture_radial_spawn_chance, L"KFSystemSettings.ini",
     L"SystemSettings", L"FractureRadialSpawnChanceScale", SettingType::real,
     0.0, 2.0, false, {}, 0.05},
    {SettingId::downsampled_translucency, L"KFSystemSettings.ini",
     L"SystemSettings", L"bAllowDownsampledTranslucency",
     SettingType::boolean, 0, 1, false},
    {SettingId::max_filter_blur_samples, L"KFSystemSettings.ini",
     L"SystemSettings", L"MaxFilterBlurSampleCount", SettingType::integer,
     1, 64, false, {}, 1},
    {SettingId::motion_blur_static_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"MotionBlurStaticScale", SettingType::real,
     0.0, 10.0, false, {}, 0.1},
    {SettingId::motion_blur_dynamic_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"MotionBlurDynamicScale", SettingType::real,
     0.0, 10.0, false, {}, 0.1},
    {SettingId::override_weapon_depth_of_field, L"KFSystemSettings.ini",
     L"SystemSettings", L"OverrideDoFWeaponSettings", SettingType::boolean,
     0, 1, false},
    {SettingId::boolean_preshadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowBooleanPreshadows", SettingType::boolean,
     0, 1, false},
    {SettingId::foreground_preshadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowForegroundPreshadows", SettingType::boolean,
     0, 1, true},
    {SettingId::max_per_object_shadow_bounds, L"KFSystemSettings.ini",
     L"SystemSettings", L"MaxPrimBoundsForPerObjectShadows",
     SettingType::real, 0.0, 10000.0, false, {}, 50.0},
    {SettingId::foreground_projection_depth_bias, L"KFSystemSettings.ini",
     L"SystemSettings", L"ForegroundProjectionDepthBias", SettingType::real,
     0.0, 5.0, false, {}, 0.05},
    {SettingId::dynamic_lights, L"KFSystemSettings.ini", L"SystemSettings",
     L"DynamicLights", SettingType::boolean, 0, 1, false},
    {SettingId::composite_dynamic_lights, L"KFSystemSettings.ini",
     L"SystemSettings", L"CompositeDynamicLights", SettingType::boolean,
     0, 1, false},
    {SettingId::secondary_h_lighting, L"KFSystemSettings.ini",
     L"SystemSettings", L"SHSecondaryLighting", SettingType::boolean,
     0, 1, false},
    {SettingId::directional_lightmaps, L"KFSystemSettings.ini",
     L"SystemSettings", L"DirectionalLightmaps", SettingType::boolean,
     0, 1, false},
    {SettingId::motion_blur_pause, L"KFSystemSettings.ini", L"SystemSettings",
     L"MotionBlurPause", SettingType::boolean, 0, 1, false},
    {SettingId::motion_blur_skinning, L"KFSystemSettings.ini", L"SystemSettings",
     L"MotionBlurSkinning", SettingType::integer, 0, 1, false,
     kMotionBlurSkinningLevels},
    {SettingId::fog_volumes, L"KFSystemSettings.ini", L"SystemSettings",
     L"FogVolumes", SettingType::boolean, 0, 1, false},
    {SettingId::floating_point_render_targets, L"KFSystemSettings.ini",
     L"SystemSettings", L"FloatingPointRenderTargets", SettingType::boolean,
     0, 1, false},
    {SettingId::only_stream_in_textures, L"KFSystemSettings.ini",
     L"SystemSettings", L"OnlyStreamInTextures", SettingType::boolean,
     0, 1, false},
    {SettingId::upscale_screen_percentage, L"KFSystemSettings.ini",
     L"SystemSettings", L"UpscaleScreenPercentage", SettingType::boolean,
     0, 1, false},
    {SettingId::temporal_aa, L"KFSystemSettings.ini", L"SystemSettings",
     L"bAllowTemporalAA", SettingType::boolean, 0, 1, false},
    {SettingId::max_multi_samples, L"KFSystemSettings.ini", L"SystemSettings",
     L"MaxMultiSamples", SettingType::integer, 1, 8, false,
     kMultiSampleLevels},
    {SettingId::shadow_filter_quality_bias, L"KFSystemSettings.ini",
     L"SystemSettings", L"ShadowFilterQualityBias", SettingType::integer,
     -1, 1, false, kShadowFilterQualityLevels},
    {SettingId::min_pre_shadow_resolution, L"KFSystemSettings.ini",
     L"SystemSettings", L"MinPreShadowResolution", SettingType::integer,
     8, 1024, false, kMinShadowResolutionLevels},
    {SettingId::pre_shadow_fade_resolution, L"KFSystemSettings.ini",
     L"SystemSettings", L"PreShadowFadeResolution", SettingType::integer,
     1, 1024, false, {}, 1},
    {SettingId::pre_shadow_resolution_factor, L"KFSystemSettings.ini",
     L"SystemSettings", L"PreShadowResolutionFactor", SettingType::real,
     0.1, 2.0, false, {}, 0.05},
    {SettingId::shadow_fade_exponent, L"KFSystemSettings.ini",
     L"SystemSettings", L"ShadowFadeExponent", SettingType::real,
     0.0, 4.0, false, {}, 0.05},
    {SettingId::shadow_filter_radius, L"KFSystemSettings.ini",
     L"SystemSettings", L"ShadowFilterRadius", SettingType::real,
     0.0, 8.0, false, {}, 0.1},
    {SettingId::per_object_shadow_transition, L"KFSystemSettings.ini",
     L"SystemSettings", L"PerObjectShadowTransition", SettingType::real,
     0.0, 2000.0, false, {}, 10.0},
    {SettingId::per_scene_shadow_transition, L"KFSystemSettings.ini",
     L"SystemSettings", L"PerSceneShadowTransition", SettingType::real,
     0.0, 5000.0, false, {}, 25.0},
    {SettingId::branching_pcf_shadows, L"KFSystemSettings.ini",
     L"SystemSettings", L"bEnableBranchingPCFShadows", SettingType::boolean,
     0, 1, false},
    {SettingId::foreground_self_shadowing, L"KFSystemSettings.ini",
     L"SystemSettings", L"bEnableForegroundSelfShadowing", SettingType::boolean,
     0, 1, false},
    {SettingId::high_precision_gbuffers, L"KFSystemSettings.ini",
     L"SystemSettings", L"HighPrecisionGBuffers", SettingType::boolean,
     0, 1, false},
    {SettingId::tessellation_pixels_per_triangle, L"KFSystemSettings.ini",
     L"SystemSettings", L"TessellationAdaptivePixelsPerTriangle",
     SettingType::real, 1.0, 256.0, false, {}, 1.0},
    {SettingId::csm_split_penumbra_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"CSMSplitPenumbraScale", SettingType::real,
     0.0, 8.0, false, {}, 0.1},
    {SettingId::csm_split_soft_transition_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"CSMSplitSoftTransitionDistanceScale",
     SettingType::real, 0.0, 16.0, false, {}, 0.1},
    {SettingId::csm_split_depth_bias_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"CSMSplitDepthBiasScale", SettingType::real,
     0.0, 8.0, false, {}, 0.1},
    {SettingId::csm_minimum_fov, L"KFSystemSettings.ini", L"SystemSettings",
     L"CSMMinimumFOV", SettingType::real, 1.0, 180.0, false, {}, 1.0},
    {SettingId::csm_fov_round_factor, L"KFSystemSettings.ini",
     L"SystemSettings", L"CSMFOVRoundFactor", SettingType::real,
     0.0, 32.0, false, {}, 0.5},
    {SettingId::scene_capture_streaming_multiplier, L"KFSystemSettings.ini",
     L"SystemSettings", L"SceneCaptureStreamingMultiplier", SettingType::real,
     0.1, 4.0, false, {}, 0.1},
    {SettingId::instanced_rendering, L"KFSystemSettings.ini", L"SystemSettings",
     L"InstancedRendering", SettingType::boolean, 0, 1, false},
    {SettingId::summed_area_table_compute, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowSummedAreaTableCompute", SettingType::boolean,
     0, 1, false},
    {SettingId::histogram_techniques, L"KFSystemSettings.ini",
     L"SystemSettings", L"AllowHistogramTechniques", SettingType::boolean,
     0, 1, false},
    {SettingId::apex_destruction_chunk_islands, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexDestructionMaxChunkIslandCount",
     SettingType::integer, 0, 100000, false, {}, 100},
    {SettingId::apex_destruction_shape_count, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexDestructionMaxShapeCount", SettingType::integer,
     0, 100000, false, {}, 100},
    {SettingId::apex_destruction_chunk_separation_lod, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexDestructionMaxChunkSeparationLOD",
     SettingType::real, 0.0, 1.0, false, {}, 0.05},
    {SettingId::apex_destruction_actor_creates_per_frame,
     L"KFSystemSettings.ini", L"SystemSettings",
     L"ApexDestructionMaxActorCreatesPerFrame", SettingType::integer,
     -1, 10000, false, {}, 1},
    {SettingId::apex_destruction_fractures_per_frame,
     L"KFSystemSettings.ini", L"SystemSettings",
     L"ApexDestructionMaxFracturesProcessedPerFrame", SettingType::integer,
     -1, 10000, false, {}, 1},
    {SettingId::apex_destruction_sort_by_benefit, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexDestructionSortByBenefit", SettingType::boolean,
     0, 1, false},
    {SettingId::apex_clothing_frequency_window, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexClothingAvgSimFrequencyWindow",
     SettingType::integer, 1, 600, false, {}, 5},
    {SettingId::apex_clothing_async_cooking, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexClothingAllowAsyncCooking", SettingType::boolean,
     0, 1, false},
    {SettingId::apex_clothing_between_substeps, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexClothingAllowApexWorkBetweenSubsteps",
     SettingType::boolean, 0, 1, false},
    {SettingId::apex_clothing_async_fetch, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexClothingAsyncFetchResults", SettingType::boolean,
     0, 1, false},
    {SettingId::disable_dynamic_wakeup, L"KFSystemSettings.ini",
     L"SystemSettings", L"DisableCanBecomeDynamicWakeup", SettingType::boolean,
     0, 1, false},
    {SettingId::dynamic_collision_threshold, L"KFSystemSettings.ini",
     L"SystemSettings", L"MakeDynamicCollisionThreshold", SettingType::real,
     0.0, 10000.0, false, {}, 25.0},
    {SettingId::max_physics_substeps, L"KFGame.ini", L"Engine.WorldInfo",
     L"MaxPhysicsSubsteps", SettingType::integer, 1, 5, false,
     kPhysicsSubstepLevels},
    {SettingId::emitter_pool_scale, L"KFGame.ini", L"Engine.WorldInfo",
     L"EmitterPoolScale", SettingType::real, 0.25, 4.0, true, {}, 0.25},
    {SettingId::physics_chunk_override_chance, L"KFGame.ini",
     L"Engine.WorldInfo", L"ChanceOfPhysicsChunkOverride", SettingType::real,
     0.0, 1.0, false, {}, 0.05},
    {SettingId::physics_chunk_override_enabled, L"KFGame.ini",
     L"Engine.WorldInfo", L"bEnableChanceOfPhysicsChunkOverride",
     SettingType::boolean, 0, 1, false},
    {SettingId::fracture_explosion_velocity_scale, L"KFGame.ini",
     L"Engine.WorldInfo", L"FractureExplosionVelScale", SettingType::real,
     0.0, 4.0, false, {}, 0.1},
    {SettingId::limit_explosion_chunk_size, L"KFGame.ini",
     L"Engine.WorldInfo", L"bLimitExplosionChunkSize", SettingType::boolean,
     0, 1, false},
    {SettingId::limit_damage_chunk_size, L"KFGame.ini",
     L"Engine.WorldInfo", L"bLimitDamageChunkSize", SettingType::boolean,
     0, 1, false},
    {SettingId::max_explosion_chunk_size, L"KFGame.ini",
     L"Engine.WorldInfo", L"MaxExplosionChunkSize", SettingType::real,
     0.0, 10000.0, false, {}, 25.0},
    {SettingId::max_damage_chunk_size, L"KFGame.ini", L"Engine.WorldInfo",
     L"MaxDamageChunkSize", SettingType::real, 0.0, 10000.0, false, {}, 25.0},
    {SettingId::texture_streaming, L"KFEngine.ini", L"Engine.Engine",
     L"bUseTextureStreaming", SettingType::boolean, 0, 1, false},
    {SettingId::texture_pool_size, L"KFEngine.ini", L"TextureStreaming",
     L"PoolSize", SettingType::integer, 0, 16384, false, {}, 64},
    {SettingId::minimum_texture_resident_mips, L"KFEngine.ini",
     L"TextureStreaming", L"MinTextureResidentMipCount", SettingType::integer,
     1, 14, false, {}, 1},
    {SettingId::texture_async_defrag, L"KFEngine.ini", L"TextureStreaming",
     L"bEnableAsyncDefrag", SettingType::boolean, 0, 1, false},
    {SettingId::texture_async_reallocation, L"KFEngine.ini",
     L"TextureStreaming", L"bEnableAsyncReallocation", SettingType::boolean,
     0, 1, false},
    {SettingId::boost_player_textures, L"KFEngine.ini", L"TextureStreaming",
     L"BoostPlayerTextures", SettingType::real, 1.0, 4.0, false, {}, 0.25},
    {SettingId::max_particle_vertex_memory, L"KFGame.ini",
     L"KFGame.KFGameEngine", L"MaxParticleVertexMemory", SettingType::integer,
     0, 1048576, false, {}, 4096},
    {SettingId::apex_write_buffer_task, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexEnableWriteBufferTask", SettingType::boolean,
     0, 1, false},
    {SettingId::apex_render_resources_game_thread, L"KFSystemSettings.ini",
     L"SystemSettings", L"ApexUpdateRenderResourcesInGameThread",
     SettingType::boolean, 0, 1, false},
    {SettingId::gore_level, L"KFGame.ini", L"KFGame.KFGameEngine",
     L"GoreLevel", SettingType::integer, 0, 2, false, kGoreLevels},
    {SettingId::rigid_body_gravity_scale, L"KFGame.ini", L"Engine.WorldInfo",
     L"RBPhysicsGravityScaling", SettingType::real, 0.1, 2.0, false, {}, 0.05},
    {SettingId::fractured_mesh_weapon_damage, L"KFGame.ini",
     L"Engine.WorldInfo", L"FracturedMeshWeaponDamage", SettingType::real,
     0.0, 4.0, false, {}, 0.1},
    {SettingId::decal_lifetime, L"KFGame.ini", L"Engine.DecalManager",
     L"DecalLifeSpan", SettingType::real, 0.0, 120.0, false, {}, 1.0},
    {SettingId::background_level_streaming, L"KFGame.ini",
     L"KFGame.KFGameEngine", L"bUseBackgroundLevelStreaming",
     SettingType::boolean, 0, 1, false},
    {SettingId::max_occlusion_pixels_fraction, L"KFGame.ini",
     L"KFGame.KFGameEngine", L"MaxOcclusionPixelsFraction",
     SettingType::real, 0.0, 1.0, false, {}, 0.01},
    {SettingId::max_particle_resize, L"KFGame.ini", L"KFGame.KFGameEngine",
     L"MaxParticleResize", SettingType::integer, 0, 1048576, false, {}, 4096},
    {SettingId::max_particle_resize_warning, L"KFGame.ini",
     L"KFGame.KFGameEngine", L"MaxParticleResizeWarn", SettingType::integer,
     0, 1048576, false, {}, 4096},
    {SettingId::check_particle_render_size, L"KFEngine.ini", L"Engine.Engine",
     L"bCheckParticleRenderSize", SettingType::boolean, 0, 1, false},
    {SettingId::max_tracked_occlusion_increment, L"KFEngine.ini",
     L"Engine.Engine", L"MaxTrackedOcclusionIncrement", SettingType::real,
     0.0, 1.0, false, {}, 0.01},
    {SettingId::tracked_occlusion_step_size, L"KFEngine.ini",
     L"Engine.Engine", L"TrackedOcclusionStepSize", SettingType::real,
     0.0, 1.0, false, {}, 0.01},
    {SettingId::streaming_pause, L"KFEngine.ini", L"Engine.Engine",
     L"bUseStreamingPause", SettingType::boolean, 0, 1, false},
    {SettingId::stop_streaming_limit, L"KFEngine.ini", L"TextureStreaming",
     L"StopStreamingLimit", SettingType::integer, 0, 64, false, {}, 1},
    {SettingId::lightmap_streaming_factor, L"KFEngine.ini", L"TextureStreaming",
     L"LightmapStreamingFactor", SettingType::real, 0.0, 4.0, false, {}, 0.01},
    {SettingId::shadowmap_streaming_factor, L"KFEngine.ini", L"TextureStreaming",
     L"ShadowmapStreamingFactor", SettingType::real, 0.0, 4.0, false, {}, 0.01},
    {SettingId::streaming_lightmaps, L"KFEngine.ini", L"TextureStreaming",
     L"AllowStreamingLightmaps", SettingType::boolean, 0, 1, false},
    {SettingId::priority_streaming, L"KFEngine.ini", L"TextureStreaming",
     L"UsePriorityStreaming", SettingType::boolean, 0, 1, false},
    {SettingId::switching_streaming_system, L"KFEngine.ini", L"TextureStreaming",
     L"bAllowSwitchingStreamingSystem", SettingType::boolean, 0, 1, false},
    {SettingId::dynamic_streaming, L"KFEngine.ini", L"TextureStreaming",
     L"UseDynamicStreaming", SettingType::boolean, 0, 1, false},
    {SettingId::particle_percentage, L"KFEngine.ini",
     L"Engine.PhysicsLODVerticalEmitter", L"ParticlePercentage",
     SettingType::integer, 0, 100, false, {}, 5},
    {SettingId::shell_eject_lifetime, L"KFGame.ini", L"KFGame.KFMuzzleFlash",
     L"ShellEjectLifetime", SettingType::real, 0.0, 60.0, true, {}, 1.0},
    {SettingId::spray_actor_lights, L"KFGame.ini", L"KFGame.KFSprayActor",
     L"bAllowSprayLights", SettingType::boolean, 0, 1, true},
    {SettingId::pilot_lights, L"KFGame.ini", L"KFGame.KFWeap_FlameBase",
     L"bArePilotLightsAllowed", SettingType::boolean, 0, 1, true},
    {SettingId::footstep_sounds, L"KFGame.ini", L"KFGame.KFPawn",
     L"bAllowFootstepSounds", SettingType::boolean, 0, 1, false},
    {SettingId::kinematic_update_distance_scale, L"KFSystemSettings.ini",
     L"SystemSettings", L"KinematicUpdateDistFactorScale", SettingType::real,
     1.0, 3.0, false, {}, 0.1},
    {SettingId::always_on_physics, L"KFGame.ini", L"KFGame.KFPawn",
     L"bAllowAlwaysOnPhysics", SettingType::boolean, 0, 1, false},
    {SettingId::override_map_whole_scene_shadow, L"KFSystemSettings.ini",
     L"SystemSettings", L"bOverrideMapWholeSceneDominantShadowSetting",
     SettingType::boolean, 0, 1, true},
}};

std::wstring lower(std::wstring value) {
    for (auto& character : value) character = std::towlower(character);
    return value;
}

std::wstring serialize_real(double value) {
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6) << value;
    auto text = output.str();
    while (text.size() > 2 && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.push_back(L'0');
    return text;
}

bool allowed_integer(const SettingDefinition& definition, int value) {
    return definition.allowed_integers.empty() ||
           std::ranges::find(definition.allowed_integers, value) !=
               definition.allowed_integers.end();
}

}  // namespace

const SettingDefinition* find_setting(SettingId id) noexcept {
    for (const auto& setting : kSettings) {
        if (setting.id == id) return &setting;
    }
    return nullptr;
}

std::span<const SettingDefinition> all_settings() noexcept { return kSettings; }

std::optional<SettingValue> parse_setting_value(
    const SettingDefinition& definition, std::wstring_view text) {
    try {
        if (definition.type == SettingType::boolean) {
            const auto normalized = lower(std::wstring{text});
            if (normalized == L"true" || normalized == L"1") return SettingValue{true};
            if (normalized == L"false" || normalized == L"0") return SettingValue{false};
            return std::nullopt;
        }
        std::size_t consumed = 0;
        const double number = std::stod(std::wstring{text}, &consumed);
        if (consumed != text.size() || !std::isfinite(number) ||
            number < definition.minimum || number > definition.maximum) {
            return std::nullopt;
        }
        if (definition.type == SettingType::real) return SettingValue{number};
        if (std::floor(number) != number) return std::nullopt;
        const auto integer = static_cast<int>(number);
        if (!allowed_integer(definition, integer)) return std::nullopt;
        return SettingValue{integer};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::wstring> serialize_setting_value(
    const SettingDefinition& definition, const SettingValue& value) {
    if (definition.type == SettingType::boolean) {
        const auto* boolean = std::get_if<bool>(&value);
        return boolean ? std::optional<std::wstring>{*boolean ? L"True" : L"False"}
                       : std::nullopt;
    }
    if (definition.type == SettingType::real) {
        const auto* real = std::get_if<double>(&value);
        if (!real || !std::isfinite(*real) || *real < definition.minimum ||
            *real > definition.maximum) {
            return std::nullopt;
        }
        return serialize_real(*real);
    }
    const auto* integer = std::get_if<int>(&value);
    if (!integer || *integer < definition.minimum ||
        *integer > definition.maximum || !allowed_integer(definition, *integer)) {
        return std::nullopt;
    }
    return std::to_wstring(*integer);
}

std::optional<SettingValue> step_setting_value(
    const SettingDefinition& definition, const SettingValue& value,
    int direction) {
    if (direction != -1 && direction != 1) return std::nullopt;
    if (!serialize_setting_value(definition, value)) return std::nullopt;
    if (definition.type == SettingType::boolean) {
        return SettingValue{!std::get<bool>(value)};
    }
    if (definition.type == SettingType::integer) {
        const int current = std::get<int>(value);
        if (!definition.allowed_integers.empty()) {
            const auto found = std::ranges::find(
                definition.allowed_integers, current);
            if (found == definition.allowed_integers.end()) return std::nullopt;
            const auto index = static_cast<std::ptrdiff_t>(
                found - definition.allowed_integers.begin());
            const auto next = std::clamp<std::ptrdiff_t>(
                index + direction, 0,
                static_cast<std::ptrdiff_t>(definition.allowed_integers.size() - 1));
            return SettingValue{definition.allowed_integers[
                static_cast<std::size_t>(next)]};
        }
        const int minimum = static_cast<int>(definition.minimum);
        const int maximum = static_cast<int>(definition.maximum);
        const int step = definition.editor_step > 0.0
            ? static_cast<int>(definition.editor_step) : 1;
        return SettingValue{std::clamp(
            current + direction * step, minimum, maximum)};
    }
    const double step = definition.editor_step > 0.0
        ? definition.editor_step : 0.1;
    const double current = std::get<double>(value);
    const double scale = 1.0 / step;
    const double next = std::clamp(
        std::round((current + static_cast<double>(direction) * step) * scale) /
            scale,
        definition.minimum, definition.maximum);
    SettingValue result{next};
    return serialize_setting_value(definition, result)
        ? std::optional<SettingValue>{result} : std::nullopt;
}

}  // namespace kf2::config
