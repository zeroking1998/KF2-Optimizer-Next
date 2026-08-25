#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <variant>

#include "kf2/config/config_preview.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::config;
    auto engine = IniDocument::parse("[Engine.Engine]\r\nGameEngine=KFGame.KFGameEngine\r\n");
    CHECK(engine.has_value());
    std::map<std::filesystem::path, IniDocument> documents;
    documents.emplace(L"KFEngine.ini", std::move(engine.value()));
    auto game = IniDocument::parse(
        "[KFGame.KFGameEngine]\r\n"
        "bSmoothFrameRate=TRUE\r\n"
        "MinSmoothedFrameRate=22.000000\r\n"
        "MaxSmoothedFrameRate=62.000000\r\n"
        "[KFGame.KFGoreManager]\r\n"
        "MaxBloodEffects=25\r\n"
        "BodyWoundDecalLifetime=30.000000\r\n"
        "BloodSplatterLifetime=10.000000\r\n"
        "BloodPoolLifetime=20.000000\r\n"
        "GoreFXLifetimeMultiplier=1.200000\r\n"
        "MaxPersistentSplatsPerFrame=100\r\n");
    CHECK(game.has_value());
    documents.emplace(L"KFGame.ini", std::move(game.value()));
    auto system = IniDocument::parse(
        "[SystemSettings]\r\n"
        "AllowSecondaryBloodEffects=True\r\n"
        "StaticDecals=True\r\n"
        "DynamicDecals=True\r\n"
        "DecalCullDistanceScale=1.000000\r\n"
        "DynamicShadows=True\r\n"
        "LightEnvironmentShadows=True\r\n"
        "AmbientOcclusion=True\r\n"
        "Bloom=True\r\n"
        "Distortion=True\r\n"
        "DropParticleDistortion=False\r\n"
        "bAllowHighQualityMaterials=True\r\n"
        "DetailMode=2\r\n"
        "MaxShadowResolution=2048\r\n"
        "MaxWholeSceneDominantShadowResolution=2048\r\n"
        "ShadowTexelsPerPixel=2.000000\r\n"
        "FractureCullDistanceScale=1.000000\r\n"
        "ParticleLODBias=0\r\n"
        "MaxDrawDistanceScale=1.000000\r\n");
    CHECK(system.has_value());
    documents.emplace(L"KFSystemSettings.ini", std::move(system.value()));

    kf2::game::GameInstallation installation;
    installation.config_root = L"C:\\KF2Config";
    const auto preview = build_preview(
        installation,
        {{SettingId::target_fps, SettingValue{120}, ChangeSource::adaptive,
          L"Smart target"},
         {SettingId::target_fps, SettingValue{90}, ChangeSource::explicit_user,
          L"Manual target"}},
        documents);
    CHECK(preview.has_value());
    CHECK(preview.value().items.size() == 1);
    CHECK(preview.value().items[0].relative_path == L"KFGame.ini");
    CHECK(std::get<int>(preview.value().items[0].before) == 62);
    CHECK(std::get<int>(preview.value().items[0].after) == 90);
    CHECK(preview.value().items[0].source == ChangeSource::explicit_user);
    CHECK(preview.value().items[0].reason == L"Manual target");
    CHECK(preview.value().items[0].state == PreviewState::ready);

    const auto inserted_startup_physics = build_preview(
        installation,
        {{SettingId::physics_async_scene, SettingValue{true},
          ChangeSource::explicit_user, L"Protected startup default"},
         {SettingId::enable_async_scene, SettingValue{true},
          ChangeSource::explicit_user, L"Protected startup default"}},
        documents);
    CHECK(inserted_startup_physics.has_value());
    CHECK(inserted_startup_physics.value().items.size() == 2);
    CHECK(!inserted_startup_physics.value().items[0].existed_before);
    CHECK(!inserted_startup_physics.value().items[1].existed_before);
    const auto inserted_engine = std::find_if(
        inserted_startup_physics.value().files.begin(),
        inserted_startup_physics.value().files.end(),
        [](const PreviewFile& file) {
            return file.relative_path == L"KFEngine.ini";
        });
    CHECK(inserted_engine != inserted_startup_physics.value().files.end());
    CHECK(inserted_engine->proposed_bytes.find("[Engine.Physics]") !=
          std::string::npos);
    CHECK(inserted_engine->proposed_bytes.find("bPhysicsAsyncScene=True") !=
          std::string::npos);
    CHECK(inserted_engine->proposed_bytes.find("bEnableAsyncScene=True") !=
          std::string::npos);

    const auto valid_smoothing_range = build_preview(
        installation,
        {{SettingId::minimum_smooth_frame_rate, SettingValue{30},
          ChangeSource::explicit_user, L"Explicit smoothing floor"},
         {SettingId::target_fps, SettingValue{120}, ChangeSource::explicit_user,
          L"Manual smoothing ceiling"}},
        documents);
    CHECK(valid_smoothing_range.has_value());
    CHECK(valid_smoothing_range.value().items.size() == 2);

    const auto invalid_smoothing_range = build_preview(
        installation,
        {{SettingId::minimum_smooth_frame_rate, SettingValue{100},
          ChangeSource::explicit_user, L"Invalid floor"},
         {SettingId::target_fps, SettingValue{90}, ChangeSource::explicit_user,
          L"Invalid ceiling"}},
        documents);
    CHECK(!invalid_smoothing_range.has_value());
    CHECK(invalid_smoothing_range.error().code ==
          kf2::ErrorCode::invalid_argument);

    const auto adaptive_smoothing_floor = build_preview(
        installation,
        {{SettingId::minimum_smooth_frame_rate, SettingValue{30},
          ChangeSource::adaptive, L"Keep the floor below the target"}},
        documents);
    CHECK(adaptive_smoothing_floor.has_value());
    CHECK(adaptive_smoothing_floor.value().items.size() == 1);
    CHECK(std::get<int>(
              adaptive_smoothing_floor.value().items[0].after) == 30);

    const auto cosmetic = build_preview(
        installation,
        {{SettingId::blood_effect_limit, SettingValue{20}, ChangeSource::adaptive,
          L"blood effects"},
         {SettingId::body_wound_decal_lifetime, SettingValue{20},
          ChangeSource::adaptive, L"wound lifetime"},
         {SettingId::blood_splatter_lifetime, SettingValue{8},
          ChangeSource::adaptive, L"splatter lifetime"},
         {SettingId::blood_pool_lifetime, SettingValue{15},
          ChangeSource::adaptive, L"pool lifetime"},
         {SettingId::gore_lifetime_multiplier, SettingValue{0.75},
          ChangeSource::adaptive, L"gore lifetime"},
         {SettingId::persistent_splats_per_frame, SettingValue{50},
          ChangeSource::adaptive, L"splats"},
         {SettingId::secondary_blood_effects, SettingValue{false},
          ChangeSource::adaptive, L"secondary blood"},
         {SettingId::dynamic_decals, SettingValue{false},
          ChangeSource::adaptive, L"dynamic decals"},
         {SettingId::decal_cull_distance_scale, SettingValue{0.5},
          ChangeSource::adaptive, L"decal distance"},
         {SettingId::dynamic_shadows, SettingValue{false},
          ChangeSource::adaptive, L"dynamic shadows"},
         {SettingId::drop_particle_distortion, SettingValue{true},
          ChangeSource::adaptive, L"particle distortion"},
         {SettingId::max_shadow_resolution, SettingValue{512},
          ChangeSource::adaptive, L"shadow resolution"},
         {SettingId::shadow_texels_per_pixel, SettingValue{0.9},
          ChangeSource::adaptive, L"shadow density"}},
        documents);
    CHECK(cosmetic.has_value());
    CHECK(cosmetic.value().items.size() == 13);
    CHECK(cosmetic.value().files.size() == 2);
    const auto game_file = std::find_if(
        cosmetic.value().files.begin(), cosmetic.value().files.end(),
        [](const PreviewFile& file) { return file.relative_path == L"KFGame.ini"; });
    const auto system_file = std::find_if(
        cosmetic.value().files.begin(), cosmetic.value().files.end(),
        [](const PreviewFile& file) {
            return file.relative_path == L"KFSystemSettings.ini";
        });
    CHECK(game_file != cosmetic.value().files.end());
    CHECK(system_file != cosmetic.value().files.end());
    CHECK(game_file->proposed_bytes.find("MaxBloodEffects=20") != std::string::npos);
    CHECK(game_file->proposed_bytes.find(
              "BodyWoundDecalLifetime=20") != std::string::npos);
    CHECK(game_file->proposed_bytes.find(
              "BloodSplatterLifetime=8") != std::string::npos);
    CHECK(game_file->proposed_bytes.find(
              "BloodPoolLifetime=15") != std::string::npos);
    CHECK(game_file->proposed_bytes.find(
              "GoreFXLifetimeMultiplier=0.75") != std::string::npos);
    CHECK(game_file->proposed_bytes.find(
              "MaxPersistentSplatsPerFrame=50") != std::string::npos);
    CHECK(system_file->proposed_bytes.find(
              "AllowSecondaryBloodEffects=False") != std::string::npos);
    CHECK(system_file->proposed_bytes.find("DynamicDecals=False") !=
          std::string::npos);
    CHECK(system_file->proposed_bytes.find("DecalCullDistanceScale=0.5") !=
          std::string::npos);
    CHECK(system_file->proposed_bytes.find("DynamicShadows=False") !=
          std::string::npos);
    CHECK(system_file->proposed_bytes.find("DropParticleDistortion=True") !=
          std::string::npos);
    CHECK(system_file->proposed_bytes.find("MaxShadowResolution=512") !=
          std::string::npos);
    CHECK(system_file->proposed_bytes.find("ShadowTexelsPerPixel=0.9") !=
          std::string::npos);

    const auto blocked_sensitive_smart = build_preview(
        installation,
        {{SettingId::max_draw_distance_scale, SettingValue{1.1}, ChangeSource::adaptive,
          L"must remain manual"}},
        documents);
    CHECK(!blocked_sensitive_smart.has_value());
    CHECK(blocked_sensitive_smart.error().code == kf2::ErrorCode::access_denied);

    const auto allowed_sensitive_manual = build_preview(
        installation,
        {{SettingId::max_draw_distance_scale, SettingValue{1.1}, ChangeSource::explicit_user,
          L"explicit user choice"}},
        documents);
    CHECK(allowed_sensitive_manual.has_value());
    CHECK(allowed_sensitive_manual.value().items.size() == 1);
    CHECK(allowed_sensitive_manual.value().items[0].source ==
          ChangeSource::explicit_user);

    const auto unchanged = build_preview(
        installation,
        {{SettingId::target_fps, SettingValue{62},
          ChangeSource::explicit_user, L"same"}},
        documents);
    CHECK(unchanged.has_value());
    CHECK(unchanged.value().items[0].state == PreviewState::unchanged);

    const auto out_of_range = build_preview(
        installation,
        {{SettingId::target_fps, SettingValue{1000},
          ChangeSource::explicit_user, L"bad"}},
        documents);
    CHECK(!out_of_range.has_value());
    CHECK(out_of_range.error().code == kf2::ErrorCode::invalid_argument);

    const auto wrong_type = build_preview(
        installation,
        {{SettingId::target_fps, SettingValue{true},
          ChangeSource::explicit_user, L"bad"}},
        documents);
    CHECK(!wrong_type.has_value());

    const auto unsupported = build_preview(
        installation,
        {{SettingId::unverified_effect_profile, SettingValue{1},
          ChangeSource::explicit_user, L"unknown"}},
        documents);
    CHECK(!unsupported.has_value());

    documents.clear();
    const auto missing = build_preview(
        installation,
        {{SettingId::target_fps, SettingValue{90},
          ChangeSource::explicit_user, L"missing"}},
        documents);
    CHECK(!missing.has_value());
    CHECK(missing.error().code == kf2::ErrorCode::not_found);
    return EXIT_SUCCESS;
}
