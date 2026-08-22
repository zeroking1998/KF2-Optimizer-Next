#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/config/settings.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using kf2::config::Settings;
    using kf2::config::parse_settings;
    using kf2::config::serialize_settings;

    const auto parsed = parse_settings(
        "schema_version=1\r\nsmart_mode=true\nsound_enabled=false\n"
        "animations_enabled=false\nguide_completed=true\nguide_step=17\n"
        "overlay_enabled=true\noverlay_show_fps=false\noverlay_show_frame_time=true\n"
        "overlay_show_cpu=false\noverlay_show_gpu=true\noverlay_show_memory=true\n"
        "overlay_position=bottom_left\n"
        "offline_gameplay_telemetry=true\n"
        "adaptive_flex_enabled=true\nadaptive_flex_auto=false\n"
        "adaptive_flex_max_substeps=5\nmanual_flex_substeps=4\n"
        "overlay_scale_percent=175\n"
        "target_fps=120\nmanual_corpse_limit=9\nmanual_gore_effect_limit=7\n"
        "quality_policy=invisible\n"
        "optimizer_profile=stability\n"
        "manual_game_path=D:\\Steam\\KillingFloor2\ncustom_key=preserved\n");
    CHECK(parsed.has_value());
    CHECK(parsed.value().target_fps == 120);
    CHECK(parsed.value().corpse_limit == 9);
    CHECK(!parsed.value().animations_enabled);
    CHECK(parsed.value().overlay_enabled);
    CHECK(!parsed.value().overlay_show_fps);
    CHECK(parsed.value().overlay_show_frame_time);
    CHECK(!parsed.value().overlay_show_cpu);
    CHECK(parsed.value().overlay_show_gpu);
    CHECK(parsed.value().overlay_show_memory);
    CHECK(parsed.value().offline_gameplay_telemetry);
    CHECK(parsed.value().overlay_position == "bottom_left");
    CHECK(parsed.value().overlay_scale_percent == 175);
    CHECK(parsed.value().quality_policy == "invisible");
    CHECK(parsed.value().optimizer_profile == "stability");
    CHECK(parsed.value().manual_game_path == "D:\\Steam\\KillingFloor2");
    CHECK(parsed.value().extras.at("custom_key") == "preserved");
    const auto migrated_serialized = serialize_settings(parsed.value());
    CHECK(migrated_serialized.find("guide_completed") == std::string::npos);
    CHECK(migrated_serialized.find("guide_step") == std::string::npos);
    CHECK(migrated_serialized.find("adaptive_flex_max_substeps") ==
          std::string::npos);

    CHECK(!parse_settings("schema_version=9\ntarget_fps=120\n").has_value());
    CHECK(!parse_settings("schema_version=1\ntarget_fps=9999\n").has_value());
    CHECK(!parse_settings("schema_version=1\ntarget_fps=29\n").has_value());
    CHECK(!parse_settings("schema_version=1\ntarget_fps=361\n").has_value());
    CHECK(!parse_settings(
        "schema_version=1\ntarget_fps=60\ntarget_fps=120\n").has_value());

    for (int target = 30; target <= 240; ++target) {
        const auto exact = parse_settings(
            "schema_version=1\ntarget_fps=" + std::to_string(target) + "\n");
        CHECK(exact.has_value());
        CHECK(exact.value().target_fps == target);
        CHECK(!exact.value().target_fps_migrated);
        const auto roundtrip = parse_settings(serialize_settings(exact.value()));
        CHECK(roundtrip.has_value());
        CHECK(roundtrip.value().target_fps == target);
        CHECK(!roundtrip.value().target_fps_migrated);
    }
    for (const int legacy : {241, 300, 360}) {
        const auto migrated = parse_settings(
            "schema_version=1\ntarget_fps=" + std::to_string(legacy) + "\n");
        CHECK(migrated.has_value());
        CHECK(migrated.value().target_fps == 240);
        CHECK(migrated.value().target_fps_migrated);
        const auto saved = serialize_settings(migrated.value());
        CHECK(saved.find("target_fps=240\n") != std::string::npos);
        const auto reloaded = parse_settings(saved);
        CHECK(reloaded.has_value());
        CHECK(reloaded.value().target_fps == 240);
        CHECK(!reloaded.value().target_fps_migrated);
    }
    CHECK(!parse_settings("schema_version=1\nsmart_mode=yes\n").has_value());
    CHECK(!parse_settings("schema_version=1\noptimizer_mode=magic\n").has_value());
    const auto migrated_manual = parse_settings(
        "schema_version=1\noptimizer_mode=manual\nsmart_mode=true\n");
    CHECK(migrated_manual.has_value());
    const auto migrated_smart = parse_settings(
        "schema_version=1\noptimizer_mode=smart\nsmart_mode=true\n");
    CHECK(migrated_smart.has_value());
    const auto adaptive = parse_settings(
        "schema_version=1\noptimizer_mode=adaptive\n"
        "adaptive_aggressiveness=aggressive\n"
        "adaptive_minimum_quality=55\nadaptive_maximum_quality=95\n"
        "adaptive_quality_change_budget=3\nadaptive_headroom_percent=12\n");
    CHECK(adaptive.has_value());
    CHECK(adaptive.value().adaptive_aggressiveness == "aggressive");
    CHECK(adaptive.value().adaptive_minimum_quality == 55);
    CHECK(adaptive.value().adaptive_maximum_quality == 95);
    const auto adaptive_is_the_enable = parse_settings(
        "schema_version=1\noptimizer_mode=adaptive\n"
        "adaptive_enabled=false\n");
    CHECK(adaptive_is_the_enable.has_value());
    CHECK(serialize_settings(adaptive_is_the_enable.value()).find(
              "adaptive_enabled") == std::string::npos);
    const auto legacy_manual_flex = parse_settings(
        "schema_version=1\noptimizer_mode=manual\n"
        "adaptive_flex_enabled=false\nadaptive_flex_auto=true\n");
    CHECK(legacy_manual_flex.has_value());
    CHECK(serialize_settings(legacy_manual_flex.value()).find(
              "adaptive_flex_enabled") == std::string::npos);
    CHECK(serialize_settings(legacy_manual_flex.value()).find(
              "manual_flex_substeps") == std::string::npos);
    CHECK(serialize_settings(legacy_manual_flex.value()).find(
              "adaptive_flex_max_substeps") == std::string::npos);
    CHECK(!parse_settings(
        "schema_version=1\nadaptive_minimum_quality=90\n"
        "adaptive_maximum_quality=80\n").has_value());
    CHECK(!parse_settings("schema_version=1\noverlay_show_memory=maybe\n").has_value());
    CHECK(!parse_settings(
        "schema_version=1\noffline_gameplay_telemetry=maybe\n").has_value());
    CHECK(!parse_settings("schema_version=1\nanimations_enabled=maybe\n").has_value());
    CHECK(!parse_settings("schema_version=1\nguide_step=25\n").has_value());
    CHECK(!parse_settings("schema_version=1\nquality_policy=magic\n").has_value());
    CHECK(!parse_settings("schema_version=1\noptimizer_profile=turbo\n").has_value());
    CHECK(!parse_settings("schema_version=1\noverlay_position=center\n").has_value());
    CHECK(!parse_settings("schema_version=1\noverlay_scale_percent=201\n").has_value());
    CHECK(parse_settings(
              "schema_version=1\nmanual_corpse_limit=2000\n")
              .has_value());
    CHECK(!parse_settings(
               "schema_version=1\nmanual_corpse_limit=2001\n")
               .has_value());
    for (int legacy = 0; legacy <= 3; ++legacy) {
        const auto migrated_corpses = parse_settings(
            "schema_version=1\nmanual_corpse_limit=" +
            std::to_string(legacy) + "\n");
        CHECK(migrated_corpses.has_value());
        CHECK(migrated_corpses.value().corpse_limit == 4);
    }
    for (const int value : {4, 1000, 1999, 2000}) {
        const auto exact = parse_settings(
            "schema_version=1\nmanual_corpse_limit=" +
            std::to_string(value) + "\n");
        CHECK(exact.has_value());
        CHECK(exact.value().corpse_limit == value);
        CHECK(parse_settings(serialize_settings(exact.value())).value()
                  .corpse_limit == value);
    }
    CHECK(!parse_settings(
        "schema_version=1\ncorpse_limit=20\nmanual_corpse_limit=21\n")
               .has_value());
    CHECK(!parse_settings("schema_version=1\nmanual_gore_effect_limit=129\n").has_value());
    CHECK(!parse_settings("schema_version=1\nmanual_flex_substeps=0\n").has_value());
    CHECK(!parse_settings("schema_version=1\nadaptive_flex_max_substeps=6\n").has_value());
    CHECK(!parse_settings("schema_version=1\nmanual_game_path=bad=value\n").has_value());
    const auto legacy_sound = parse_settings(
        "schema_version=1\nsound_enabled=true\n");
    CHECK(legacy_sound.has_value());
    CHECK(serialize_settings(legacy_sound.value()).find("sound_enabled") ==
          std::string::npos);

    CHECK(serialize_settings(Settings{}) ==
          "schema_version=1\noptimizer_mode=adaptive\n"
          "animations_enabled=true\n"
          "automatic_update_checks=true\n"
          "overlay_enabled=false\noverlay_show_fps=true\noverlay_show_frame_time=true\n"
          "overlay_show_cpu=true\noverlay_show_gpu=true\noverlay_show_memory=false\n"
          "restore_config_after_game=true\noffline_gameplay_telemetry=false\n"
          "adaptive_aggressiveness=balanced\n"
          "adaptive_minimum_quality=70\nadaptive_maximum_quality=100\n"
          "adaptive_quality_change_budget=2\nadaptive_headroom_percent=8\n"
          "adaptive_emergency_enabled=true\n"
          "adaptive_quality_recovery_enabled=true\n"
          "adaptive_manual_locks_enabled=true\n"
          "adaptive_shadow_mode=true\n"
          "adaptive_calibration_enabled=true\nadaptive_logging=true\n"
          "overlay_position=top_right\n"
          "overlay_scale_percent=100\n"
          "target_fps=60\ncorpse_limit=20\n"
          "quality_policy=exact\n"
          "optimizer_profile=balanced\n");

    Settings with_extras;
    CHECK(serialize_settings(with_extras).find(
              "optimizer_mode=adaptive\n") != std::string::npos);
    with_extras.extras.emplace("z_last", "2");
    with_extras.extras.emplace("a_first", "1");
    CHECK(serialize_settings(with_extras).ends_with(
        "a_first=1\nz_last=2\n"));
    with_extras.extras.clear();
    with_extras.manual_game_path = "D:\\Steam\\KillingFloor2";
    CHECK(serialize_settings(with_extras).ends_with(
        "manual_game_path=D:\\Steam\\KillingFloor2\n"));
    const auto updates_off = parse_settings(
        "schema_version=1\nautomatic_update_checks=false\n");
    CHECK(updates_off.has_value());
    CHECK(!updates_off.value().automatic_update_checks);
    CHECK(serialize_settings(updates_off.value()).find(
              "automatic_update_checks=false\n") != std::string::npos);
    CHECK(!parse_settings(
        "schema_version=1\nautomatic_update_checks=maybe\n").has_value());
    return EXIT_SUCCESS;
}
