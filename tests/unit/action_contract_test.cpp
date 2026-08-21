#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>

#include "app/runtime/action_contract.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::app::runtime;

    constexpr std::array<std::string_view, 68> kExistingActions{{
        "dashboard-diagnostics", "dashboard-launch",
        "dashboard-overlay", "dashboard-refresh", "dashboard-settings",
        "diagnostics-backup", "diagnostics-benchmark-baseline",
        "diagnostics-benchmark-compare", "diagnostics-clear",
        "diagnostics-export", "diagnostics-export-inventory",
        "diagnostics-export-support", "diagnostics-flex-audit",
        "diagnostics-flex-restore", "diagnostics-full-check",
        "diagnostics-open-data", "diagnostics-open-log",
        "diagnostics-repair-package",
        "diagnostics-refresh", "game-launch", "game-offline-telemetry",
        "game-open-config", "game-open-install", "game-open-logs",
        "game-select-install", "header-backup",
        "header-diagnostics", "header-launch",
        "header-restore", "optimizer-apply", "optimizer-backup",
        "optimizer-export", "optimizer-import", "optimizer-open-backups",
        "optimizer-open-settings", "optimizer-preview", "optimizer-restore",
        "overlay-position", "overlay-scale-down", "overlay-scale-reset",
        "overlay-scale-up", "overlay-show-cpu", "overlay-show-fps",
        "overlay-show-frame-time", "overlay-show-gpu",
        "overlay-show-memory", "overlay-toggle",
        "settings-adaptive-aggressiveness", "settings-adaptive-budget",
        "settings-adaptive-calibration", "settings-adaptive-emergency",
        "settings-adaptive-headroom-down", "settings-adaptive-headroom-up",
        "settings-adaptive-locks", "settings-adaptive-logging",
        "settings-adaptive-maximum-down", "settings-adaptive-maximum-up",
        "settings-adaptive-minimum-down", "settings-adaptive-minimum-up",
        "settings-adaptive-recovery", "settings-adaptive-shadow",
        "settings-advanced-toggle", "settings-animations",
        "settings-corpses-down", "settings-corpses-up",
        "settings-finetuning",
        "settings-target-down", "settings-target-up",
    }};

    constexpr std::array<std::string_view, 6> kExistingControls{{
        "overlay-scale-slider", "settings-adaptive-headroom-slider",
        "settings-adaptive-maximum-slider",
        "settings-adaptive-minimum-slider", "settings-corpses-slider",
        "settings-target-slider",
    }};

    constexpr std::array<ActionId, 84> kStableActionIds{{
        ActionId::navigate_diagnostics,
        ActionId::retired_navigate_guide,
        ActionId::navigate_settings,
        ActionId::navigate_overlay,
        ActionId::navigate_optimizer,
        ActionId::refresh_status,
        ActionId::diagnostics_benchmark_baseline,
        ActionId::diagnostics_benchmark_compare,
        ActionId::diagnostics_clear,
        ActionId::diagnostics_export,
        ActionId::diagnostics_export_inventory,
        ActionId::diagnostics_export_support,
        ActionId::diagnostics_flex_audit,
        ActionId::diagnostics_flex_restore,
        ActionId::diagnostics_full_check,
        ActionId::diagnostics_open_data,
        ActionId::diagnostics_open_log,
        ActionId::game_launch,
        ActionId::game_offline_telemetry,
        ActionId::game_open_config,
        ActionId::game_open_install,
        ActionId::game_open_logs,
        ActionId::game_select_install,
        ActionId::retired_guide_finish,
        ActionId::retired_guide_next,
        ActionId::retired_guide_previous,
        ActionId::retired_guide_reset,
        ActionId::retired_guide_reset_completion,
        ActionId::optimizer_apply,
        ActionId::optimizer_backup,
        ActionId::optimizer_export,
        ActionId::optimizer_import,
        ActionId::retired_optimizer_manual_create,
        ActionId::retired_optimizer_manual_decrease,
        ActionId::retired_optimizer_manual_increase,
        ActionId::retired_optimizer_manual_load,
        ActionId::retired_optimizer_manual_lock,
        ActionId::retired_optimizer_manual_next,
        ActionId::retired_optimizer_manual_next_group,
        ActionId::retired_optimizer_manual_previous,
        ActionId::retired_optimizer_manual_previous_group,
        ActionId::retired_optimizer_manual_reset,
        ActionId::optimizer_open_backups,
        ActionId::optimizer_preview,
        ActionId::optimizer_restore,
        ActionId::overlay_position,
        ActionId::overlay_scale_down,
        ActionId::overlay_scale_reset,
        ActionId::overlay_scale_up,
        ActionId::overlay_show_cpu,
        ActionId::overlay_show_fps,
        ActionId::overlay_show_frame_time,
        ActionId::overlay_show_gpu,
        ActionId::overlay_show_memory,
        ActionId::overlay_toggle,
        ActionId::settings_adaptive_aggressiveness,
        ActionId::settings_adaptive_budget,
        ActionId::settings_adaptive_calibration,
        ActionId::settings_adaptive_emergency,
        ActionId::settings_adaptive_headroom_down,
        ActionId::settings_adaptive_headroom_up,
        ActionId::settings_adaptive_locks,
        ActionId::settings_adaptive_logging,
        ActionId::settings_adaptive_maximum_down,
        ActionId::settings_adaptive_maximum_up,
        ActionId::settings_adaptive_minimum_down,
        ActionId::settings_adaptive_minimum_up,
        ActionId::settings_adaptive_recovery,
        ActionId::settings_adaptive_shadow,
        ActionId::settings_advanced_toggle,
        ActionId::settings_animations,
        ActionId::settings_corpses_down,
        ActionId::settings_corpses_up,
        ActionId::retired_settings_flex_down,
        ActionId::retired_settings_flex_up,
        ActionId::retired_settings_gore_down,
        ActionId::retired_settings_gore_up,
        ActionId::retired_settings_mode_adaptive,
        ActionId::retired_settings_mode_manual,
        ActionId::retired_settings_session_config,
        ActionId::settings_target_down,
        ActionId::settings_target_up,
        ActionId::settings_adaptive_online,
        ActionId::diagnostics_repair_package,
    }};

    for (std::size_t index = 0; index < kStableActionIds.size(); ++index) {
        CHECK(static_cast<std::uint16_t>(kStableActionIds[index]) == index);
    }

    CHECK(action_bindings().size() == 68);
    CHECK(action_definitions().size() == 60);
    CHECK(control_definitions().size() == kExistingControls.size());

    for (const auto name : kExistingActions) {
        const auto request = parse_action(name);
        CHECK(request.has_value());
        CHECK(std::holds_alternative<NoPayload>(request->payload));
        CHECK(find_action(request->id) != nullptr);
    }
    for (const auto name : kExistingControls) {
        CHECK(find_control(name) != nullptr);
    }
    CHECK(!parse_action("unknown-action").has_value());
    CHECK(!parse_action("settings-mode-manual").has_value());
    CHECK(!parse_action("settings-mode-adaptive").has_value());
    CHECK(!parse_action("settings-session-config").has_value());
    CHECK(!parse_action("settings-gore-down").has_value());
    CHECK(!parse_action("optimizer-manual-create").has_value());
    CHECK(!parse_action("optimizer-manual-load").has_value());
    CHECK(!parse_action("header-guide").has_value());
    CHECK(!parse_action("guide-next").has_value());
    CHECK(!parse_action("settings-flex-down").has_value());
    CHECK(find_control("unknown-slider") == nullptr);

    std::set<std::string_view> binding_names;
    std::set<ActionId> binding_ids;
    for (const auto& binding : action_bindings()) {
        CHECK(binding_names.insert(binding.name).second);
        binding_ids.insert(binding.id);
    }
    CHECK(binding_ids.size() == action_definitions().size());

    std::set<ActionId> definition_ids;
    std::set<std::string_view> canonical_names;
    std::array<std::size_t, 7> feature_counts{};
    for (const auto& definition : action_definitions()) {
        CHECK(definition_ids.insert(definition.id).second);
        CHECK(canonical_names.insert(definition.canonical_name).second);
        CHECK(definition.payload_kind == ActionPayloadKind::no_payload);
        ++feature_counts[static_cast<std::size_t>(definition.feature)];
        const auto parsed = parse_action(definition.canonical_name);
        CHECK(parsed.has_value());
        CHECK(parsed->id == definition.id);
    }

    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::navigation)] == 4);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::game)] == 6);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::settings)] == 20);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::overlay)] == 10);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::diagnostics)] == 13);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::optimizer)] == 4);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::backup)] == 3);

    constexpr std::array<ActionId, 4> kNavigationIds{{
        ActionId::navigate_diagnostics,
        ActionId::navigate_settings,
        ActionId::navigate_overlay,
        ActionId::navigate_optimizer,
    }};
    for (const auto id : kNavigationIds) {
        const auto* definition = find_action(id);
        CHECK(definition != nullptr);
        CHECK(definition->feature == FeatureId::navigation);
    }

    CHECK(find_action(ActionId::retired_settings_mode_manual) == nullptr);
    CHECK(find_action(ActionId::retired_settings_mode_adaptive) == nullptr);
    CHECK(find_action(ActionId::retired_navigate_guide) == nullptr);
    CHECK(find_action(ActionId::retired_guide_finish) == nullptr);
    CHECK(find_action(ActionId::retired_settings_flex_down) == nullptr);

    std::set<ControlId> control_ids;
    std::set<std::string_view> control_names;
    std::size_t overlay_controls = 0;
    std::size_t settings_controls = 0;
    for (const auto& control : control_definitions()) {
        CHECK(control_ids.insert(control.id).second);
        CHECK(control_names.insert(control.name).second);
        if (control.feature == FeatureId::overlay) ++overlay_controls;
        if (control.feature == FeatureId::settings) ++settings_controls;
    }
    CHECK(overlay_controls == 1);
    CHECK(settings_controls == 5);

    CHECK(parse_action("header-launch")->id ==
          parse_action("game-launch")->id);
    CHECK(parse_action("dashboard-launch")->id ==
          parse_action("game-launch")->id);
    CHECK(parse_action("header-backup")->id ==
          parse_action("optimizer-backup")->id);
    CHECK(parse_action("diagnostics-backup")->id ==
          parse_action("optimizer-backup")->id);
    CHECK(parse_action("header-restore")->id ==
          parse_action("optimizer-restore")->id);
    CHECK(parse_action("dashboard-refresh")->id ==
          parse_action("diagnostics-refresh")->id);
    CHECK(parse_action("dashboard-diagnostics")->id ==
          parse_action("header-diagnostics")->id);
    CHECK(parse_action("optimizer-open-settings")->id ==
          parse_action("dashboard-settings")->id);

    CHECK(!requires_normal_mode(ActionId::game_launch, {.protected_game_launch = false}));
    CHECK(requires_normal_mode(ActionId::game_launch, {.protected_game_launch = true}));
    CHECK(requires_normal_mode(ActionId::optimizer_backup, {}));
    CHECK(!requires_normal_mode(
        ActionId::retired_settings_mode_adaptive, {}));
    CHECK(!requires_normal_mode(ActionId::settings_adaptive_online, {}));
    static_assert(static_cast<std::uint16_t>(ActionId::settings_adaptive_online) >
                  static_cast<std::uint16_t>(ActionId::settings_target_up));
    CHECK(!requires_normal_mode(ActionId::diagnostics_export, {}));
    CHECK(!requires_normal_mode(ActionId::diagnostics_benchmark_baseline, {}));
    CHECK(!requires_normal_mode(ActionId::diagnostics_repair_package, {}));

    const auto launch = resolve_action(
        "header-launch", {.protected_game_launch = true});
    CHECK(launch.has_value());
    CHECK(launch->id == ActionId::game_launch);
    CHECK(launch->canonical_name == "game-launch");
    CHECK(launch->normal_mode_required);

    const auto refresh = resolve_action("dashboard-refresh", {});
    CHECK(refresh.has_value());
    CHECK(refresh->id == ActionId::refresh_status);
    CHECK(refresh->canonical_name == "diagnostics-refresh");
    CHECK(!refresh->normal_mode_required);

    CHECK(!resolve_action("settings-adaptive-online", {}).has_value());

    CHECK(find_action(ActionId::settings_adaptive_online) == nullptr);

    CHECK(!resolve_action("unknown-action", {}).has_value());

    const auto* target = find_control("settings-target-slider");
    const auto* corpses = find_control("settings-corpses-slider");
    const auto* gore = find_control("settings-gore-slider");
    const auto* scale = find_control("overlay-scale-slider");
    CHECK(target && target->minimum == 30 && target->maximum == 240);
    CHECK(corpses && corpses->minimum == 4 && corpses->maximum == 2000);
    CHECK(gore == nullptr);
    CHECK(find_control("settings-flex-slider") == nullptr);
    CHECK(scale && scale->minimum == 60 && scale->maximum == 200);
    CHECK(clamp_control_value(*target, 10) == 30);
    CHECK(clamp_control_value(*target, 500) == 240);
    for (int value = 4; value <= 2000; ++value) {
        const auto request = resolve_control("settings-corpses-slider", value);
        CHECK(request.has_value());
        CHECK(request->value == value);
    }

    for (int requested = 30; requested <= 240; ++requested) {
        const auto exact = resolve_control("settings-target-slider", requested);
        CHECK(exact.has_value());
        CHECK(exact->value == requested);
    }

    const auto target_request = resolve_control("settings-target-slider", 500);
    CHECK(target_request.has_value());
    CHECK(target_request->id == ControlId::target_fps);
    CHECK(target_request->value == 240);

    const auto corpse_request = resolve_control("settings-corpses-slider", 1);
    CHECK(corpse_request.has_value());
    CHECK(corpse_request->id == ControlId::corpse_limit);
    CHECK(corpse_request->value == 4);

    CHECK(!resolve_control("unknown-slider", 100).has_value());

    return EXIT_SUCCESS;
}
