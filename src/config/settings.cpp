#include "kf2/config/settings.hpp"

#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

#include "kf2/optimizer/adaptive_stability.hpp"

namespace kf2::config {
namespace {

Result<bool> parse_boolean(std::string_view value) {
    if (value == "true") {
        return Result<bool>::success(true);
    }
    if (value == "false") {
        return Result<bool>::success(false);
    }
    return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Boolean setting is invalid", 0});
}

Result<int> parse_integer(std::string_view value) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return Result<int>::failure(
            {ErrorCode::invalid_argument, L"Integer setting is invalid", 0});
    }
    return Result<int>::success(parsed);
}

Result<Settings> invalid_settings(const wchar_t* message) {
    return Result<Settings>::failure({ErrorCode::invalid_argument, message, 0});
}

bool safe_path_text(std::string_view value) {
    if (value.size() > 4096) return false;
    for (const unsigned char character : value) {
        if (character < 0x20 || character == '=') return false;
    }
    return true;
}

}  // namespace

Result<Settings> parse_settings(std::string_view text) {
    Settings settings;
    bool schema_seen = false;
    bool corpse_limit_seen = false;
    std::set<std::string> seen;
    std::size_t offset = 0;

    while (offset <= text.size()) {
        const std::size_t end = text.find('\n', offset);
        std::string line{text.substr(offset, end == std::string_view::npos
                                                ? text.size() - offset
                                                : end - offset)};
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            const auto equals = line.find('=');
            if (equals == std::string::npos || equals == 0) {
                return invalid_settings(L"Settings line is malformed");
            }
            const std::string key = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if (!seen.insert(key).second) {
                return invalid_settings(L"Settings key is duplicated");
            }

            if (key == "schema_version") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() != 1) {
                    return invalid_settings(L"Settings schema is unsupported");
                }
                settings.schema_version = parsed.value();
                schema_seen = true;
            } else if (key == "smart_mode") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"smart_mode is invalid");
                }
            } else if (key == "optimizer_mode") {
                if (value == "smart" || value == "manual" ||
                    value == "adaptive") {
                    // Manual and Smart are accepted only as migration input.
                    // Every supported configuration now runs the one Adaptive
                    // controller and is serialized back canonically.
                } else {
                    return invalid_settings(L"optimizer_mode is invalid");
                }
            } else if (key == "sound_enabled") {
                // Accepted only to migrate older portable settings. The
                // product is intentionally silent and never stores or uses
                // Windows system-sound preferences anymore.
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"sound_enabled is invalid");
                }
            } else if (key == "animations_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"animations_enabled is invalid");
                settings.animations_enabled = parsed.value();
            } else if (key == "automatic_update_checks") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"automatic_update_checks is invalid");
                }
                settings.automatic_update_checks = parsed.value();
            } else if (key == "guide_completed") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"guide_completed is invalid");
                // Legacy onboarding state is accepted only for migration.
            } else if (key == "guide_step") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 1 ||
                    parsed.value() > 24) {
                    return invalid_settings(L"guide_step is outside 1..24");
                }
            } else if (key == "overlay_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"overlay_enabled is invalid");
                }
                settings.overlay_enabled = parsed.value();
            } else if (key == "overlay_show_fps") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"overlay_show_fps is invalid");
                settings.overlay_show_fps = parsed.value();
            } else if (key == "overlay_show_frame_time") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"overlay_show_frame_time is invalid");
                settings.overlay_show_frame_time = parsed.value();
            } else if (key == "overlay_show_cpu") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"overlay_show_cpu is invalid");
                settings.overlay_show_cpu = parsed.value();
            } else if (key == "overlay_show_gpu") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"overlay_show_gpu is invalid");
                settings.overlay_show_gpu = parsed.value();
            } else if (key == "overlay_show_memory") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"overlay_show_memory is invalid");
                settings.overlay_show_memory = parsed.value();
            } else if (key == "restore_config_after_game") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"restore_config_after_game is invalid");
                }
                settings.restore_config_after_game = parsed.value();
            } else if (key == "offline_gameplay_telemetry") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"offline_gameplay_telemetry is invalid");
                }
                settings.offline_gameplay_telemetry = parsed.value();
            } else if (key == "adaptive_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"adaptive_enabled is invalid");
                }
            } else if (key == "adaptive_aggressiveness") {
                if (value != "conservative" && value != "balanced" &&
                    value != "aggressive") {
                    return invalid_settings(
                        L"adaptive_aggressiveness is invalid");
                }
                settings.adaptive_aggressiveness = value;
            } else if (key == "adaptive_minimum_quality") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 0 ||
                    parsed.value() > 100) {
                    return invalid_settings(
                        L"adaptive_minimum_quality is outside 0..100");
                }
                settings.adaptive_minimum_quality = parsed.value();
            } else if (key == "adaptive_maximum_quality") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 0 ||
                    parsed.value() > 100) {
                    return invalid_settings(
                        L"adaptive_maximum_quality is outside 0..100");
                }
                settings.adaptive_maximum_quality = parsed.value();
            } else if (key == "adaptive_quality_change_budget") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 1 ||
                    parsed.value() > 5) {
                    return invalid_settings(
                        L"adaptive_quality_change_budget is outside 1..5");
                }
                settings.adaptive_quality_change_budget = parsed.value();
            } else if (key == "adaptive_headroom_percent") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 0 ||
                    parsed.value() > 50) {
                    return invalid_settings(
                        L"adaptive_headroom_percent is outside 0..50");
                }
                settings.adaptive_headroom_percent = parsed.value();
            } else if (key == "adaptive_emergency_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"adaptive_emergency_enabled is invalid");
                }
                settings.adaptive_emergency_enabled = parsed.value();
            } else if (key == "adaptive_quality_recovery_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"adaptive_quality_recovery_enabled is invalid");
                }
                settings.adaptive_quality_recovery_enabled = parsed.value();
            } else if (key == "adaptive_manual_locks_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"adaptive_manual_locks_enabled is invalid");
                }
                settings.adaptive_manual_locks_enabled = parsed.value();
            } else if (key == "adaptive_online_allowed") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"adaptive_online_allowed is invalid");
                }
                // Legacy broad session gate. Accepted only so existing
                // settings can migrate to per-control runtime capabilities.
            } else if (key == "adaptive_shadow_mode") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"adaptive_shadow_mode is invalid");
                }
                settings.adaptive_shadow_mode = parsed.value();
            } else if (key == "adaptive_calibration_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(
                        L"adaptive_calibration_enabled is invalid");
                }
                settings.adaptive_calibration_enabled = parsed.value();
            } else if (key == "adaptive_logging") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) {
                    return invalid_settings(L"adaptive_logging is invalid");
                }
                settings.adaptive_logging = parsed.value();
            } else if (key == "adaptive_flex_enabled") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"adaptive_flex_enabled is invalid");
            } else if (key == "adaptive_flex_auto") {
                const auto parsed = parse_boolean(value);
                if (!parsed.has_value()) return invalid_settings(L"adaptive_flex_auto is invalid");
            } else if (key == "adaptive_flex_max_substeps") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 1 || parsed.value() > 5)
                    return invalid_settings(L"adaptive_flex_max_substeps is outside 1..5");
                // Legacy user ceiling is accepted only for migration. The
                // Adaptive controller owns the complete 1..5 range.
            } else if (key == "manual_flex_substeps") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 1 || parsed.value() > 5)
                    return invalid_settings(L"manual_flex_substeps is outside 1..5");
            } else if (key == "overlay_position") {
                if (value != "top_left" && value != "top_right" &&
                    value != "bottom_left" && value != "bottom_right") {
                    return invalid_settings(L"overlay_position is invalid");
                }
                settings.overlay_position = value;
            } else if (key == "overlay_scale_percent") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 60 ||
                    parsed.value() > 200) {
                    return invalid_settings(
                        L"overlay_scale_percent is outside 60..200");
                }
                settings.overlay_scale_percent = parsed.value();
            } else if (key == "target_fps") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() ||
                    parsed.value() < optimizer::kTargetFpsMinimum ||
                    parsed.value() > 360) {
                    return invalid_settings(
                        L"target_fps is outside supported input 30..360");
                }
                settings.target_fps_migrated =
                    parsed.value() > optimizer::kTargetFpsMaximum;
                settings.target_fps = std::min(
                    parsed.value(), optimizer::kTargetFpsMaximum);
            } else if (key == "corpse_limit" ||
                       key == "manual_corpse_limit") {
                if (corpse_limit_seen) {
                    return invalid_settings(L"corpse_limit is duplicated");
                }
                corpse_limit_seen = true;
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 0 ||
                    parsed.value() > 2000) {
                    return invalid_settings(
                        L"corpse_limit is outside 0..2000");
                }
                // Older portable builds allowed 0..3 even though KF2 clamps
                // MaxDeadBodies to 4.  Migrate those values instead of making
                // an otherwise valid portable settings file unbootable.
                settings.corpse_limit = std::max(parsed.value(), 4);
            } else if (key == "manual_gore_effect_limit") {
                const auto parsed = parse_integer(value);
                if (!parsed.has_value() || parsed.value() < 0 || parsed.value() > 128) {
                    return invalid_settings(L"manual_gore_effect_limit is outside 0..128");
                }
            } else if (key == "quality_policy") {
                if (value != "exact" && value != "invisible" &&
                    value != "performance") {
                    return invalid_settings(L"quality_policy is invalid");
                }
                settings.quality_policy = value;
            } else if (key == "optimizer_profile") {
                if (value != "balanced" && value != "stability" &&
                    value != "high_performance" && value != "custom") {
                    return invalid_settings(L"optimizer_profile is invalid");
                }
                settings.optimizer_profile = value;
            } else if (key == "manual_game_path") {
                if (!safe_path_text(value)) {
                    return invalid_settings(L"manual_game_path is invalid");
                }
                settings.manual_game_path = value;
            } else {
                settings.extras.emplace(key, value);
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }

    if (!schema_seen) {
        return invalid_settings(L"Settings schema is missing");
    }
    // Legacy mode keys are read above, but all supported configurations are
    // normalized to the single Adaptive controller.
    if (settings.adaptive_minimum_quality > settings.adaptive_maximum_quality) {
        return invalid_settings(
            L"adaptive minimum quality exceeds maximum quality");
    }
    return Result<Settings>::success(std::move(settings));
}

