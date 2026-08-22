#include "features/settings/settings_actions.hpp"

#include "app/application_runtime.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

namespace kf2::features::settings {
namespace {

namespace product_optimizer = ::kf2::optimizer;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

void reset_adaptive_controller(app::UiRuntime& runtime) {
    auto generation = runtime.adaptive_actuation.generation();
    generation.settings = ++runtime.adaptive_settings_generation;
    runtime.adaptive_actuation.rebase(generation, runtime.monotonic_ns());
    runtime.adaptive_governor.reset();
    runtime.adaptive_profile_gate.reset();
    runtime.adaptive_overhead_breaches = 0;
    runtime.adaptive_overhead_frozen = false;
    runtime.adaptive_decision = {};
}

enum class AdaptiveChange {
    aggressiveness,
    budget,
    calibration,
    emergency,
    headroom_down,
    headroom_up,
    locks,
    logging,
    maximum_down,
    maximum_up,
    minimum_down,
    minimum_up,
    recovery,
    shadow,
};

app::runtime::DispatchResult change_adaptive_policy(
    app::UiRuntime& runtime, AdaptiveChange change) {
    const auto previous = runtime.optimizer_settings;
    switch (change) {
        case AdaptiveChange::shadow:
            runtime.optimizer_settings.adaptive_shadow_mode =
                !runtime.optimizer_settings.adaptive_shadow_mode;
            break;
        case AdaptiveChange::aggressiveness:
            if (runtime.optimizer_settings.adaptive_aggressiveness ==
                "conservative") {
                runtime.optimizer_settings.adaptive_aggressiveness =
                    "balanced";
            } else if (
                runtime.optimizer_settings.adaptive_aggressiveness ==
                "balanced") {
                runtime.optimizer_settings.adaptive_aggressiveness =
                    "aggressive";
            } else {
                runtime.optimizer_settings.adaptive_aggressiveness =
                    "conservative";
            }
            break;
        case AdaptiveChange::minimum_down:
            runtime.optimizer_settings.adaptive_minimum_quality = std::max(
                0,
                runtime.optimizer_settings.adaptive_minimum_quality - 5);
            break;
        case AdaptiveChange::minimum_up:
            runtime.optimizer_settings.adaptive_minimum_quality = std::min(
                runtime.optimizer_settings.adaptive_maximum_quality,
                runtime.optimizer_settings.adaptive_minimum_quality + 5);
            break;
        case AdaptiveChange::maximum_down:
            runtime.optimizer_settings.adaptive_maximum_quality = std::max(
                runtime.optimizer_settings.adaptive_minimum_quality,
                runtime.optimizer_settings.adaptive_maximum_quality - 5);
            break;
        case AdaptiveChange::maximum_up:
            runtime.optimizer_settings.adaptive_maximum_quality = std::min(
                100,
                runtime.optimizer_settings.adaptive_maximum_quality + 5);
            break;
        case AdaptiveChange::budget:
            runtime.optimizer_settings.adaptive_quality_change_budget =
                runtime.optimizer_settings.adaptive_quality_change_budget == 5
                    ? 1
                    : runtime.optimizer_settings
                              .adaptive_quality_change_budget + 1;
            break;
        case AdaptiveChange::headroom_down:
            runtime.optimizer_settings.adaptive_headroom_percent = std::max(
                0, runtime.optimizer_settings.adaptive_headroom_percent - 1);
            break;
        case AdaptiveChange::headroom_up:
            runtime.optimizer_settings.adaptive_headroom_percent = std::min(
                50, runtime.optimizer_settings.adaptive_headroom_percent + 1);
            break;
        case AdaptiveChange::emergency:
            runtime.optimizer_settings.adaptive_emergency_enabled =
                !runtime.optimizer_settings.adaptive_emergency_enabled;
            break;
        case AdaptiveChange::recovery:
            runtime.optimizer_settings.adaptive_quality_recovery_enabled =
                !runtime.optimizer_settings
                     .adaptive_quality_recovery_enabled;
            break;
        case AdaptiveChange::locks:
            runtime.optimizer_settings.adaptive_manual_locks_enabled =
                !runtime.optimizer_settings.adaptive_manual_locks_enabled;
            break;
        case AdaptiveChange::calibration:
            runtime.optimizer_settings.adaptive_calibration_enabled =
                !runtime.optimizer_settings.adaptive_calibration_enabled;
            break;
        case AdaptiveChange::logging:
            runtime.optimizer_settings.adaptive_logging =
                !runtime.optimizer_settings.adaptive_logging;
            break;
    }
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    reset_adaptive_controller(runtime);
    auto status = runtime.model.status();
    runtime.update_adaptive_policy_status(status);
    const auto bounded_profile = product_optimizer::bound_adaptive_profile(
        app::stored_adaptive_profile(runtime.optimizer_settings),
        runtime.optimizer_settings.adaptive_minimum_quality,
        runtime.optimizer_settings.adaptive_maximum_quality);
    status.recommended_profile = bounded_profile
        ? std::wstring{product_optimizer::adaptive_profile_label(
              *bounded_profile)}
        : L"not available";
    status.recommendation_ready = bounded_profile.has_value();
    status.recommendation_reason = bounded_profile
        ? L"Automatic profile is ready; fresh telemetry will refine it"
        : L"No verified named profile fits the selected quality limits";
    status.adaptive_state = L"observing";
    status.adaptive_action = L"hold";
    status.adaptive_reason =
        L"Controller policy changed; fresh stabilization is required";
    runtime.model.set_status(std::move(status));
    show_notice(
        runtime, ui::NoticeSeverity::info, L"ADAPTIVE_POLICY_CHANGED",
        L"Adaptive policy saved. Per-capability evidence, individual locks and fail-closed provider rules remain mandatory.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult change_target(app::UiRuntime& runtime,
                                           int direction) {
    const int previous = runtime.optimizer_settings.target_fps;
    runtime.optimizer_settings.target_fps = std::clamp(
        runtime.optimizer_settings.target_fps + direction,
        product_optimizer::kTargetFpsMinimum,
        product_optimizer::kTargetFpsMaximum);
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings.target_fps = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.target_fps = runtime.optimizer_settings.target_fps;
    runtime.model.set_status(std::move(status));
    auto generation = runtime.adaptive_actuation.generation();
    generation.settings = ++runtime.adaptive_settings_generation;
    runtime.adaptive_actuation.rebase(generation, runtime.monotonic_ns());
    // Preserve the Governor instance so it can detect the target generation,
    // invalidate target-dependent filters, and publish a stabilization HOLD.
    runtime.adaptive_profile_gate.reset();
    runtime.adaptive_overhead_breaches = 0;
    runtime.adaptive_overhead_frozen = false;
    show_notice(runtime, ui::NoticeSeverity::info, L"TARGET_FPS_CHANGED",
                L"Target FPS: " +
                    std::to_wstring(runtime.optimizer_settings.target_fps));
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult change_corpse_limit(
    app::UiRuntime& runtime, int direction) {
    int& value = runtime.optimizer_settings.corpse_limit;
    const int previous = value;
    value = std::clamp(value + direction, 4, 2000);
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        value = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.corpse_limit = runtime.optimizer_settings.corpse_limit;
    runtime.model.set_status(std::move(status));
    auto generation = runtime.adaptive_actuation.generation();
    generation.settings = ++runtime.adaptive_settings_generation;
    runtime.adaptive_actuation.rebase(generation, runtime.monotonic_ns());
    show_notice(runtime, ui::NoticeSeverity::info, L"CORPSE_LIMIT_CHANGED",
                L"Adaptive corpse ceiling: " + std::to_wstring(value));
    return app::runtime::DispatchResult::handled;
}

}  // namespace

app::runtime::DispatchResult advanced_toggle(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    auto status = runtime.model.status();
    status.advanced_settings_visible = !status.advanced_settings_visible;
    runtime.model.set_status(std::move(status));
    const auto* focus_action = app::runtime::find_action(
        app::runtime::ActionId::settings_advanced_toggle);
    runtime.controller.focus_target(
        ui::Destination::settings,
        focus_action == nullptr
            ? std::nullopt
            : std::optional<std::string_view>{focus_action->canonical_name});
    runtime.invalidate();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult adaptive_aggressiveness(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::aggressiveness);
}

app::runtime::DispatchResult adaptive_budget(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::budget);
}

app::runtime::DispatchResult adaptive_calibration(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::calibration);
}

app::runtime::DispatchResult adaptive_emergency(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::emergency);
}

