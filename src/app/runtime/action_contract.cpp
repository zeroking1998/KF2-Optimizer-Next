#include "action_contract.hpp"

#include "kf2/optimizer/adaptive_stability.hpp"

#include <array>

namespace kf2::app::runtime {
namespace {

constexpr AccessPolicy kRestricted = AccessPolicy::restricted_mode_allowed;
constexpr AccessPolicy kNormal = AccessPolicy::normal_mode_required;

constexpr std::array<ActionDefinition, 61> kActions{{
    {ActionId::diagnostics_export_support, "diagnostics-export-support", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_flex_restore, "diagnostics-flex-restore", FeatureId::diagnostics, kNormal},
    {ActionId::diagnostics_full_check, "diagnostics-full-check", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_open_data, "diagnostics-open-data", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_open_log, "diagnostics-open-log", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_repair_package, "diagnostics-repair-package", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_auto_repair, "header-repair", FeatureId::diagnostics, kRestricted},
    {ActionId::game_launch, "dashboard-launch", FeatureId::game, AccessPolicy::conditional_game_launch},
    {ActionId::game_open_config, "game-open-config", FeatureId::game, kRestricted},
    {ActionId::game_select_install, "game-select-install", FeatureId::game, kNormal},
    {ActionId::optimizer_backup, "diagnostics-backup", FeatureId::backup, kNormal},
    {ActionId::overlay_position, "overlay-position", FeatureId::overlay, kNormal},
    {ActionId::overlay_scale_reset, "overlay-scale-reset", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_cpu, "overlay-show-cpu", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_fps, "overlay-show-fps", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_frame_time, "overlay-show-frame-time", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_gpu, "overlay-show-gpu", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_memory, "overlay-show-memory", FeatureId::overlay, kNormal},
    {ActionId::overlay_toggle, "overlay-toggle", FeatureId::overlay, kNormal},
    {ActionId::settings_updates_automatic, "settings-updates-automatic", FeatureId::settings, kRestricted},
    {ActionId::settings_updates_check, "settings-updates-check", FeatureId::settings, kRestricted},
    {ActionId::settings_updates_install, "settings-updates-install", FeatureId::settings, kRestricted},
    {ActionId::settings_updates_later, "settings-updates-later", FeatureId::settings, kRestricted},
    {ActionId::settings_updates_ignore, "settings-updates-ignore", FeatureId::settings, kRestricted},
    {ActionId::graphics_display, "graphics-display", FeatureId::graphics, kNormal},
    {ActionId::graphics_resolution, "graphics-resolution", FeatureId::graphics, kNormal},
    {ActionId::graphics_overall_quality, "graphics-overall-quality", FeatureId::graphics, kNormal},
    {ActionId::graphics_vsync, "graphics-vsync", FeatureId::graphics, kNormal},
    {ActionId::graphics_variable_frame_rate, "graphics-variable-frame-rate", FeatureId::graphics, kNormal},
    {ActionId::graphics_environment_detail, "graphics-environment-detail", FeatureId::graphics, kNormal},
    {ActionId::graphics_character_detail, "graphics-character-detail", FeatureId::graphics, kNormal},
    {ActionId::graphics_fx, "graphics-fx", FeatureId::graphics, kNormal},
    {ActionId::graphics_texture_resolution, "graphics-texture-resolution", FeatureId::graphics, kNormal},
    {ActionId::graphics_texture_filtering, "graphics-texture-filtering", FeatureId::graphics, kNormal},
    {ActionId::graphics_shadow_quality, "graphics-shadow-quality", FeatureId::graphics, kNormal},
    {ActionId::graphics_realtime_reflections, "graphics-realtime-reflections", FeatureId::graphics, kNormal},
    {ActionId::graphics_anti_aliasing, "graphics-anti-aliasing", FeatureId::graphics, kNormal},
    {ActionId::graphics_bloom, "graphics-bloom", FeatureId::graphics, kNormal},
    {ActionId::graphics_motion_blur, "graphics-motion-blur", FeatureId::graphics, kNormal},
    {ActionId::graphics_ambient_occlusion, "graphics-ambient-occlusion", FeatureId::graphics, kNormal},
    {ActionId::graphics_depth_of_field, "graphics-depth-of-field", FeatureId::graphics, kNormal},
    {ActionId::graphics_volumetric_lighting, "graphics-volumetric-lighting", FeatureId::graphics, kNormal},
    {ActionId::graphics_lens_flares, "graphics-lens-flares", FeatureId::graphics, kNormal},
    {ActionId::graphics_light_shafts, "graphics-light-shafts", FeatureId::graphics, kNormal},
    {ActionId::graphics_flex, "graphics-flex", FeatureId::graphics, kNormal},
    {ActionId::graphics_apply, "graphics-apply", FeatureId::graphics, kNormal},
    {ActionId::graphics_reset, "graphics-reset", FeatureId::graphics, kNormal},
    {ActionId::advanced_one_frame_thread_lag, "advanced-one-frame-thread-lag", FeatureId::advanced, kNormal},
    {ActionId::advanced_per_frame_sleep, "advanced-per-frame-sleep", FeatureId::advanced, kNormal},
    {ActionId::advanced_per_frame_yield, "advanced-per-frame-yield", FeatureId::advanced, kNormal},
    {ActionId::advanced_background_level_streaming, "advanced-background-level-streaming", FeatureId::advanced, kNormal},
    {ActionId::advanced_texture_streaming, "advanced-texture-streaming", FeatureId::advanced, kNormal},
    {ActionId::advanced_priority_streaming, "advanced-priority-streaming", FeatureId::advanced, kNormal},
    {ActionId::advanced_dynamic_streaming, "advanced-dynamic-streaming", FeatureId::advanced, kNormal},
    {ActionId::advanced_hardware_shadow_filtering, "advanced-hardware-shadow-filtering", FeatureId::advanced, kNormal},
    {ActionId::advanced_downsampled_translucency, "advanced-downsampled-translucency", FeatureId::advanced, kNormal},
    {ActionId::advanced_floating_point_render_targets, "advanced-floating-point-render-targets", FeatureId::advanced, kNormal},
    {ActionId::advanced_max_multisamples, "advanced-max-multisamples", FeatureId::advanced, kNormal},
    {ActionId::advanced_gore_level, "advanced-gore-level", FeatureId::advanced, kNormal},
    {ActionId::advanced_apply, "advanced-apply", FeatureId::advanced, kNormal},
    {ActionId::advanced_reset, "advanced-reset", FeatureId::advanced, kNormal},
}};

constexpr std::array<ActionBinding, 63> kBindings{{
    {"dashboard-launch", ActionId::game_launch},
    {"diagnostics-backup", ActionId::optimizer_backup},
    {"diagnostics-export-support", ActionId::diagnostics_export_support},
    {"diagnostics-flex-restore", ActionId::diagnostics_flex_restore}, {"diagnostics-full-check", ActionId::diagnostics_full_check},
    {"diagnostics-open-data", ActionId::diagnostics_open_data}, {"diagnostics-open-log", ActionId::diagnostics_open_log},
    {"diagnostics-repair-package", ActionId::diagnostics_repair_package},
    {"game-open-config", ActionId::game_open_config},
    {"game-select-install", ActionId::game_select_install},
    {"header-repair", ActionId::diagnostics_auto_repair},
    {"header-update-check", ActionId::settings_updates_check},
    {"header-update-install", ActionId::settings_updates_install},
    {"overlay-position", ActionId::overlay_position},
    {"overlay-scale-reset", ActionId::overlay_scale_reset},
    {"overlay-show-cpu", ActionId::overlay_show_cpu},
    {"overlay-show-fps", ActionId::overlay_show_fps}, {"overlay-show-frame-time", ActionId::overlay_show_frame_time},
    {"overlay-show-gpu", ActionId::overlay_show_gpu}, {"overlay-show-memory", ActionId::overlay_show_memory},
    {"overlay-toggle", ActionId::overlay_toggle},
    {"settings-updates-automatic", ActionId::settings_updates_automatic},
    {"settings-updates-check", ActionId::settings_updates_check},
    {"settings-updates-install", ActionId::settings_updates_install},
    {"settings-updates-later", ActionId::settings_updates_later},
    {"settings-updates-ignore", ActionId::settings_updates_ignore},
    {"graphics-display", ActionId::graphics_display},
    {"graphics-resolution", ActionId::graphics_resolution},
    {"graphics-overall-quality", ActionId::graphics_overall_quality},
    {"graphics-vsync", ActionId::graphics_vsync},
    {"graphics-variable-frame-rate", ActionId::graphics_variable_frame_rate},
    {"graphics-environment-detail", ActionId::graphics_environment_detail},
    {"graphics-character-detail", ActionId::graphics_character_detail},
    {"graphics-fx", ActionId::graphics_fx},
    {"graphics-texture-resolution", ActionId::graphics_texture_resolution},
    {"graphics-texture-filtering", ActionId::graphics_texture_filtering},
    {"graphics-shadow-quality", ActionId::graphics_shadow_quality},
    {"graphics-realtime-reflections", ActionId::graphics_realtime_reflections},
    {"graphics-anti-aliasing", ActionId::graphics_anti_aliasing},
    {"graphics-bloom", ActionId::graphics_bloom},
    {"graphics-motion-blur", ActionId::graphics_motion_blur},
    {"graphics-ambient-occlusion", ActionId::graphics_ambient_occlusion},
    {"graphics-depth-of-field", ActionId::graphics_depth_of_field},
    {"graphics-volumetric-lighting", ActionId::graphics_volumetric_lighting},
    {"graphics-lens-flares", ActionId::graphics_lens_flares},
    {"graphics-light-shafts", ActionId::graphics_light_shafts},
    {"graphics-flex", ActionId::graphics_flex},
    {"graphics-apply", ActionId::graphics_apply},
    {"graphics-reset", ActionId::graphics_reset},
    {"advanced-one-frame-thread-lag", ActionId::advanced_one_frame_thread_lag},
    {"advanced-per-frame-sleep", ActionId::advanced_per_frame_sleep},
    {"advanced-per-frame-yield", ActionId::advanced_per_frame_yield},
    {"advanced-background-level-streaming", ActionId::advanced_background_level_streaming},
    {"advanced-texture-streaming", ActionId::advanced_texture_streaming},
    {"advanced-priority-streaming", ActionId::advanced_priority_streaming},
    {"advanced-dynamic-streaming", ActionId::advanced_dynamic_streaming},
    {"advanced-hardware-shadow-filtering", ActionId::advanced_hardware_shadow_filtering},
    {"advanced-downsampled-translucency", ActionId::advanced_downsampled_translucency},
    {"advanced-floating-point-render-targets", ActionId::advanced_floating_point_render_targets},
    {"advanced-max-multisamples", ActionId::advanced_max_multisamples},
    {"advanced-gore-level", ActionId::advanced_gore_level},
    {"advanced-apply", ActionId::advanced_apply},
    {"advanced-reset", ActionId::advanced_reset},
}};

constexpr std::array<ControlDefinition, 7> kControls{{
    {ControlId::overlay_scale, "overlay-scale-slider", FeatureId::overlay, 60, 200},
    {ControlId::corpse_limit, "settings-corpses-slider", FeatureId::settings, 4, 2000},
    {ControlId::target_fps, "settings-target-slider", FeatureId::settings,
     optimizer::kTargetFpsMinimum, optimizer::kTargetFpsMaximum},
    {ControlId::film_grain, "graphics-film-grain-slider", FeatureId::graphics, 0, 200},
    {ControlId::advanced_screen_percentage, "advanced-screen-percentage-slider", FeatureId::advanced, 50, 200},
    {ControlId::advanced_particle_percentage, "advanced-particle-percentage-slider", FeatureId::advanced, 0, 100},
    {ControlId::advanced_decal_lifetime, "advanced-decal-lifetime-slider", FeatureId::advanced, 0, 120},
}};

}  // namespace

std::span<const ActionDefinition> action_definitions() noexcept {
    return kActions;
}

std::span<const ActionBinding> action_bindings() noexcept {
    return kBindings;
}

std::optional<ActionRequest> parse_action(std::string_view name) noexcept {
    for (const auto& binding : kBindings) {
        if (binding.name == name) {
            return ActionRequest{binding.id, name};
        }
    }
    return std::nullopt;
}

const ActionDefinition* find_action(ActionId id) noexcept {
    for (const auto& definition : kActions) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

bool requires_normal_mode(ActionId id, ActionAccessContext context) noexcept {
    const auto* definition = find_action(id);
    if (definition == nullptr) {
        return false;
    }
    if (definition->access == AccessPolicy::normal_mode_required) {
        return true;
    }
    return definition->access == AccessPolicy::conditional_game_launch &&
           context.protected_game_launch;
}

std::optional<ResolvedAction> resolve_action(
    std::string_view name, ActionAccessContext context) noexcept {
    const auto request = parse_action(name);
    if (!request) {
        return std::nullopt;
    }
    const auto* definition = find_action(request->id);
    if (definition == nullptr) {
        return std::nullopt;
    }
    return ResolvedAction{
        definition->id,
        definition->canonical_name,
        requires_normal_mode(definition->id, context),
    };
}

std::span<const ControlDefinition> control_definitions() noexcept {
    return kControls;
}

const ControlDefinition* find_control(std::string_view name) noexcept {
    for (const auto& definition : kControls) {
        if (definition.name == name) {
            return &definition;
        }
    }
    return nullptr;
}

int clamp_control_value(const ControlDefinition& definition,
                        int requested) noexcept {
    if (requested < definition.minimum) {
        return definition.minimum;
    }
    if (requested > definition.maximum) {
        return definition.maximum;
    }
    return requested;
}

std::optional<ControlRequest> resolve_control(
    std::string_view name, int requested) noexcept {
    const auto* definition = find_control(name);
    if (definition == nullptr) {
        return std::nullopt;
    }
    return ControlRequest{
        definition->id,
        clamp_control_value(*definition, requested),
    };
}

}  // namespace kf2::app::runtime