std::string serialize_settings(const Settings& settings) {
    std::ostringstream output;
    output << "schema_version=" << settings.schema_version << '\n'
           << "optimizer_mode=adaptive\n"
           << "animations_enabled=" << (settings.animations_enabled ? "true" : "false") << '\n'
           << "automatic_update_checks="
           << (settings.automatic_update_checks ? "true" : "false") << '\n'
           << "overlay_enabled=" << (settings.overlay_enabled ? "true" : "false") << '\n'
           << "overlay_show_fps=" << (settings.overlay_show_fps ? "true" : "false") << '\n'
           << "overlay_show_frame_time=" << (settings.overlay_show_frame_time ? "true" : "false") << '\n'
           << "overlay_show_cpu=" << (settings.overlay_show_cpu ? "true" : "false") << '\n'
           << "overlay_show_gpu=" << (settings.overlay_show_gpu ? "true" : "false") << '\n'
           << "overlay_show_memory=" << (settings.overlay_show_memory ? "true" : "false") << '\n'
           << "restore_config_after_game="
           << (settings.restore_config_after_game ? "true" : "false") << '\n'
           << "offline_gameplay_telemetry="
           << (settings.offline_gameplay_telemetry ? "true" : "false") << '\n'
           << "adaptive_aggressiveness="
           << settings.adaptive_aggressiveness << '\n'
           << "adaptive_minimum_quality="
           << settings.adaptive_minimum_quality << '\n'
           << "adaptive_maximum_quality="
           << settings.adaptive_maximum_quality << '\n'
           << "adaptive_quality_change_budget="
           << settings.adaptive_quality_change_budget << '\n'
           << "adaptive_headroom_percent="
           << settings.adaptive_headroom_percent << '\n'
           << "adaptive_emergency_enabled="
           << (settings.adaptive_emergency_enabled ? "true" : "false") << '\n'
           << "adaptive_quality_recovery_enabled="
           << (settings.adaptive_quality_recovery_enabled ? "true" : "false") << '\n'
           << "adaptive_manual_locks_enabled="
           << (settings.adaptive_manual_locks_enabled ? "true" : "false") << '\n'
           << "adaptive_shadow_mode="
           << (settings.adaptive_shadow_mode ? "true" : "false") << '\n'
           << "adaptive_calibration_enabled="
           << (settings.adaptive_calibration_enabled ? "true" : "false") << '\n'
           << "adaptive_logging="
           << (settings.adaptive_logging ? "true" : "false") << '\n'
           << "overlay_position=" << settings.overlay_position << '\n'
           << "overlay_scale_percent=" << settings.overlay_scale_percent << '\n'
           << "target_fps=" << settings.target_fps << '\n'
           << "corpse_limit=" << settings.corpse_limit << '\n'
           << "quality_policy=" << settings.quality_policy << '\n'
           << "optimizer_profile=" << settings.optimizer_profile << '\n';
    if (!settings.manual_game_path.empty()) {
        output << "manual_game_path=" << settings.manual_game_path << '\n';
    }
    for (const auto& [key, value] : settings.extras) {
        output << key << '=' << value << '\n';
    }
    return output.str();
}

}  // namespace kf2::config
