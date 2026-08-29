#include "features/telemetry/telemetry_effect_stage.hpp"

#include "app/application_runtime.hpp"

namespace kf2::telemetry_pipeline {

void apply_flex_control_effect(app::UiRuntime& runtime,
                               const FlexControlEffect& effect) {
    if (!runtime.game_process) return;
    const auto now_ns = runtime.monotonic_ns();
    const std::optional<double> observed = runtime.last_flex_observation
        ? std::optional<double>{static_cast<double>(
              runtime.last_flex_observation->last_forwarded_substeps)}
        : runtime.adaptive_actuation.effective_value(
              optimizer::AdaptiveControlId::flex_solver_substeps);
    const auto& proposed = runtime.adaptive_actuation.propose(
        optimizer::AdaptiveControlId::flex_solver_substeps,
        static_cast<double>(effect.requested_substeps), observed,
        effect.capability, now_ns, "flex_shared_memory");
    if (proposed.status == optimizer::AdaptiveActionStatus::proposed) {
        static_cast<void>(runtime.adaptive_actuation.dispatch(
            optimizer::AdaptiveControlId::flex_solver_substeps, now_ns));
    }
    const auto* action = runtime.adaptive_actuation.current(
        optimizer::AdaptiveControlId::flex_solver_substeps);
    if (!action ||
        (action->status != optimizer::AdaptiveActionStatus::pending &&
         action->status != optimizer::AdaptiveActionStatus::applied)) {
        return;
    }
    const bool write_succeeded = flex::write_adaptive_control(
        *runtime.game_process, effect.requested_substeps);
    if (!write_succeeded &&
        action->status == optimizer::AdaptiveActionStatus::pending) {
        static_cast<void>(runtime.adaptive_actuation.receive({
            action->action_id,
            action->control,
            optimizer::AdaptiveActionStatus::failed,
            action->requested_value,
            {},
            action->generation,
            now_ns,
            "flex_shared_memory",
            "control_write_failed"}));
        return;
    }
    if (write_succeeded &&
        effect.constrained != runtime.flex_adaptive_constrained) {
        runtime.flex_adaptive_constrained = effect.constrained;
        runtime.events->append(
            {0, diagnostics::Severity::info,
             effect.constrained ? "FLEX_ADAPTIVE_CONSTRAINED"
                                : "FLEX_ADAPTIVE_RECOVERED",
             effect.constrained
                 ? L"Adaptive FleX requested a hysteresis-stabilized solver level from 1 to 5; applied status waits for shared-memory readback"
                 : L"Adaptive FleX requested passthrough; applied status waits for shared-memory readback",
             L"flex"});
    }
}

void apply_adaptive_profile_effect(
    app::UiRuntime& runtime, const AdaptiveProfileEffect& effect,
    ui::UiStatus& status) {
    const std::string selected{
        optimizer::adaptive_profile_token(effect.profile)};
    if (selected == runtime.optimizer_settings.optimizer_profile) return;
    const auto previous = runtime.optimizer_settings.optimizer_profile;
    runtime.optimizer_settings.optimizer_profile = selected;
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings.optimizer_profile = previous;
        runtime.adaptive_profile_gate = {};
        runtime.events->append(
            {0, diagnostics::Severity::error,
             "ADAPTIVE_BASELINE_SAVE_FAILED", saved.error().message,
             L"optimizer"});
        return;
    }
    status.profile = std::wstring{selected.begin(), selected.end()};
    runtime.events->append(
        {0, diagnostics::Severity::info, "ADAPTIVE_PROFILE_SELECTED",
         L"Adaptive saved a sustained active-gameplay recommendation for the next protected automatic launch",
         L"optimizer"});
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {

Result<bool> UiRuntime::ensure_automatic_flex_lab() {
    if (!installation) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"A verified KF2 installation was not found", 0});
    }
    const auto game_directory = installation->install_root /
        L"Binaries" / L"Win64";
    const auto state_directory =
        settings_path.parent_path() / L"flex-lab";
    const auto marker = state_directory /
        L"flex-lab-transaction.marker";
    const auto preserved_original = game_directory /
        L"flexRelease_original.dll";

    if (std::filesystem::exists(marker) ||
        std::filesystem::exists(preserved_original)) {
        const auto recovered = flex::recover_offline_lab(
            game_directory, state_directory, false);
        if (!recovered.has_value()) {
            return Result<bool>::failure(recovered.error());
        }
        if (std::filesystem::exists(marker) &&
            std::filesystem::exists(preserved_original)) {
            const auto audit =
                flex::audit_runtime(preserved_original, true);
            if (audit.has_value() && audit.value().exact_known_runtime) {
                return Result<bool>::success(false);
            }
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"The existing FleX hook transaction is not verified",
                 0});
        }
    }

    const auto audit = flex::audit_runtime(
        game_directory / L"flexRelease_x64.dll");
    if (!audit.has_value()) {
        return Result<bool>::failure(audit.error());
    }
    if (!audit.value().exact_known_runtime) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"The installed FleX runtime is not the verified KF2 build",
             0});
    }

    const auto installed = flex::install_offline_lab({
        .game_directory = game_directory,
        .state_directory = state_directory,
        .forwarder_dll = settings_path.parent_path() / L"Lab" /
            L"flexRelease_x64.forwarder-lab.dll",
        .game_running = false,
        .exact_runtime_verified = true,
        .offline_confirmed = true});
    if (!installed.has_value()) {
        return Result<bool>::failure(installed.error());
    }
    events->append({0, diagnostics::Severity::info,
        "FLEX_AUTO_HOOK_READY",
        L"The verified offline FleX hook was prepared automatically for Adaptive control",
        L"flex"});
    return Result<bool>::success(true);
}

