#include "features/game/game_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::game {
namespace {

namespace product_diagnostics = ::kf2::diagnostics;
namespace product_game = ::kf2::game;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

app::runtime::DispatchResult open_directory(
    app::UiRuntime& runtime, const std::filesystem::path& target) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"GAME_PATH_UNAVAILABLE",
                    L"A verified KF2 installation was not found.");
        return app::runtime::DispatchResult::handled;
    }
    const auto opened = app::open_local_directory(target);
    show_notice(runtime,
                opened.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::warning,
                opened.has_value() ? L"LOCAL_FOLDER_OPENED"
                                   : L"LOCAL_FOLDER_UNAVAILABLE",
                opened.has_value() ? L"The verified local folder was opened."
                                   : opened.error().message);
    return app::runtime::DispatchResult::handled;
}

}  // namespace

app::runtime::DispatchResult select_install(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto selected = app::choose_game_directory(
        runtime.window
            ? static_cast<HWND>(
                  runtime.window->native_handle_for_testing())
            : nullptr);
    if (!selected) {
        show_notice(runtime, ui::NoticeSeverity::info,
                    L"GAME_SELECTION_CANCELLED",
                    L"The KF2 folder selection was cancelled.");
        return app::runtime::DispatchResult::handled;
    }
    if (!runtime.discovery_input) {
        show_notice(
            runtime, ui::NoticeSeverity::warning,
            L"GAME_SELECTION_UNAVAILABLE",
            L"The automatic KF2 config directory must be available before a manual game folder can be verified.");
        return app::runtime::DispatchResult::handled;
    }
    auto validated = product_game::validate_game_candidate(
        *selected, runtime.discovery_input->config_root,
        runtime.discovery_input->allowed_config_parent,
        product_game::DiscoverySource::manual);
    if (!validated.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"GAME_SELECTION_REJECTED",
                    validated.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto encoded = app::path_utf8(validated.value().install_root);
    if (!encoded) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"GAME_SELECTION_REJECTED",
                    L"The selected path cannot be stored as valid UTF-8.");
        return app::runtime::DispatchResult::handled;
    }
    const auto previous = runtime.optimizer_settings.manual_game_path;
    runtime.optimizer_settings.manual_game_path = *encoded;
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings.manual_game_path = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    runtime.installation = std::move(validated.value());
    auto status = runtime.model.status();
    status.game_detected = true;
    status.game = L"Game detected: " +
        runtime.installation->install_root.wstring();
    status.config = ui::ConfigWorkflowState::detected;
    runtime.model.set_status(std::move(status));
    runtime.events->append(
        {0, product_diagnostics::Severity::info,
         "GAME_PATH_SELECTED",
         L"A manually selected KF2 folder passed executable and config-root validation",
         L"discovery"});
    show_notice(runtime, ui::NoticeSeverity::info,
                L"GAME_PATH_SELECTED",
                L"The selected KF2 folder was verified and saved locally.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult open_install(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return runtime.installation
        ? open_directory(runtime, runtime.installation->install_root)
        : open_directory(runtime, {});
}

app::runtime::DispatchResult open_config(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return runtime.installation
        ? open_directory(runtime, runtime.installation->config_root)
        : open_directory(runtime, {});
}

app::runtime::DispatchResult open_logs(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return runtime.installation
        ? open_directory(
              runtime,
              runtime.installation->config_root.parent_path() / L"Logs")
        : open_directory(runtime, {});
}

