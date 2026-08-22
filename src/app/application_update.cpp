#include "application_runtime.hpp"

#include <Windows.h>

#include <chrono>
#include <ctime>
#include <cwchar>
#include <mutex>
#include <optional>
#include <thread>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/update/github_release_client.hpp"
#include "kf2/update/update_helper.hpp"
#include "kf2/update/update_package.hpp"
#include "kf2/update/update_state.hpp"

namespace kf2::app {
namespace {

std::wstring widen_utf8(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return L"Unavailable";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::int64_t unix_now() {
    return static_cast<std::int64_t>(std::chrono::duration_cast<
        std::chrono::seconds>(std::chrono::system_clock::now()
                                 .time_since_epoch()).count());
}

std::wstring format_last_check(std::int64_t value) {
    if (value <= 0) return L"Never";
    const std::time_t time = static_cast<std::time_t>(value);
    std::tm local{};
    if (localtime_s(&local, &time) != 0) return L"Unavailable";
    wchar_t buffer[64]{};
    if (std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M", &local) ==
        0) return L"Unavailable";
    return buffer;
}

std::wstring format_size(std::uint64_t bytes) {
    const auto tenths = (bytes * 10ULL + 512ULL * 1024ULL) /
        (1024ULL * 1024ULL);
    return std::to_wstring(tenths / 10ULL) + L"." +
        std::to_wstring(tenths % 10ULL) + L" MiB";
}

update::PersistedUpdateState persisted_state(
    const update::UpdateSnapshot& snapshot) {
    update::PersistedUpdateState state{
        .last_check_unix_seconds = snapshot.last_check_unix_seconds,
        .ignored_version = snapshot.ignored_version};
    if (!snapshot.cached_check_completed) return state;
    if (snapshot.cached_available_version) {
        state.last_result = update::PersistedCheckResult::available;
        state.available_version = *snapshot.cached_available_version;
    } else {
        state.last_result = update::PersistedCheckResult::current;
    }
    return state;
}

}  // namespace

struct UpdateCheckAsyncState {
    std::mutex mutex;
    std::optional<Result<std::optional<update::ReleaseInfo>>> outcome;
};

struct UpdateInstallAsyncState {
    std::mutex mutex;
    std::optional<Result<update::PreparedUpdatePackage>> outcome;
};

void UiRuntime::refresh_update_presentation() {
    auto status = model.status();
    const auto& snapshot = update_controller.snapshot();
    status.update_installed_version = widen_utf8(snapshot.installed_version);
    status.update_available_version = snapshot.available_release
        ? widen_utf8(snapshot.available_release->version)
        : snapshot.cached_available_version
            ? widen_utf8(*snapshot.cached_available_version) : L"None";
    status.update_last_check = format_last_check(
        snapshot.last_check_unix_seconds);
    status.update_status = snapshot.status;
    status.automatic_update_checks = snapshot.automatic_checks_enabled;
    status.update_checking = snapshot.phase == update::UpdatePhase::checking;
    status.update_installing = snapshot.phase == update::UpdatePhase::installing;
    status.update_available = snapshot.available_release.has_value();
    status.update_newer_version_known =
        snapshot.available_release.has_value() ||
        snapshot.cached_available_version.has_value();
    status.update_prompt_visible = status.update_newer_version_known &&
        !snapshot.dismissed;
    status.update_check_completed = snapshot.cached_check_completed;
    status.update_installable = status.update_available &&
        snapshot.available_release->asset.has_value() &&
        snapshot.available_release->install_block_reason.empty();
    if (snapshot.available_release) {
        status.update_published_at =
            widen_utf8(snapshot.available_release->published_at);
        status.update_changelog =
            widen_utf8(snapshot.available_release->changelog);
        status.update_download_size = snapshot.available_release->asset
            ? format_size(snapshot.available_release->asset->size_bytes)
            : L"Unavailable";
    } else {
        status.update_published_at.clear();
        status.update_changelog.clear();
        status.update_download_size.clear();
    }
    model.set_status(std::move(status));
    invalidate();
}

void UiRuntime::start_update_check(update::CheckTrigger trigger) {
    const auto started = update_controller.begin_check(trigger, unix_now());
    if (started != update::CheckStart::started) {
        refresh_update_presentation();
        return;
    }
    static_cast<void>(update::save_update_state(
        update_state_path, persisted_state(update_controller.snapshot())));
    const auto state = std::make_shared<UpdateCheckAsyncState>();
    update_check_state = state;
    const std::string installed = update_controller.snapshot().installed_version;
    std::thread([state, installed] {
        auto result = update::query_official_github_releases(installed);
        std::scoped_lock lock{state->mutex};
        state->outcome.emplace(std::move(result));
    }).detach();
    refresh_update_presentation();
}

void UiRuntime::poll_update_check() {
    if (!update_check_state) return;
    std::optional<Result<std::optional<update::ReleaseInfo>>> outcome;
    {
        std::scoped_lock lock{update_check_state->mutex};
        if (!update_check_state->outcome) return;
        outcome.emplace(std::move(*update_check_state->outcome));
    }
    update_check_state.reset();
    update_controller.complete_check(std::move(*outcome));
    static_cast<void>(update::save_update_state(
        update_state_path, persisted_state(update_controller.snapshot())));
    refresh_update_presentation();
}

void UiRuntime::toggle_automatic_update_checks() {
    if (start_mode != StartMode::normal) {
        model.set_notice({ui::NoticeSeverity::warning, L"MODE_READ_ONLY",
                          L"Restart normally to change update settings.", L""});
        invalidate();
        return;
    }
    const bool previous = optimizer_settings.automatic_update_checks;
    optimizer_settings.automatic_update_checks = !previous;
    const auto saved = platform::windows::atomic_replace_utf8(
        settings_path, config::serialize_settings(optimizer_settings));
    if (!saved.has_value()) {
        optimizer_settings.automatic_update_checks = previous;
        model.set_notice({ui::NoticeSeverity::error,
                          L"UPDATE_SETTING_SAVE_FAILED",
                          saved.error().message, L""});
    } else {
        update_controller.set_automatic_checks_enabled(
            optimizer_settings.automatic_update_checks);
    }
    refresh_update_presentation();
}

void UiRuntime::dismiss_update() {
    update_controller.dismiss();
    refresh_update_presentation();
}

void UiRuntime::ignore_update() {
    update_controller.ignore_available_version();
    static_cast<void>(update::save_update_state(
        update_state_path, persisted_state(update_controller.snapshot())));
    refresh_update_presentation();
}

void UiRuntime::start_update_install() {
    if (!update_controller.begin_install_with_user_consent()) {
        refresh_update_presentation();
        return;
    }
    wchar_t temporary[MAX_PATH + 1]{};
    const DWORD count = GetTempPathW(MAX_PATH, temporary);
    if (count == 0 || count > MAX_PATH) {
        update_controller.complete_install_failure(
            L"The temporary update folder is unavailable.");
        refresh_update_presentation();
        return;
    }
    const auto release = *update_controller.snapshot().available_release;
    const auto work = std::filesystem::path{temporary} /
        L"KF2OptimizerNext-Update" /
        (std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(static_cast<unsigned long long>(monotonic_ns())));
    const auto state = std::make_shared<UpdateInstallAsyncState>();
    update_install_state = state;
    std::thread([state, release, work] {
        auto result = update::prepare_update_package(release, work);
        std::scoped_lock lock{state->mutex};
        state->outcome.emplace(std::move(result));
    }).detach();
    refresh_update_presentation();
}

void UiRuntime::poll_update_install() {
    if (!update_install_state) return;
    std::optional<Result<update::PreparedUpdatePackage>> outcome;
    {
        std::scoped_lock lock{update_install_state->mutex};
        if (!update_install_state->outcome) return;
        outcome.emplace(std::move(*update_install_state->outcome));
    }
    update_install_state.reset();
    if (!outcome->has_value()) {
        update_controller.complete_install_failure(outcome->error().message);
    } else {
        const auto release = update_controller.snapshot().available_release;
        const auto launched = release && window
            ? update::launch_update_helper(
                  outcome->value(), executable_root, release->version,
                  GetCurrentProcessId())
            : Result<bool>::failure(
                  {ErrorCode::internal_failure,
                   L"The update restart window is unavailable", 0});
        if (launched.has_value()) {
            PostMessageW(static_cast<HWND>(window->native_handle_for_testing()),
                         WM_CLOSE, 0, 0);
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(outcome->value().work_root, ignored);
        update_controller.complete_install_failure(launched.error().message);
    }
    refresh_update_presentation();
}

}  // namespace kf2::app
