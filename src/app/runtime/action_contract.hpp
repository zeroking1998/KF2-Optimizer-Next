#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace kf2::app::runtime {

enum class FeatureId : std::uint8_t {
    navigation, game, settings, overlay, diagnostics, optimizer, backup
};

enum class AccessPolicy : std::uint8_t {
    restricted_mode_allowed,
    normal_mode_required,
    conditional_game_launch,
};

enum class ActionId : std::uint16_t {
    navigate_diagnostics = 0,
    retired_navigate_guide = 1,
    navigate_settings = 2,
    navigate_overlay = 3,
    navigate_optimizer = 4,
    refresh_status = 5,
    diagnostics_benchmark_baseline = 6,
    diagnostics_benchmark_compare = 7,
    diagnostics_clear = 8,
    diagnostics_export = 9,
    diagnostics_export_inventory = 10,
    diagnostics_export_support = 11,
    diagnostics_flex_audit = 12,
    diagnostics_flex_restore = 13,
    diagnostics_full_check = 14,
    diagnostics_open_data = 15,
    diagnostics_open_log = 16,
    game_launch = 17,
    game_offline_telemetry = 18,
    game_open_config = 19,
    game_open_install = 20,
    game_open_logs = 21,
    game_select_install = 22,
    retired_guide_finish = 23,
    retired_guide_next = 24,
    retired_guide_previous = 25,
    retired_guide_reset = 26,
    retired_guide_reset_completion = 27,
    optimizer_apply = 28,
    optimizer_backup = 29,
    optimizer_export = 30,
    optimizer_import = 31,
    retired_optimizer_manual_create = 32,
    retired_optimizer_manual_decrease = 33,
    retired_optimizer_manual_increase = 34,
    retired_optimizer_manual_load = 35,
    retired_optimizer_manual_lock = 36,
    retired_optimizer_manual_next = 37,
    retired_optimizer_manual_next_group = 38,
    retired_optimizer_manual_previous = 39,
    retired_optimizer_manual_previous_group = 40,
    retired_optimizer_manual_reset = 41,
    optimizer_open_backups = 42,
    optimizer_preview = 43,
    optimizer_restore = 44,
    overlay_position = 45,
    overlay_scale_down = 46,
    overlay_scale_reset = 47,
    overlay_scale_up = 48,
    overlay_show_cpu = 49,
    overlay_show_fps = 50,
    overlay_show_frame_time = 51,
    overlay_show_gpu = 52,
    overlay_show_memory = 53,
    overlay_toggle = 54,
    settings_adaptive_aggressiveness = 55,
    settings_adaptive_budget = 56,
    settings_adaptive_calibration = 57,
    settings_adaptive_emergency = 58,
    settings_adaptive_headroom_down = 59,
    settings_adaptive_headroom_up = 60,
    settings_adaptive_locks = 61,
    settings_adaptive_logging = 62,
    settings_adaptive_maximum_down = 63,
    settings_adaptive_maximum_up = 64,
    settings_adaptive_minimum_down = 65,
    settings_adaptive_minimum_up = 66,
    settings_adaptive_recovery = 67,
    settings_adaptive_shadow = 68,
    settings_advanced_toggle = 69,
    settings_animations = 70,
    settings_corpses_down = 71,
    settings_corpses_up = 72,
    retired_settings_flex_down = 73,
    retired_settings_flex_up = 74,
    retired_settings_gore_down = 75,
    retired_settings_gore_up = 76,
    retired_settings_mode_adaptive = 77,
    retired_settings_mode_manual = 78,
    retired_settings_session_config = 79,
    settings_target_down = 80,
    settings_target_up = 81,
    settings_adaptive_online = 82,
    diagnostics_repair_package = 83,
    diagnostics_auto_repair = 84,
    settings_updates_automatic = 85,
    settings_updates_check = 86,
    settings_updates_install = 87,
    settings_updates_later = 88,
};

enum class ControlId : std::uint8_t {
    overlay_scale, adaptive_headroom, adaptive_maximum, adaptive_minimum,
    corpse_limit, retired_gore_effect_limit, target_fps,
};

struct NoPayload final {};

using ActionPayload = std::variant<std::monostate, NoPayload>;

enum class ActionPayloadKind : std::uint8_t {
    no_payload,
};

struct ActionDefinition {
    ActionId id;
    std::string_view canonical_name;
    FeatureId feature;
    AccessPolicy access;
    ActionPayloadKind payload_kind;

    constexpr ActionDefinition(
        ActionId action_id, std::string_view name, FeatureId owner,
        AccessPolicy policy,
        ActionPayloadKind payload = ActionPayloadKind::no_payload) noexcept
        : id{action_id}, canonical_name{name}, feature{owner}, access{policy},
          payload_kind{payload} {}
};

struct ActionBinding {
    std::string_view name;
    ActionId id;
};

struct ActionRequest {
    ActionId id;
    std::string_view received_name;
    ActionPayload payload{NoPayload{}};
};

struct ActionAccessContext {
    bool protected_game_launch{false};
};

struct ResolvedAction {
    ActionId id;
    std::string_view canonical_name;
    bool normal_mode_required;
};

struct ControlDefinition {
    ControlId id;
    std::string_view name;
    FeatureId feature;
    int minimum;
    int maximum;
};

struct ControlRequest {
    ControlId id;
    int value;
};

std::span<const ActionDefinition> action_definitions() noexcept;
std::span<const ActionBinding> action_bindings() noexcept;
std::optional<ActionRequest> parse_action(std::string_view name) noexcept;
const ActionDefinition* find_action(ActionId id) noexcept;
bool requires_normal_mode(ActionId id, ActionAccessContext context) noexcept;
std::optional<ResolvedAction> resolve_action(
    std::string_view name, ActionAccessContext context) noexcept;

std::span<const ControlDefinition> control_definitions() noexcept;
const ControlDefinition* find_control(std::string_view name) noexcept;
int clamp_control_value(const ControlDefinition& definition,
                        int requested) noexcept;
std::optional<ControlRequest> resolve_control(
    std::string_view name, int requested) noexcept;

}  // namespace kf2::app::runtime
