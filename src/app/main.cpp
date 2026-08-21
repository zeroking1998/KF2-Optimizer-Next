#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <fstream>
#include <iterator>
#include <shellapi.h>

#include "kf2/app/application.hpp"
#include "kf2/app/build_identity.hpp"
#include "kf2/app/state_paths.hpp"
#include "kf2/config/settings.hpp"
#include "kf2/core/result.hpp"
#include "kf2/diagnostics/crash_recorder.hpp"
#include "kf2/game/game_discovery.hpp"
#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/platform/windows/process_security.hpp"
#include "kf2/security/package_integrity.hpp"

namespace {

int show_fatal(const kf2::Error& error, int exit_code) {
    std::wstring message = error.message;
    if (error.native_code != 0) {
        message += L"\n\nWindows error: " + std::to_wstring(error.native_code);
    }
    MessageBoxW(nullptr, message.c_str(), L"KF2 Optimizer Next",
                MB_OK | MB_ICONERROR);
    return exit_code;
}

std::uint64_t current_process_start_id() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == FALSE) {
        return 0;
    }
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32U) |
           creation.dwLowDateTime;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show_command) {
    namespace windows = kf2::platform::windows;

    const auto secured_search = windows::harden_process_dll_search();
    if (!secured_search.has_value()) {
        return show_fatal(secured_search.error(), 9);
    }

    const auto executable_directory = windows::executable_directory();
    if (!executable_directory.has_value()) {
        return show_fatal(executable_directory.error(), 10);
    }

    const auto build = kf2::app::current_build_identity();
    const auto package_audit = kf2::security::audit_package_integrity(
        executable_directory.value(), build.commit);
    if (!package_audit.has_value()) {
        return show_fatal(package_audit.error(), 16);
    }
    std::wstring package_warning;
    const bool missing_release_manifest =
        build.channel == "release" && !package_audit.value().managed_package;
    const bool damaged_package = missing_release_manifest ||
        (package_audit.value().managed_package &&
         !package_audit.value().verified);
    if (damaged_package) {
        package_warning = (missing_release_manifest
                ? L"The Release executable is outside its verified portable package"
                : package_audit.value().message) +
            L". The program started in Safe Mode. Re-extract the complete "
            L"portable package; existing Data settings, logs and backups can "
            L"be kept.";
    }

    std::filesystem::path local_app_data;
    const auto discovered_app_data = windows::local_app_data_directory();
    if (discovered_app_data.has_value()) {
        local_app_data = discovered_app_data.value();
    }

    const auto state_location = kf2::app::choose_state_location(
        executable_directory.value(), local_app_data,
        [](const std::filesystem::path& path) {
            return windows::probe_writable_directory(path);
        });
    if (!state_location.has_value()) {
        return show_fatal(state_location.error(), 11);
    }

    const auto identity = kf2::app::format_build_identity(
        kf2::app::current_build_identity());
    auto crash_recorder = kf2::diagnostics::CrashRecorder::arm(
        state_location.value().root / L"logs" / L"crashes", identity);
    // Crash diagnostics are deliberately best-effort: a blocked diagnostic
    // directory must not prevent safe-mode or recovery startup.
    static_cast<void>(crash_recorder);
    const std::wstring wide_identity(identity.begin(), identity.end());
    const std::uint64_t process_start_id = current_process_start_id();
    if (process_start_id == 0) {
        return show_fatal(
            {kf2::ErrorCode::platform_failure,
             L"Process start identity could not be determined", GetLastError()},
            12);
    }

    kf2::app::StartMode mode = kf2::app::StartMode::normal;
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view argument{argv[i]};
            if (argument == L"--read-only") mode = kf2::app::StartMode::read_only;
            else if (argument == L"--safe-mode") mode = kf2::app::StartMode::safe;
        }
        LocalFree(argv);
    }
    if (damaged_package) mode = kf2::app::StartMode::safe;
    auto discovery = kf2::game::default_game_discovery_input();
    if (discovery.has_value()) {
        const auto settings_path = state_location.value().root / L"settings.ini";
        std::ifstream input(settings_path, std::ios::binary);
        if (input) {
            const std::string bytes{std::istreambuf_iterator<char>{input},
                                    std::istreambuf_iterator<char>{}};
            const auto parsed = kf2::config::parse_settings(bytes);
            if (parsed.has_value() && !parsed.value().manual_game_path.empty()) {
                const int wide_size = MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS,
                    parsed.value().manual_game_path.data(),
                    static_cast<int>(parsed.value().manual_game_path.size()),
                    nullptr, 0);
                if (wide_size > 0) {
                    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        parsed.value().manual_game_path.data(),
                                        static_cast<int>(parsed.value().manual_game_path.size()),
                                        wide.data(), wide_size);
                    discovery.value().manual_candidates.insert(
                        discovery.value().manual_candidates.begin(),
                        std::filesystem::path{wide});
                }
            }
        }
    }
    auto application = kf2::app::Application::start({
        .state_root = state_location.value().root,
        .executable_root = executable_directory.value(),
        .instance_name = L"Local\\KF2OptimizerNext",
        .identity = {GetCurrentProcessId(), process_start_id},
        .create_window = true,
        .window_title = L"KF2 Optimizer Next - " + wide_identity,
        .game_discovery = discovery.has_value()
                              ? std::optional<kf2::game::GameDiscoveryInput>{discovery.value()}
                              : std::nullopt,
        .mode = mode,
        .startup_warning = std::move(package_warning),
    });
    if (!application.has_value()) {
        if (application.error().code == kf2::ErrorCode::already_running) {
            if (HWND existing = FindWindowW(L"KF2OptimizerNextMainWindow", nullptr)) {
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            return 0;
        }
        return show_fatal(application.error(), 13);
    }

    const auto run_result = application.value().run(show_command);
    if (!run_result.has_value()) {
        return show_fatal(run_result.error(), 14);
    }
    const auto shutdown = application.value().shutdown_cleanly();
    if (!shutdown.has_value()) {
        return show_fatal(shutdown.error(), 15);
    }
    return run_result.value();
}
