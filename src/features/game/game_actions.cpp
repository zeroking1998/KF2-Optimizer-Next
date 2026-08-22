#include "features/game/game_actions.hpp"

#include "app/application_runtime.hpp"
#include "kf2/config/setting_catalog.hpp"

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
    runtime.reload_video_settings();
    runtime.reload_advanced_settings();
    auto status = runtime.model.status();
    status.game_detected = true;
    status.game = L"Game detected: " +
        runtime.installation->install_root.wstring();
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

app::runtime::DispatchResult open_config(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return runtime.installation
        ? open_directory(runtime, runtime.installation->config_root)
        : open_directory(runtime, {});
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
    const bool automatic_external_profile_ready =
        runtime.session_config_snapshot &&
        runtime.session_config_waiting_for_launch &&
        runtime.session_config_launch_deadline_ns == 0;
    if (runtime.session_config_snapshot &&
        !automatic_external_profile_ready) {
        show_notice(
            runtime, ui::NoticeSeverity::error,
            L"SESSION_CONFIG_RECOVERY_REQUIRED",
            L"A protected KF2 INI snapshot is still active. Restore it before starting another session.");
        return app::runtime::DispatchResult::handled;
    }
    if (!automatic_external_profile_ready) {
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
    }
    const auto captured_values = config::read_catalog_values(
        runtime.session_config_snapshot->snapshot_root / L"files");
    if (!captured_values.has_value()) {
        const auto detail = captured_values.error().message;
        if (runtime.restore_protected_session_config(
                L"The captured FleX setting could not be verified")) {
            show_notice(runtime, ui::NoticeSeverity::error,
                        L"FLEX_SETTING_VERIFICATION_FAILED", detail);
        }
        return app::runtime::DispatchResult::handled;
    }
    const auto physx = captured_values.value().find(
        config::SettingId::physx_level);
    const auto* configured_physx_level =
        physx == captured_values.value().end()
            ? nullptr : std::get_if<int>(&physx->second);
    if (!configured_physx_level) {
        if (runtime.restore_protected_session_config(
                L"The captured FleX setting was unavailable")) {
            show_notice(runtime, ui::NoticeSeverity::error,
                        L"FLEX_SETTING_VERIFICATION_FAILED",
                        L"The user's KF2 FleX setting could not be verified.");
        }
        return app::runtime::DispatchResult::handled;
    }
    const bool automatic_flex_launch =
        app::should_prepare_adaptive_flex_runtime(
            runtime.start_mode, *configured_physx_level);
    if (!automatic_external_profile_ready) {
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
    } else {
        runtime.events->append(
            {0, product_diagnostics::Severity::info,
             "ADAPTIVE_EXTERNAL_LAUNCH_REUSED",
             L"The already verified external-launch profile was reused for the app-started KF2 session",
             L"optimizer"});
    }
    if (!automatic_external_profile_ready) {
        const auto capabilities =
            runtime.prepare_automatic_protected_launch_capabilities();
        if (!capabilities.has_value()) {
            const auto detail = capabilities.error().message;
            if (runtime.restore_protected_session_config(
                    L"Automatic launch capability setup failed")) {
                show_notice(runtime, ui::NoticeSeverity::error,
                            L"AUTOMATIC_LAUNCH_CAPABILITY_FAILED", detail);
            }
            return app::runtime::DispatchResult::handled;
        }
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
        automatic_flex_launch
            ? L"Killing Floor 2 start was requested with the protected Adaptive plan, corpse provider and user-enabled FleX. The verified providers and exact original INIs will be restored after the session."
            : L"Killing Floor 2 start was requested with the protected Adaptive plan and corpse provider. FleX remains off because the user did not enable it in KF2. The verified provider and exact original INIs will be restored after the session.");
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::game
