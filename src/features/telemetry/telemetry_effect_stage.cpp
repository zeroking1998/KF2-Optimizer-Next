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
    const bool diagnostics_match = runtime.last_flex_observation &&
        runtime.last_flex_observation->diagnostics_enabled ==
            runtime.optimizer_settings.debug_flex_diagnostics;
    if (action->status == optimizer::AdaptiveActionStatus::applied &&
        diagnostics_match) {
        return;
    }
    const bool write_succeeded = flex::write_fixed_control(
        *runtime.game_process,
        runtime.optimizer_settings.debug_flex_diagnostics);
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
        effect.constrained != runtime.flex_minimum_limited) {
        runtime.flex_minimum_limited = effect.constrained;
        runtime.events->append(
            {0, diagnostics::Severity::info,
             effect.constrained ? "FLEX_MINIMUM_LIMIT_REQUESTED"
                                : "FLEX_MINIMUM_LIMIT_RELEASED",
             effect.constrained
                 ? L"User-enabled FleX requested the fixed minimum solver level of one; applied status waits for shared-memory readback"
                 : L"The fixed FleX minimum limit was released; applied status waits for shared-memory readback",
             L"flex"});
    }
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {
namespace {

struct FlexTransactionState {
    bool marker_exists{};
    bool original_exists{};
};

Result<FlexTransactionState> inspect_flex_transaction_state(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory) {
    std::error_code error;
    const bool marker_exists = std::filesystem::exists(
        state_directory / L"flex-lab-transaction.marker", error);
    if (error) {
        return Result<FlexTransactionState>::failure(
            {ErrorCode::io_failure,
             L"FleX transaction marker status cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    const bool original_exists = std::filesystem::exists(
        game_directory / L"flexRelease_original.dll", error);
    if (error) {
        return Result<FlexTransactionState>::failure(
            {ErrorCode::io_failure,
             L"FleX original runtime status cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<FlexTransactionState>::success(
        {marker_exists, original_exists});
}

}  // namespace

Result<bool> UiRuntime::ensure_fixed_flex_runtime() {
    if (!installation) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"A verified KF2 installation was not found", 0});
    }
    const auto game_directory = installation->install_root /
        L"Binaries" / L"Win64";
    const auto state_directory =
        settings_path.parent_path() / L"flex-lab";
    const auto preserved_original = game_directory /
        L"flexRelease_original.dll";

    const auto transaction = inspect_flex_transaction_state(
        game_directory, state_directory);
    if (!transaction.has_value()) {
        return Result<bool>::failure(transaction.error());
    }
    if (transaction.value().marker_exists ||
        transaction.value().original_exists) {
        std::wstring recovery_details;
        const auto recovered = flex::recover_offline_lab(
            game_directory, state_directory, false, &recovery_details);
        if (!recovered.has_value()) {
            return Result<bool>::failure(recovered.error());
        }
        if (recovered.value()) {
            events->append({0, diagnostics::Severity::info,
                "FLEX_LAB_RECOVERED", recovery_details, L"flex"});
        }
        const auto retained = inspect_flex_transaction_state(
            game_directory, state_directory);
        if (!retained.has_value()) {
            return Result<bool>::failure(retained.error());
        }
        if (retained.value().marker_exists &&
            retained.value().original_exists) {
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
        "FLEX_MINIMUM_HOOK_READY",
        L"The verified offline FleX hook was prepared for the fixed minimum solver level",
        L"flex"});
    return Result<bool>::success(true);
}

bool UiRuntime::restore_fixed_flex_runtime(std::wstring_view reason) {
    if (!installation) return true;
    const auto game_directory = installation->install_root /
        L"Binaries" / L"Win64";
    const auto state_directory =
        settings_path.parent_path() / L"flex-lab";
    const auto transaction = inspect_flex_transaction_state(
        game_directory, state_directory);
    if (!transaction.has_value()) {
        events->append({0, diagnostics::Severity::error,
            "FLEX_FIXED_RESTORE_FAILED", transaction.error().message,
            L"flex"});
        model.set_recovery_required(true);
        model.set_notice({ui::NoticeSeverity::error,
            L"FLEX_FIXED_RESTORE_FAILED", transaction.error().message,
            L"Do not start KF2 again until the original FleX runtime is restored."});
        invalidate();
        return false;
    }
    if (!transaction.value().marker_exists &&
        !transaction.value().original_exists) {
        return true;
    }
    const bool running = game::game_process_may_be_running(
        installation->executable);
    std::wstring recovery_details;
    const auto restored = flex::restore_offline_lab(
        game_directory, state_directory, running, &recovery_details);
    if (!restored.has_value()) {
        events->append({0, diagnostics::Severity::error,
            "FLEX_FIXED_RESTORE_FAILED", restored.error().message,
            L"flex"});
        model.set_recovery_required(true);
        model.set_notice({ui::NoticeSeverity::error,
            L"FLEX_FIXED_RESTORE_FAILED", restored.error().message,
            L"Do not start KF2 again until the original FleX runtime is restored."});
        invalidate();
        return false;
    }
    events->append({0, diagnostics::Severity::info,
        "FLEX_FIXED_RESTORED",
        std::wstring{reason} +
            L"; the original FleX runtime was restored and verified. " +
            recovery_details,
        L"flex"});
    return true;
}

bool UiRuntime::restore_protected_session_config(std::wstring_view reason) {
    if (final_graphics_capture_pending) {
        model.set_recovery_required(true);
        return false;
    }
    if (installation) {
        const auto running = game::find_running_game_process(installation->executable);
        if (running.has_value() || running.error().code != ErrorCode::not_found) {
            const auto message = running.has_value()
                ? L"KF2 is still running. Protected INIs and runtime packages were retained."
                : L"KF2 process enumeration could not confirm that the game has ended. Protected state was retained.";
            model.set_recovery_required(true);
            events->append({0, diagnostics::Severity::warning,
                "PROTECTED_SESSION_RESTORE_DEFERRED",
                std::wstring{reason} + L"; " + message, L"config"});
            model.set_notice({ui::NoticeSeverity::warning,
                L"PROTECTED_SESSION_RESTORE_DEFERRED", message,
                L"Close KF2, then restart KF2 Optimizer to complete protected recovery."});
            invalidate();
            return false;
        }
    }
    if (installation) {
        const auto cap_recovered = game::recover_frame_rate_cap(
            *installation, settings_path.parent_path());
        if (!cap_recovered.has_value()) {
            model.set_recovery_required(true);
            events->append({0, diagnostics::Severity::error,
                "TARGET_FPS_RECOVERY_BLOCKED", cap_recovered.error().message,
                L"config"});
            model.set_notice({ui::NoticeSeverity::error,
                L"TARGET_FPS_RECOVERY_BLOCKED", cap_recovered.error().message,
                L"Protected INIs and their snapshot were retained. Close KF2 and restart the optimizer after the file lock is released."});
            return false;
        }
    }
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
        const auto restored = restore_session_video_settings();
        if (!restored.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "SESSION_CONFIG_RESTORE_FAILED", restored.error().message,
                L"config"});
            model.set_recovery_required(true);
            model.set_notice({ui::NoticeSeverity::error,
                L"SESSION_CONFIG_RESTORE_FAILED", restored.error().message,
                L"The protected originals and confirmed graphics replay remain retained. Keep KF2 closed and restart the Optimizer to retry recovery."});
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
        }
    }
    if (!restore_fixed_flex_runtime(reason)) complete = false;
    if (installation) {
        const auto capped = synchronize_frame_rate_cap();
        if (!capped.has_value()) {
            complete = false;
            model.set_recovery_required(true);
            events->append({0, diagnostics::Severity::error,
                "TARGET_FPS_PERSIST_FAILED", capped.error().message,
                L"config"});
            model.set_notice({ui::NoticeSeverity::error,
                L"TARGET_FPS_PERSIST_FAILED",
                L"Required native FPS-cap synchronization failed: " +
                    capped.error().message,
                L"Keep KF2 closed until the native cap can be written and verified."});
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
