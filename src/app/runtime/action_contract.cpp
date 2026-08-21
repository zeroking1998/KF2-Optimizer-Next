#include "action_contract.hpp"

#include "kf2/optimizer/adaptive_stability.hpp"

#include <array>

namespace kf2::app::runtime {
namespace {

constexpr AccessPolicy kRestricted = AccessPolicy::restricted_mode_allowed;
constexpr AccessPolicy kNormal = AccessPolicy::normal_mode_required;

constexpr std::array<ActionDefinition, 60> kActions{{
    {ActionId::navigate_diagnostics, "header-diagnostics", FeatureId::navigation, kRestricted},
    {ActionId::navigate_settings, "dashboard-settings", FeatureId::navigation, kRestricted},
    {ActionId::navigate_overlay, "dashboard-overlay", FeatureId::navigation, kRestricted},
    {ActionId::navigate_optimizer, "settings-finetuning", FeatureId::navigation, kRestricted},
    {ActionId::refresh_status, "diagnostics-refresh", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_benchmark_baseline, "diagnostics-benchmark-baseline", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_benchmark_compare, "diagnostics-benchmark-compare", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_clear, "diagnostics-clear", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_export, "diagnostics-export", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_export_inventory, "diagnostics-export-inventory", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_export_support, "diagnostics-export-support", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_flex_audit, "diagnostics-flex-audit", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_flex_restore, "diagnostics-flex-restore", FeatureId::diagnostics, kNormal},
    {ActionId::diagnostics_full_check, "diagnostics-full-check", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_open_data, "diagnostics-open-data", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_open_log, "diagnostics-open-log", FeatureId::diagnostics, kRestricted},
    {ActionId::diagnostics_repair_package, "diagnostics-repair-package", FeatureId::diagnostics, kRestricted},
    {ActionId::game_launch, "game-launch", FeatureId::game, AccessPolicy::conditional_game_launch},
    {ActionId::game_offline_telemetry, "game-offline-telemetry", FeatureId::game, kNormal},
    {ActionId::game_open_config, "game-open-config", FeatureId::game, kRestricted},
    {ActionId::game_open_install, "game-open-install", FeatureId::game, kRestricted},
    {ActionId::game_open_logs, "game-open-logs", FeatureId::game, kRestricted},
    {ActionId::game_select_install, "game-select-install", FeatureId::game, kNormal},
    {ActionId::optimizer_apply, "optimizer-apply", FeatureId::optimizer, kNormal},
    {ActionId::optimizer_backup, "optimizer-backup", FeatureId::backup, kNormal},
    {ActionId::optimizer_export, "optimizer-export", FeatureId::optimizer, kRestricted},
    {ActionId::optimizer_import, "optimizer-import", FeatureId::optimizer, kRestricted},
    {ActionId::optimizer_open_backups, "optimizer-open-backups", FeatureId::backup, kRestricted},
    {ActionId::optimizer_preview, "optimizer-preview", FeatureId::optimizer, kRestricted},
    {ActionId::optimizer_restore, "optimizer-restore", FeatureId::backup, kNormal},
    {ActionId::overlay_position, "overlay-position", FeatureId::overlay, kNormal},
    {ActionId::overlay_scale_down, "overlay-scale-down", FeatureId::overlay, kNormal},
    {ActionId::overlay_scale_reset, "overlay-scale-reset", FeatureId::overlay, kNormal},
    {ActionId::overlay_scale_up, "overlay-scale-up", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_cpu, "overlay-show-cpu", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_fps, "overlay-show-fps", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_frame_time, "overlay-show-frame-time", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_gpu, "overlay-show-gpu", FeatureId::overlay, kNormal},
    {ActionId::overlay_show_memory, "overlay-show-memory", FeatureId::overlay, kNormal},
    {ActionId::overlay_toggle, "overlay-toggle", FeatureId::overlay, kNormal},
    {ActionId::settings_adaptive_aggressiveness, "settings-adaptive-aggressiveness", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_budget, "settings-adaptive-budget", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_calibration, "settings-adaptive-calibration", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_emergency, "settings-adaptive-emergency", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_headroom_down, "settings-adaptive-headroom-down", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_headroom_up, "settings-adaptive-headroom-up", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_locks, "settings-adaptive-locks", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_logging, "settings-adaptive-logging", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_maximum_down, "settings-adaptive-maximum-down", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_maximum_up, "settings-adaptive-maximum-up", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_minimum_down, "settings-adaptive-minimum-down", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_minimum_up, "settings-adaptive-minimum-up", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_recovery, "settings-adaptive-recovery", FeatureId::settings, kNormal},
    {ActionId::settings_adaptive_shadow, "settings-adaptive-shadow", FeatureId::settings, kNormal},
    {ActionId::settings_advanced_toggle, "settings-advanced-toggle", FeatureId::settings, kRestricted},
    {ActionId::settings_animations, "settings-animations", FeatureId::settings, kNormal},
    {ActionId::settings_corpses_down, "settings-corpses-down", FeatureId::settings, kNormal},
    {ActionId::settings_corpses_up, "settings-corpses-up", FeatureId::settings, kNormal},
    {ActionId::settings_target_down, "settings-target-down", FeatureId::settings, kNormal},
    {ActionId::settings_target_up, "settings-target-up", FeatureId::settings, kNormal},
}};

constexpr std::array<ActionBinding, 68> kBindings{{
    {"dashboard-diagnostics", ActionId::navigate_diagnostics},
    {"dashboard-launch", ActionId::game_launch}, {"dashboard-overlay", ActionId::navigate_overlay},
    {"dashboard-refresh", ActionId::refresh_status}, {"dashboard-settings", ActionId::navigate_settings},
    {"diagnostics-backup", ActionId::optimizer_backup}, {"diagnostics-benchmark-baseline", ActionId::diagnostics_benchmark_baseline},
    {"diagnostics-benchmark-compare", ActionId::diagnostics_benchmark_compare}, {"diagnostics-clear", ActionId::diagnostics_clear},
    {"diagnostics-export", ActionId::diagnostics_export}, {"diagnostics-export-inventory", ActionId::diagnostics_export_inventory},
    {"diagnostics-export-support", ActionId::diagnostics_export_support}, {"diagnostics-flex-audit", ActionId::diagnostics_flex_audit},
    {"diagnostics-flex-restore", ActionId::diagnostics_flex_restore}, {"diagnostics-full-check", ActionId::diagnostics_full_check},
    {"diagnostics-open-data", ActionId::diagnostics_open_data}, {"diagnostics-open-log", ActionId::diagnostics_open_log},
    {"diagnostics-refresh", ActionId::refresh_status}, {"diagnostics-repair-package", ActionId::diagnostics_repair_package},
    {"game-launch", ActionId::game_launch},
    {"game-offline-telemetry", ActionId::game_offline_telemetry}, {"game-open-config", ActionId::game_open_config},
    {"game-open-install", ActionId::game_open_install}, {"game-open-logs", ActionId::game_open_logs},
    {"game-select-install", ActionId::game_select_install},
    {"header-backup", ActionId::optimizer_backup}, {"header-diagnostics", ActionId::navigate_diagnostics},
    {"header-launch", ActionId::game_launch},
    {"header-restore", ActionId::optimizer_restore}, {"optimizer-apply", ActionId::optimizer_apply},
    {"optimizer-backup", ActionId::optimizer_backup}, {"optimizer-export", ActionId::optimizer_export},
    {"optimizer-import", ActionId::optimizer_import}, {"optimizer-open-backups", ActionId::optimizer_open_backups},
    {"optimizer-open-settings", ActionId::navigate_settings}, {"optimizer-preview", ActionId::optimizer_preview},
    {"optimizer-restore", ActionId::optimizer_restore}, {"overlay-position", ActionId::overlay_position},
    {"overlay-scale-down", ActionId::overlay_scale_down}, {"overlay-scale-reset", ActionId::overlay_scale_reset},
    {"overlay-scale-up", ActionId::overlay_scale_up}, {"overlay-show-cpu", ActionId::overlay_show_cpu},
    {"overlay-show-fps", ActionId::overlay_show_fps}, {"overlay-show-frame-time", ActionId::overlay_show_frame_time},
    {"overlay-show-gpu", ActionId::overlay_show_gpu}, {"overlay-show-memory", ActionId::overlay_show_memory},
    {"overlay-toggle", ActionId::overlay_toggle}, {"settings-adaptive-aggressiveness", ActionId::settings_adaptive_aggressiveness},
    {"settings-adaptive-budget", ActionId::settings_adaptive_budget}, {"settings-adaptive-calibration", ActionId::settings_adaptive_calibration},
    {"settings-adaptive-emergency", ActionId::settings_adaptive_emergency}, {"settings-adaptive-headroom-down", ActionId::settings_adaptive_headroom_down},
    {"settings-adaptive-headroom-up", ActionId::settings_adaptive_headroom_up}, {"settings-adaptive-locks", ActionId::settings_adaptive_locks},
    {"settings-adaptive-logging", ActionId::settings_adaptive_logging}, {"settings-adaptive-maximum-down", ActionId::settings_adaptive_maximum_down},
    {"settings-adaptive-maximum-up", ActionId::settings_adaptive_maximum_up}, {"settings-adaptive-minimum-down", ActionId::settings_adaptive_minimum_down},
    {"settings-adaptive-minimum-up", ActionId::settings_adaptive_minimum_up},
    {"settings-adaptive-recovery", ActionId::settings_adaptive_recovery},
    {"settings-adaptive-shadow", ActionId::settings_adaptive_shadow}, {"settings-advanced-toggle", ActionId::settings_advanced_toggle},
    {"settings-animations", ActionId::settings_animations}, {"settings-corpses-down", ActionId::settings_corpses_down},
    {"settings-corpses-up", ActionId::settings_corpses_up}, {"settings-finetuning", ActionId::navigate_optimizer},
    {"settings-target-down", ActionId::settings_target_down}, {"settings-target-up", ActionId::settings_target_up},
}};

constexpr std::array<ControlDefinition, 6> kControls{{
    {ControlId::overlay_scale, "overlay-scale-slider", FeatureId::overlay, 60, 200},
    {ControlId::adaptive_headroom, "settings-adaptive-headroom-slider", FeatureId::settings, 0, 50},
    {ControlId::adaptive_maximum, "settings-adaptive-maximum-slider", FeatureId::settings, 0, 100},
    {ControlId::adaptive_minimum, "settings-adaptive-minimum-slider", FeatureId::settings, 0, 100},
    {ControlId::corpse_limit, "settings-corpses-slider", FeatureId::settings, 4, 2000},
    {ControlId::target_fps, "settings-target-slider", FeatureId::settings,
     optimizer::kTargetFpsMinimum, optimizer::kTargetFpsMaximum},
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