app::runtime::DispatchResult offline_telemetry(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"GAMEPLAY_LOG_UNAVAILABLE",
                    L"A verified KF2 installation was not found.");
        return app::runtime::DispatchResult::handled;
    }
    if (product_game::find_running_game_process(
            runtime.installation->executable).has_value() ||
        runtime.session_config_snapshot) {
        show_notice(
            runtime, ui::NoticeSeverity::warning,
            L"GAMEPLAY_LOG_GAME_RUNNING",
            L"Close KF2 and finish the protected session before changing offline gameplay data collection.");
        return app::runtime::DispatchResult::handled;
    }
    const bool previous =
        runtime.optimizer_settings.offline_gameplay_telemetry;
    runtime.optimizer_settings.offline_gameplay_telemetry = !previous;
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        runtime.optimizer_settings.offline_gameplay_telemetry = previous;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.offline_gameplay_telemetry =
        runtime.optimizer_settings.offline_gameplay_telemetry;
    runtime.model.set_status(std::move(status));
    show_notice(
        runtime, ui::NoticeSeverity::info,
        L"GAMEPLAY_LOG_MODE_CHANGED",
        runtime.optimizer_settings.offline_gameplay_telemetry
            ? L"Offline gameplay data is enabled for the next session started by this app. The complete INI state is restored automatically."
            : L"Offline gameplay data is disabled. No game INI was changed.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult launch(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"GAME_LAUNCH_UNAVAILABLE",
                    L"A verified KF2 installation was not found.");
        return app::runtime::DispatchResult::handled;
    }
    if (product_game::find_running_game_process(
            runtime.installation->executable).has_value()) {
        show_notice(runtime, ui::NoticeSeverity::info,
                    L"GAME_ALREADY_RUNNING",
                    L"Killing Floor 2 is already running.");
        return app::runtime::DispatchResult::handled;
    }
    if (runtime.session_config_snapshot) {
        show_notice(
            runtime, ui::NoticeSeverity::error,
            L"SESSION_CONFIG_RECOVERY_REQUIRED",
            L"A protected KF2 INI snapshot is still active. Restore it before starting another session.");
        return app::runtime::DispatchResult::handled;
    }
    const bool automatic_flex_launch =
        runtime.start_mode == app::StartMode::normal;
    const bool protected_gameplay_provider =
        app::should_prepare_protected_gameplay_provider(runtime.start_mode);
    auto captured = config::capture_session_config(
        runtime.installation->config_root,
        runtime.settings_path.parent_path());
    if (!captured.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SESSION_CONFIG_SNAPSHOT_FAILED",
                    captured.error().message);
        return app::runtime::DispatchResult::handled;
    }
    runtime.session_config_snapshot = std::move(captured.value());
    runtime.events->append(
        {0, product_diagnostics::Severity::info,
         "SESSION_CONFIG_CAPTURED",
         L"The exact pre-game KF2 INI state was captured before automatic launch actions",
         L"config"});
    const auto automatic = runtime.apply_adaptive_launch_profile();
    if (!automatic.has_value()) {
        const auto detail = automatic.error().message;
        if (runtime.restore_protected_session_config(
                L"Adaptive automatic launch setup failed")) {
            show_notice(runtime, ui::NoticeSeverity::error,
                        L"ADAPTIVE_LAUNCH_SETUP_FAILED", detail);
        }
        return app::runtime::DispatchResult::handled;
    }
    if (automatic_flex_launch) {
        const auto prepared = runtime.ensure_automatic_flex_lab();
        if (!prepared.has_value()) {
            const auto detail = prepared.error().message;
            if (runtime.restore_protected_session_config(
                    L"Automatic FleX hook setup failed")) {
                show_notice(runtime, ui::NoticeSeverity::error,
                            L"FLEX_AUTO_HOOK_FAILED", detail);
            }
            return app::runtime::DispatchResult::handled;
        }
    }
    if (protected_gameplay_provider) {
        const auto telemetry_module =
            product_game::install_offline_telemetry_lab({
                .config_root = runtime.installation->config_root,
                .state_root = runtime.settings_path.parent_path(),
                .module_asset = runtime.executable_root / L"Data" / L"Lab" /
                    L"KF2OptimizerTelemetry.u",
                .game_running = false});
        if (!telemetry_module.has_value()) {
            const auto detail = telemetry_module.error().message;
            if (runtime.restore_protected_session_config(
                    L"Offline telemetry package setup failed")) {
                show_notice(runtime, ui::NoticeSeverity::error,
                            L"GAMEPLAY_TELEMETRY_INSTALL_FAILED", detail);
            }
            return app::runtime::DispatchResult::handled;
        }
        const auto enabled = product_game::enable_offline_gameplay_logging(
            runtime.installation->config_root, true,
            runtime.optimizer_settings.corpse_limit,
            runtime.optimizer_settings.target_fps, true,
            runtime.optimizer_settings.adaptive_quality_change_budget);
        if (!enabled.has_value()) {
            const auto detail = enabled.error().message;
            if (runtime.restore_protected_session_config(
                    L"Offline gameplay data setup failed")) {
                show_notice(runtime, ui::NoticeSeverity::error,
                            L"GAMEPLAY_LOG_ENABLE_FAILED", detail);
            }
            return app::runtime::DispatchResult::handled;
        }
        runtime.events->append(
            {0, product_diagnostics::Severity::info,
             "GAMEPLAY_LOG_LAB_READY",
             L"The protected standalone provider exposes verified KF2 AI/wave telemetry, bounded corpse cleanup, physics, LOD and ragdoll capabilities, plus temporary per-action corpse ID markers; other General Adaptive capabilities remain independent",
             L"game"});
    }
    SHELLEXECUTEINFOW launch_request{};
    launch_request.cbSize = sizeof(launch_request);
    launch_request.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    launch_request.lpVerb = L"open";
    const auto executable = runtime.installation->executable.wstring();
    const auto working_directory =
        runtime.installation->executable.parent_path().wstring();
    launch_request.lpFile = executable.c_str();
    launch_request.lpDirectory = working_directory.c_str();
    constexpr wchar_t offline_telemetry_parameters[] = L"-useunpublished";
    if (protected_gameplay_provider) {
        launch_request.lpParameters = offline_telemetry_parameters;
    }
    launch_request.nShow = SW_SHOWNORMAL;
    // ShellExecuteExW may pump this UI thread while Steam displays its
    // launch-arguments confirmation. Arm the protected-session wait first so
    // a re-entrant telemetry tick cannot misclassify that gap as KF2 closing.
    if (runtime.session_config_snapshot) {
        runtime.session_config_waiting_for_launch = true;
        constexpr std::uint64_t kLaunchSafetyTimeoutNs =
            90ULL * 1'000'000'000ULL;
        const auto now = runtime.monotonic_ns();
        runtime.session_config_launch_deadline_ns =
            now > std::numeric_limits<std::uint64_t>::max() -
                      kLaunchSafetyTimeoutNs
                ? std::numeric_limits<std::uint64_t>::max()
                : now + kLaunchSafetyTimeoutNs;
    }
    if (!ShellExecuteExW(&launch_request)) {
        const DWORD launch_error = GetLastError();
        bool restored = true;
        if (runtime.session_config_snapshot) {
            restored = runtime.restore_protected_session_config(
                L"KF2 launch failed");
        }
        if (restored) {
            show_notice(
                runtime, ui::NoticeSeverity::error, L"GAME_LAUNCH_FAILED",
                L"Killing Floor 2 could not be started (Windows error " +
                    std::to_wstring(launch_error) + L").");
        }
        return app::runtime::DispatchResult::handled;
    }
    show_notice(
        runtime, ui::NoticeSeverity::info, L"GAME_LAUNCH_STARTED",
        L"Killing Floor 2 start was requested with the automatic protected Adaptive plan, corpse provider and adaptive FleX. The verified providers and exact original INIs will be restored after the session.");
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::game
