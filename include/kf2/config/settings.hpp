#pragma once

#include <map>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::config {

struct Settings {
    int schema_version{1};
    bool automatic_update_checks{true};
    bool overlay_enabled{true};
    bool overlay_show_fps{true};
    bool overlay_show_frame_time{true};
    bool overlay_show_cpu{true};
    bool overlay_show_gpu{true};
    bool overlay_show_memory{true};
    bool debug_corpse_markers{false};
    bool debug_zed_markers{false};
    bool restore_config_after_game{true};
    std::string adaptive_aggressiveness{"balanced"};
    int adaptive_minimum_quality{10};
    int adaptive_maximum_quality{100};
    int adaptive_quality_change_budget{2};
    int adaptive_headroom_percent{8};
    bool adaptive_emergency_enabled{true};
    bool adaptive_quality_recovery_enabled{true};
    bool adaptive_manual_locks_enabled{true};
    bool adaptive_shadow_mode{false};
    bool adaptive_calibration_enabled{true};
    bool adaptive_logging{true};
    std::string overlay_position{"top_right"};
    int overlay_scale_percent{100};
    int target_fps{60};
    // Transient load evidence. It is intentionally not serialized.
    bool target_fps_migrated{false};
    bool adaptive_quality_range_migrated{false};
    int corpse_limit{20};
    std::string quality_policy{"exact"};
    std::string optimizer_profile{"balanced"};
    std::string manual_game_path;
    std::map<std::string, std::string> extras;
};

[[nodiscard]] Result<Settings> parse_settings(std::string_view text);
[[nodiscard]] std::string serialize_settings(const Settings& settings);

}  // namespace kf2::config
