#include <cstdlib>
#include <iostream>
#include <set>
#include <filesystem>
#include <fstream>
#include <Windows.h>

#include "kf2/config/setting_catalog.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::config;
    CHECK(setting_token(SettingId::corpse_limit) == "MaxDeadBodies");
    CHECK(find_setting_by_token("DynamicShadows") != nullptr);
    CHECK(find_setting_by_token("MinSmoothedFrameRate") != nullptr);
    CHECK(find_setting_by_token("ParticleLODBias") != nullptr);
    CHECK(find_setting_by_token("SkeletalMeshLODBias") != nullptr);
    CHECK(find_setting_by_token("MaxDrawDistanceScale") != nullptr);
    CHECK(find_setting_by_token("ShouldCorpseCollideWithLiving") != nullptr);
    CHECK(find_setting_by_token("bAllowRagdollAndGoreOnDeadBodies") != nullptr);
    CHECK(find_setting_by_token("DepthOfField") != nullptr);
    CHECK(find_setting_by_token("bAllowLightShafts") != nullptr);
    CHECK(find_setting_by_token("LensFlares") != nullptr);
    CHECK(find_setting_by_token("AllowRadialBlur") != nullptr);
    CHECK(find_setting_by_token("bAllowSeparateTranslucency") != nullptr);
    CHECK(find_setting_by_token("bAllowFracturedDamage") != nullptr);
    CHECK(find_setting_by_token("MotionBlur") != nullptr);
    CHECK(find_setting_by_token("PostProcessAA") != nullptr);
    CHECK(find_setting_by_token("DistanceFog") != nullptr);
    CHECK(find_setting_by_token("FilteredDistortion") != nullptr);
    CHECK(find_setting_by_token("UnbatchedDecals") != nullptr);
    CHECK(find_setting_by_token("bAllowWholeSceneDominantShadows") != nullptr);
    CHECK(find_setting_by_token("bUseConservativeShadowBounds") != nullptr);
    CHECK(find_setting_by_token("MaxAnisotropy") != nullptr);
    CHECK(find_setting_by_token("bAllowPostprocessMLAA") != nullptr);
    CHECK(find_setting_by_token("AllowPersistentSplats") != nullptr);
    CHECK(find_setting_by_token("AllowScreenSpaceReflections") != nullptr);
    CHECK(find_setting_by_token("AllowVariableBlurReflections") != nullptr);
    CHECK(find_setting_by_token("AllowSubsurfaceScattering") != nullptr);
    CHECK(find_setting_by_token("AllowLightFunctions") != nullptr);
    CHECK(find_setting_by_token("LightCones") != nullptr);
    CHECK(find_setting_by_token("DestructionLifetimeScale") != nullptr);
    CHECK(find_setting_by_token("bAllowExplosionLights") != nullptr);
    CHECK(find_setting_by_token("BloodSplatSize") != nullptr);
    CHECK(find_setting_by_token("WholeSceneShadowCutoffDistance") != nullptr);
    CHECK(find_setting_by_token("AllowGroupedPerObjectShadows") != nullptr);
    CHECK(find_setting_by_token("DepthPrepass") != nullptr);
    CHECK(find_setting_by_token("BloomQuality") != nullptr);
    CHECK(find_setting_by_token("VolumetricLightingMode") != nullptr);
    CHECK(find_setting_by_token("FlexInvisibleFramesBeforeSleep") != nullptr);
    CHECK(find_setting_by_token("FlexDistanceBeforeSleep") != nullptr);
    CHECK(find_setting_by_token("AllowSPHFluidMipmap") != nullptr);
    CHECK(find_setting_by_token("FlexRigidBodiesCollisionAtHighLevel") != nullptr);
    const auto* physx_level = find_setting_by_token("PhysXLevel");
    CHECK(physx_level != nullptr);
    CHECK(physx_level->relative_path == L"KFEngine.ini");
    CHECK(physx_level->section == L"Engine.Engine");
    CHECK(parse_setting_value(*physx_level, L"0").has_value());
    CHECK(parse_setting_value(*physx_level, L"2").has_value());
    CHECK(!parse_setting_value(*physx_level, L"3").has_value());
    CHECK(find_setting_by_token("UseVsync") != nullptr);
    CHECK(find_setting_by_token("OneFrameThreadLag") != nullptr);
    CHECK(find_setting_by_token("ForceAffinity") != nullptr);
    CHECK(find_setting_by_token("ScreenPercentage") != nullptr);
    CHECK(find_setting_by_token("UseComputeSSR") != nullptr);
    CHECK(find_setting_by_token("AllowPerObjectShadows") != nullptr);
    CHECK(find_setting_by_token("ShadowDepthBias") != nullptr);
    CHECK(find_setting_by_token("NumFracturedPartsScale") != nullptr);
    CHECK(find_setting_by_token("bAllowDownsampledTranslucency") != nullptr);
    CHECK(find_setting_by_token("OverrideDoFWeaponSettings") != nullptr);
    CHECK(find_setting_by_token("ApexDestructionMaxChunkIslandCount") != nullptr);
    CHECK(find_setting_by_token("ApexClothingAllowApexWorkBetweenSubsteps") != nullptr);
    CHECK(find_setting_by_token("bEnableBranchingPCFShadows") != nullptr);
    CHECK(find_setting_by_token("FloatingPointRenderTargets") != nullptr);
    CHECK(find_setting_by_token("MaxPhysicsSubsteps") != nullptr);
    CHECK(find_setting_by_token("EmitterPoolScale") != nullptr);
    CHECK(find_setting_by_token("bEnableChanceOfPhysicsChunkOverride") != nullptr);
    CHECK(find_setting_by_token("bUseTextureStreaming") != nullptr);
    CHECK(find_setting_by_token("PoolSize") != nullptr);
    CHECK(find_setting_by_token("MemoryMargin") != nullptr);
    CHECK(find_setting_by_token("HysteresisLimit") != nullptr);
    CHECK(find_setting_by_token("bPhysicsAsyncScene") != nullptr);
    CHECK(find_setting_by_token("bEnableAsyncScene") != nullptr);
    CHECK(find_setting_by_token("MaxParticleVertexMemory") != nullptr);
    CHECK(find_setting_by_token("ApexEnableWriteBufferTask") != nullptr);
    CHECK(find_setting_by_token("GoreLevel") != nullptr);
    CHECK(find_setting_by_token("RBPhysicsGravityScaling") != nullptr);
    CHECK(find_setting_by_token("DecalLifeSpan") != nullptr);
    CHECK(find_setting_by_token("bUseBackgroundLevelStreaming") != nullptr);
    CHECK(find_setting_by_token("MaxOcclusionPixelsFraction") != nullptr);
    CHECK(find_setting_by_token("MaxParticleResize") != nullptr);
    CHECK(find_setting_by_token("bCheckParticleRenderSize") != nullptr);
    CHECK(find_setting_by_token("MaxTrackedOcclusionIncrement") != nullptr);
    CHECK(find_setting_by_token("bUseStreamingPause") != nullptr);
    CHECK(find_setting_by_token("LightmapStreamingFactor") != nullptr);
    CHECK(find_setting_by_token("UsePriorityStreaming") != nullptr);
    CHECK(find_setting_by_token("UseDynamicStreaming") != nullptr);
    CHECK(find_setting_by_token("ParticlePercentage") != nullptr);
    CHECK(find_setting_by_token("ShellEjectLifetime") != nullptr);
    CHECK(find_setting_by_token("bAllowSprayLights") != nullptr);
    CHECK(find_setting_by_token("bArePilotLightsAllowed") != nullptr);
    CHECK(find_setting_by_token("bAllowFootstepSounds") != nullptr);
    CHECK(find_setting_by_token("KinematicUpdateDistFactorScale") != nullptr);
    CHECK(find_setting_by_token("bAllowAlwaysOnPhysics") != nullptr);
    CHECK(find_setting_by_token(
        "bOverrideMapWholeSceneDominantShadowSetting") != nullptr);
    CHECK(find_setting_by_token("Unknown") == nullptr);
    std::size_t adaptive_protected_count = 0;
    std::set<SettingCategory> categories;
    for (const auto& setting : all_settings()) {
        if (!setting.adaptive_allowed) ++adaptive_protected_count;
        categories.insert(setting_category(setting.id));
        CHECK(!setting_category_label(setting.id).empty());
    }
    CHECK(adaptive_protected_count == 146);
    CHECK(categories.size() == all_setting_categories().size());
    CHECK(all_settings().size() == 217);
    const auto* physics_substeps = find_setting_by_token("MaxPhysicsSubsteps");
    CHECK(physics_substeps != nullptr);
    CHECK(!physics_substeps->adaptive_allowed);
    CHECK(parse_setting_value(*physics_substeps, L"1").has_value());
    CHECK(parse_setting_value(*physics_substeps, L"5").has_value());
    CHECK(!parse_setting_value(*physics_substeps, L"0").has_value());
    CHECK(!parse_setting_value(*physics_substeps, L"6").has_value());
    const auto* gore_level = find_setting_by_token("GoreLevel");
    CHECK(gore_level != nullptr);
    CHECK(!gore_level->adaptive_allowed);
    CHECK(parse_setting_value(*gore_level, L"0").has_value());
    CHECK(parse_setting_value(*gore_level, L"2").has_value());
    CHECK(!parse_setting_value(*gore_level, L"3").has_value());
    const auto* particle_percentage = find_setting_by_token("ParticlePercentage");
    CHECK(particle_percentage != nullptr);
    CHECK(parse_setting_value(*particle_percentage, L"0").has_value());
    CHECK(parse_setting_value(*particle_percentage, L"100").has_value());
    CHECK(!parse_setting_value(*particle_percentage, L"101").has_value());
    const auto* shell_lifetime = find_setting_by_token("ShellEjectLifetime");
    CHECK(shell_lifetime != nullptr);
    CHECK(parse_setting_value(*shell_lifetime, L"20.0").has_value());
    CHECK(!parse_setting_value(*shell_lifetime, L"61.0").has_value());
    const auto* kinematic =
        find_setting_by_token("KinematicUpdateDistFactorScale");
    CHECK(kinematic != nullptr);
    CHECK(parse_setting_value(*kinematic, L"1.3").has_value());
    CHECK(parse_setting_value(*kinematic, L"3.0").has_value());
    CHECK(!parse_setting_value(*kinematic, L"0.9").has_value());

    const auto root = std::filesystem::temp_directory_path() /
        (std::wstring{L"kf2-manual-catalog-"} +
         std::to_wstring(static_cast<unsigned long>(GetCurrentProcessId())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::map<std::filesystem::path, std::string> files;
    for (const auto& definition : all_settings()) {
        if (definition.insert_if_missing) continue;
        auto& bytes = files[definition.relative_path];
        bytes += "[";
        for (const wchar_t character : definition.section) bytes.push_back(
            static_cast<char>(character));
        bytes += "]\n";
        for (const wchar_t character : definition.key) bytes.push_back(
            static_cast<char>(character));
        bytes += "=";
        const SettingValue value = definition.type == SettingType::boolean
            ? SettingValue{true} : definition.type == SettingType::integer
                ? SettingValue{static_cast<int>(definition.minimum)}
                : SettingValue{definition.minimum};
        const auto text = serialize_setting_value(definition, value);
        for (const wchar_t character : *text) bytes.push_back(
            static_cast<char>(character));
        bytes += "\n";
    }
    for (const auto& [path, bytes] : files) {
        std::ofstream output(root / path, std::ios::binary);
        output << bytes;
    }
    const auto catalog = read_catalog_values(root);
    CHECK(catalog.has_value());
    CHECK(catalog.value().size() == all_settings().size() - 2);
    const auto linked_alias = root.parent_path() / L"KFEngine-linked.ini";
    CHECK(CreateHardLinkW(linked_alias.c_str(),
                          (root / L"KFEngine.ini").c_str(), nullptr) != FALSE);
    CHECK(!read_catalog_values(root).has_value());
    std::filesystem::remove(linked_alias);
    std::filesystem::remove_all(root);
    return EXIT_SUCCESS;
}