app::runtime::DispatchResult adaptive_headroom_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::headroom_down);
}

app::runtime::DispatchResult adaptive_headroom_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::headroom_up);
}

app::runtime::DispatchResult adaptive_locks(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::locks);
}

app::runtime::DispatchResult adaptive_logging(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::logging);
}

app::runtime::DispatchResult adaptive_maximum_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::maximum_down);
}

app::runtime::DispatchResult adaptive_maximum_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::maximum_up);
}

app::runtime::DispatchResult adaptive_minimum_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::minimum_down);
}

app::runtime::DispatchResult adaptive_minimum_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::minimum_up);
}

app::runtime::DispatchResult adaptive_recovery(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::recovery);
}

app::runtime::DispatchResult adaptive_shadow(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_adaptive_policy(runtime, AdaptiveChange::shadow);
}

app::runtime::DispatchResult animations(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const bool previous = runtime.optimizer_settings.animations_enabled;
    runtime.optimizer_settings.animations_enabled = !previous;
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings.animations_enabled = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    runtime.controller.set_animations_enabled(
        runtime.optimizer_settings.animations_enabled);
    runtime.update_animation_cadence();
    auto status = runtime.model.status();
    status.animations_enabled =
        runtime.optimizer_settings.animations_enabled;
    runtime.model.set_status(std::move(status));
    show_notice(
        runtime, ui::NoticeSeverity::info, L"ANIMATIONS_CHANGED",
        runtime.optimizer_settings.animations_enabled
            ? L"Full UI and overlay animations are enabled."
            : L"Animations are reduced; metric values remain live and readable.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult target_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_target(runtime, -1);
}

app::runtime::DispatchResult target_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_target(runtime, 1);
}

app::runtime::DispatchResult corpses_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_corpse_limit(runtime, -1);
}

app::runtime::DispatchResult corpses_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return change_corpse_limit(runtime, 1);
}

app::runtime::DispatchResult updates_automatic(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.toggle_automatic_update_checks();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_check(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.start_update_check(update::CheckTrigger::manual);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_install(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.start_update_install();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_later(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.dismiss_update();
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::settings