bool UiRuntime::restore_automatic_flex_lab(std::wstring_view reason) {
    if (!installation) return true;
    const auto game_directory = installation->install_root /
        L"Binaries" / L"Win64";
    const auto state_directory =
        settings_path.parent_path() / L"flex-lab";
    if (!std::filesystem::exists(
            state_directory / L"flex-lab-transaction.marker") &&
        !std::filesystem::exists(
            game_directory / L"flexRelease_original.dll")) {
        return true;
    }
    const bool running = game::find_running_game_process(
        installation->executable).has_value();
    const auto restored = flex::restore_offline_lab(
        game_directory, state_directory, running);
    if (!restored.has_value()) {
        events->append({0, diagnostics::Severity::error,
            "FLEX_AUTO_RESTORE_FAILED", restored.error().message,
            L"flex"});
        model.set_recovery_required(true);
        model.set_notice({ui::NoticeSeverity::error,
            L"FLEX_AUTO_RESTORE_FAILED", restored.error().message,
            L"Do not start KF2 again until the original FleX runtime is restored."});
        invalidate();
        return false;
    }
    events->append({0, diagnostics::Severity::info,
        "FLEX_AUTO_RESTORED",
        std::wstring{reason} +
            L"; the original FleX runtime was restored and verified",
        L"flex"});
    return true;
}

bool UiRuntime::restore_protected_session_config(std::wstring_view reason) {
    game_restart_handoff_previous_process.reset();
    game_restart_handoff_deadline_ns = 0;
    game_restart_handoff_new_settings = false;
    bool complete = true;
    // Restore the native viewport/INI state even if Windows still has a
    // runtime file open. Each recovery surface is independent, so one busy
    // file must not prevent the remaining protected state from being fixed.
    if (installation) {
        const auto module_restored =
            game::restore_offline_telemetry_lab(
                installation->config_root,
                settings_path.parent_path(),
                false);
        if (!module_restored.has_value() &&
            module_restored.error().code != ErrorCode::not_found) {
            events->append({0, diagnostics::Severity::error,
                "OFFLINE_TELEMETRY_CLEANUP_FAILED",
                module_restored.error().message, L"game"});
            model.set_recovery_required(true);
            model.set_notice({ui::NoticeSeverity::error,
                L"OFFLINE_TELEMETRY_CLEANUP_FAILED",
                module_restored.error().message,
                L"Do not start KF2 again until the telemetry package is safely removed."});
            complete = false;
        }
        if (module_restored.has_value() && module_restored.value()) {
            events->append({0, diagnostics::Severity::info,
                "OFFLINE_TELEMETRY_REMOVED",
                L"The hash-bound KF2 offline telemetry package was removed after the session",
                L"game"});
        }
    }
    if (session_config_snapshot) {
        const auto restored =
            config::restore_session_config(*session_config_snapshot);
        if (!restored.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "SESSION_CONFIG_RESTORE_FAILED", restored.error().message,
                L"config"});
            model.set_recovery_required(true);
            model.set_notice({ui::NoticeSeverity::error,
                L"SESSION_CONFIG_RESTORE_FAILED", restored.error().message,
                L"Do not start KF2 again until the protected INI snapshot is restored."});
            complete = false;
        } else {
            events->append({0, diagnostics::Severity::info,
                "SESSION_CONFIG_RESTORED",
                std::wstring{reason} + L"; " +
                    std::to_wstring(restored.value()) +
                    L" protected INI files were restored and verified; temporal anti-aliasing remains disabled",
                L"config"});
            if (complete) {
                model.set_notice({ui::NoticeSeverity::info,
                    L"SESSION_CONFIG_RESTORED",
                    std::wstring{reason} + L"; " +
                        std::to_wstring(restored.value()) +
                        L" protected INI files were restored and verified.", L""});
            }
            session_config_snapshot.reset();
            session_config_waiting_for_launch = false;
            session_config_launch_deadline_ns = 0;
        }
    }
    if (!restore_automatic_flex_lab(reason)) complete = false;
    if (installation) {
        const auto capped = synchronize_frame_rate_cap();
        if (!capped.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "TARGET_FPS_PERSIST_FAILED", capped.error().message,
                L"config"});
        } else if (capped.value().changed) {
            events->append({0, diagnostics::Severity::info,
                "TARGET_FPS_PERSISTED",
                std::wstring{reason} +
                    L"; KF2's native startup cap was restored to " +
                    std::to_wstring(capped.value().target_fps) + L" FPS",
                L"config"});
        }
    }
    // The credential belongs to the protected KF2 session, not to a single
    // telemetry binding. Recoverable rebinds preserve it in detach_telemetry;
    // only final configuration teardown invalidates the in-memory copy.
    adaptive_control_token.clear();
    adaptive_control_sequence = 0;
    adaptive_control_pending.reset();
    invalidate();
    return complete;
}

}  // namespace kf2::app
