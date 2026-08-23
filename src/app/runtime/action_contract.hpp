#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace kf2::app::runtime {

enum class FeatureId : std::uint8_t {
    game, settings, overlay, diagnostics, backup, graphics, advanced
};

enum class AccessPolicy : std::uint8_t {
    restricted_mode_allowed,
    normal_mode_required,
    conditional_game_launch,
};

enum class ActionId : std::uint16_t {
    diagnostics_export_support = 11,
    diagnostics_flex_restore = 13,
    diagnostics_full_check = 14,
    diagnostics_open_data = 15,
    diagnostics_open_log = 16,
    game_launch = 17,
    game_open_config = 19,
    game_select_install = 22,
    optimizer_backup = 29,
    overlay_position = 45,
    overlay_scale_reset = 47,
    overlay_show_cpu = 49,
    overlay_show_fps = 50,
    overlay_show_frame_time = 51,
    overlay_show_gpu = 52,
    overlay_show_memory = 53,
    overlay_toggle = 54,
    diagnostics_repair_package = 83,
    diagnostics_auto_repair = 84,
    settings_updates_automatic = 85,
    settings_updates_check = 86,
    settings_updates_install = 87,
    settings_updates_later = 88,
    settings_updates_ignore = 89,
    graphics_display = 90,
    graphics_resolution = 91,
    graphics_overall_quality = 92,
    graphics_vsync = 93,
    graphics_variable_frame_rate = 94,
    graphics_environment_detail = 95,
    graphics_character_detail = 96,
    graphics_fx = 97,
    graphics_texture_resolution = 98,
    graphics_texture_filtering = 99,
    graphics_shadow_quality = 100,
    graphics_realtime_reflections = 101,
    graphics_anti_aliasing = 102,
    graphics_bloom = 103,
    graphics_motion_blur = 104,
    graphics_ambient_occlusion = 105,
    graphics_depth_of_field = 106,
    graphics_volumetric_lighting = 107,
    graphics_lens_flares = 108,
    graphics_light_shafts = 109,
    graphics_flex = 110,
    graphics_apply = 111,
    graphics_reset = 112,
    advanced_one_frame_thread_lag = 113,
    advanced_per_frame_sleep = 114,
    advanced_per_frame_yield = 115,
    advanced_background_level_streaming = 116,
    advanced_texture_streaming = 117,
    advanced_priority_streaming = 118,
    advanced_dynamic_streaming = 119,
    advanced_hardware_shadow_filtering = 121,
    advanced_downsampled_translucency = 122,
    advanced_floating_point_render_targets = 123,
    advanced_max_multisamples = 124,
    advanced_gore_level = 125,
    advanced_apply = 126,
    advanced_reset = 127,
};

enum class ControlId : std::uint8_t {
    overlay_scale, corpse_limit, target_fps, film_grain,
    advanced_screen_percentage, advanced_particle_percentage,
    advanced_decal_lifetime,
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
