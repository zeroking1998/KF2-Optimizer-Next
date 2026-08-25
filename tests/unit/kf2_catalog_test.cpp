#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

#include "kf2/config/kf2_catalog.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::config;
    const auto settings = all_settings();
    CHECK(!settings.empty());
    std::set<SettingId> ids;
    std::set<std::pair<std::wstring, std::wstring>> targets;
    for (const auto& setting : settings) {
        CHECK(ids.insert(setting.id).second);
        CHECK(find_setting(setting.id) == &setting);
        CHECK(setting.relative_path == L"KFEngine.ini" ||
              setting.relative_path == L"KFGame.ini" ||
              setting.relative_path == L"KFSystemSettings.ini");
        CHECK(!setting.section.empty());
        CHECK(!setting.key.empty());
        CHECK(targets.insert({setting.section, setting.key}).second);
        CHECK(std::isfinite(setting.minimum));
        CHECK(std::isfinite(setting.maximum));
        CHECK(setting.minimum <= setting.maximum);
        CHECK(setting.editor_step >= 0.0);
        CHECK(setting.editor_step == 0.0 ||
              setting.type == SettingType::real ||
              (setting.type == SettingType::integer &&
               std::floor(setting.editor_step) == setting.editor_step));
        for (const int allowed : setting.allowed_integers) {
            CHECK(setting.type == SettingType::integer);
            CHECK(allowed >= setting.minimum && allowed <= setting.maximum);
        }

        SettingValue low;
        SettingValue high;
        if (setting.type == SettingType::boolean) {
            CHECK(setting.minimum == 0 && setting.maximum == 1);
            low = false;
            high = true;
        } else if (setting.type == SettingType::integer) {
            CHECK(std::floor(setting.minimum) == setting.minimum);
            CHECK(std::floor(setting.maximum) == setting.maximum);
            low = static_cast<int>(setting.minimum);
            high = static_cast<int>(setting.maximum);
        } else {
            low = setting.minimum;
            high = setting.maximum;
        }
        for (const auto& value : {low, high}) {
            const auto serialized = serialize_setting_value(setting, value);
            CHECK(serialized.has_value());
            const auto parsed = parse_setting_value(setting, *serialized);
            CHECK(parsed.has_value());
            CHECK(*parsed == value);
        }
    }
    const auto* anisotropy = find_setting(SettingId::max_anisotropy);
    CHECK(anisotropy != nullptr);
    CHECK(anisotropy->allowed_integers.size() == 5);
    for (const int value : {1, 2, 4, 8, 16}) {
        CHECK(serialize_setting_value(*anisotropy, value).has_value());
        CHECK(parse_setting_value(*anisotropy, std::to_wstring(value)).has_value());
    }
    for (const int value : {0, 3, 6, 11, 17}) {
        CHECK(!serialize_setting_value(*anisotropy, value).has_value());
        CHECK(!parse_setting_value(*anisotropy, std::to_wstring(value)).has_value());
    }
    CHECK(std::get<int>(*step_setting_value(*anisotropy, 4, 1)) == 8);
    CHECK(std::get<int>(*step_setting_value(*anisotropy, 4, -1)) == 2);
    CHECK(std::get<int>(*step_setting_value(*anisotropy, 16, 1)) == 16);
    CHECK(!step_setting_value(*anisotropy, 4, 0).has_value());
    const auto* target = find_setting(SettingId::target_fps);
    CHECK(target != nullptr);
    CHECK(target->relative_path == L"KFGame.ini");
    CHECK(target->section == L"KFGame.KFGameEngine");
    CHECK(target->minimum == 30);
    CHECK(target->maximum == 240);
    CHECK(target->editor_step == 1);
    CHECK(std::get<int>(*step_setting_value(*target, 60, 1)) == 61);
    CHECK(std::get<int>(*step_setting_value(*target, 60, -1)) == 59);
    for (int value = 30; value <= 240; ++value) {
        CHECK(parse_setting_value(*target, std::to_wstring(value)).has_value());
    }
    CHECK(!parse_setting_value(*target, L"241").has_value());
    const auto* corpses = find_setting(SettingId::corpse_limit);
    CHECK(corpses != nullptr);
    CHECK(corpses->minimum == 4);
    CHECK(!parse_setting_value(*corpses, L"3").has_value());
    for (int value = 4; value <= 2000; ++value) {
        CHECK(parse_setting_value(*corpses, std::to_wstring(value)).has_value());
    }
    CHECK(!parse_setting_value(*corpses, L"2001").has_value());
    const auto* wounds = find_setting(SettingId::wound_decal_limit);
    CHECK(wounds != nullptr);
    CHECK(wounds->minimum == 5);
    CHECK(!parse_setting_value(*wounds, L"4").has_value());
    const auto* minimum = find_setting(
        SettingId::minimum_smooth_frame_rate);
    CHECK(minimum != nullptr);
    CHECK(minimum->relative_path == L"KFGame.ini");
    CHECK(minimum->section == L"KFGame.KFGameEngine");
    CHECK(minimum->key == L"MinSmoothedFrameRate");
    CHECK(minimum->adaptive_allowed);
    const auto* smoothing = find_setting(SettingId::smooth_frame_rate);
    CHECK(smoothing != nullptr);
    CHECK(smoothing->relative_path == L"KFGame.ini");
    CHECK(smoothing->section == L"KFGame.KFGameEngine");
    const auto* physics_async_scene =
        find_setting(SettingId::physics_async_scene);
    CHECK(physics_async_scene != nullptr);
    CHECK(physics_async_scene->relative_path == L"KFEngine.ini");
    CHECK(physics_async_scene->section == L"Engine.Physics");
    CHECK(physics_async_scene->key == L"bPhysicsAsyncScene");
    CHECK(physics_async_scene->type == SettingType::boolean);
    CHECK(!physics_async_scene->adaptive_allowed);
    const auto* enable_async_scene =
        find_setting(SettingId::enable_async_scene);
    CHECK(enable_async_scene != nullptr);
    CHECK(enable_async_scene->relative_path == L"KFEngine.ini");
    CHECK(enable_async_scene->section == L"Engine.Physics");
    CHECK(enable_async_scene->key == L"bEnableAsyncScene");
    CHECK(enable_async_scene->type == SettingType::boolean);
    CHECK(!enable_async_scene->adaptive_allowed);
    const auto* memory_margin =
        find_setting(SettingId::texture_streaming_memory_margin);
    CHECK(memory_margin != nullptr);
    CHECK(memory_margin->relative_path == L"KFEngine.ini");
    CHECK(memory_margin->section == L"TextureStreaming");
    CHECK(memory_margin->key == L"MemoryMargin");
    CHECK(memory_margin->type == SettingType::integer);
    CHECK(!memory_margin->adaptive_allowed);
    const auto* streaming_hysteresis =
        find_setting(SettingId::texture_streaming_hysteresis_limit);
    CHECK(streaming_hysteresis != nullptr);
    CHECK(streaming_hysteresis->relative_path == L"KFEngine.ini");
    CHECK(streaming_hysteresis->section == L"TextureStreaming");
    CHECK(streaming_hysteresis->key == L"HysteresisLimit");
    CHECK(streaming_hysteresis->type == SettingType::integer);
    CHECK(!streaming_hysteresis->adaptive_allowed);
    const auto* shadow_resolution =
        find_setting(SettingId::max_shadow_resolution);
    CHECK(shadow_resolution != nullptr);
    CHECK(std::get<int>(*step_setting_value(
        *shadow_resolution, 512, 1)) == 1024);
    CHECK(!parse_setting_value(*shadow_resolution, L"768").has_value());
    const auto* lifetime = find_setting(SettingId::gore_lifetime_multiplier);
    CHECK(lifetime != nullptr);
    CHECK(std::abs(std::get<double>(*step_setting_value(
        *lifetime, 1.0, 1)) - 1.05) < 0.000001);
    const auto* shadows = find_setting(SettingId::dynamic_shadows);
    CHECK(shadows != nullptr);
    CHECK(!std::get<bool>(*step_setting_value(*shadows, true, 1)));
    const auto* flex_sleep_frames =
        find_setting(SettingId::flex_invisible_frames_before_sleep);
    CHECK(flex_sleep_frames != nullptr);
    CHECK(!flex_sleep_frames->adaptive_allowed);
    CHECK(std::get<int>(*step_setting_value(
        *flex_sleep_frames, 60, 1)) == 65);
    const auto* flex_sleep_distance =
        find_setting(SettingId::flex_distance_before_sleep);
    CHECK(flex_sleep_distance != nullptr);
    CHECK(!flex_sleep_distance->adaptive_allowed);
    CHECK(std::abs(std::get<double>(*step_setting_value(
        *flex_sleep_distance, 1500.0, -1)) - 1400.0) < 0.000001);
    CHECK(!find_setting(SettingId::unverified_effect_profile));
    return EXIT_SUCCESS;
}
