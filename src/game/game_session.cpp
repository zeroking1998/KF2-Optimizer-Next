#include "kf2/game/game_session.hpp"

#include <dwmapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <optional>
#include <string_view>
#include <vector>

namespace kf2::game {
namespace {

#ifdef KF2_GAME_PROCESS_TESTING
std::atomic_uint32_t process_opens{0};
std::atomic_uint32_t process_creation_queries{0};
#endif

std::uint64_t file_time_value(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

std::wstring folded(std::filesystem::path path) {
    auto value = path.native();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return value;
}

bool is_nonblocking_overlay(HWND window) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_EX_LAYERED) == 0) return false;
    return (style & WS_EX_TRANSPARENT) != 0;
}

bool is_overlay_window(HWND window) {
    wchar_t class_name[64]{};
    if (GetClassNameW(window, class_name, 64) <= 0) return false;
    const std::wstring_view name{class_name};
    return name == L"KF2OptimizerNext-Overlay";
}

bool process_start_matches(const GameProcessIdentity& identity) {
    if (identity.native_process) {
        const HANDLE process = identity.native_process->get(identity);
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
#ifdef KF2_GAME_PROCESS_TESTING
    ++process_opens;
#endif
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, identity.pid);
    if (!process) return false;
#ifdef KF2_GAME_PROCESS_TESTING
    ++process_creation_queries;
#endif
    FILETIME creation{}, exit{}, kernel{}, user{};
    const bool matches =
        GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE &&
        file_time_value(creation) == identity.process_start_id &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return matches;
}

}  // namespace

GameProcessHandle::GameProcessHandle(
    HANDLE handle, std::uint32_t pid, std::uint64_t start,
    bool metrics_readable) noexcept
    : handle_{handle}, pid_{pid}, start_{start},
      metrics_readable_{metrics_readable} {}

GameProcessHandle::~GameProcessHandle() { CloseHandle(handle_); }

HANDLE GameProcessHandle::get(const GameProcessIdentity& identity) const noexcept {
    return identity.pid == pid_ && identity.process_start_id == start_
        ? handle_ : nullptr;
}

Result<GameProcessIdentity> bind_game_process(
    std::uint32_t pid, const std::filesystem::path& expected_executable) {
    if (pid == 0 || expected_executable.empty()) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::invalid_argument, L"Game process identity is incomplete", 0});
    }
#ifdef KF2_GAME_PROCESS_TESTING
    ++process_opens;
#endif
    // Metrics share the session handle when permitted. Restricted processes
    // still bind with the original read-only observation rights.
    constexpr DWORD observation_access =
        PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    HANDLE opened = OpenProcess(observation_access | PROCESS_VM_READ, FALSE, pid);
    const bool metrics_readable = opened != nullptr;
    if (!opened) {
#ifdef KF2_GAME_PROCESS_TESTING
        ++process_opens;
#endif
        opened = OpenProcess(observation_access, FALSE, pid);
    }
    std::unique_ptr<void, decltype(&CloseHandle)> owned{opened, &CloseHandle};
    const HANDLE process = owned.get();
    if (!process) return Result<GameProcessIdentity>::failure(
        {ErrorCode::access_denied, L"Game process cannot be inspected", GetLastError()});
    const DWORD wait = WaitForSingleObject(process, 0);
    if (wait != WAIT_TIMEOUT) {
        const DWORD error = wait == WAIT_FAILED ? GetLastError() : 0;
        return Result<GameProcessIdentity>::failure(
            {wait == WAIT_OBJECT_0 ? ErrorCode::stale_data
                                  : ErrorCode::platform_failure,
             L"Game process is not running", error});
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    DWORD length = 32768;
    std::vector<wchar_t> path(length);
#ifdef KF2_GAME_PROCESS_TESTING
    ++process_creation_queries;
#endif
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
        const auto error = GetLastError();
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::platform_failure, L"Game process time query failed", error});
    }
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        const auto error = GetLastError();
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::platform_failure, L"Game process path query failed", error});
    }
    if (length == 0 || length > path.size()) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::platform_failure, L"Game process path is unavailable", 0});
    }
    std::error_code canonical_error;
    auto actual = std::filesystem::weakly_canonical(
        std::filesystem::path{std::wstring{path.data(), length}}, canonical_error);
    if (canonical_error) return Result<GameProcessIdentity>::failure(
        {ErrorCode::platform_failure, L"Game process path cannot be verified",
         static_cast<std::uint32_t>(canonical_error.value())});
    const auto expected = std::filesystem::weakly_canonical(
        expected_executable, canonical_error);
    if (canonical_error) return Result<GameProcessIdentity>::failure(
        {ErrorCode::platform_failure, L"Expected game path cannot be verified",
         static_cast<std::uint32_t>(canonical_error.value())});
    if (folded(actual) != folded(expected)) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::stale_data, L"Game executable identity does not match", 0});
    }
    const auto start = file_time_value(creation);
    auto native_process = std::shared_ptr<const GameProcessHandle>{
        new GameProcessHandle{owned.release(), pid, start, metrics_readable}};
    return Result<GameProcessIdentity>::success(
        {pid, start, std::move(actual), std::move(native_process)});
}

bool is_game_process_current(const GameProcessIdentity& process) noexcept {
    return process.pid != 0 && process.process_start_id != 0 &&
           process_start_matches(process);
}

#ifdef KF2_GAME_PROCESS_TESTING
detail::ProcessQueryCounts detail::process_query_counts_for_testing() noexcept {
    return {process_opens.load(), process_creation_queries.load()};
}
#endif

Result<GameWindowState> inspect_game_window(
    const GameProcessIdentity& process, HWND window) {
    if (!IsWindow(window)) return Result<GameWindowState>::failure(
        {ErrorCode::not_found, L"Game window no longer exists", 0});
    // The executable path was verified when the session was bound. During the
    // 120 ms hot path, a nonblocking liveness check and immutable creation
    // time reject exits and PID reuse without querying and canonicalizing
    // the EXE path again on every frame sample.
    if (!is_game_process_current(process)) {
        return Result<GameWindowState>::failure(
            {ErrorCode::stale_data, L"Game process was restarted", 0});
    }
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != process.pid || GetAncestor(window, GA_ROOT) != window) {
        return Result<GameWindowState>::failure(
            {ErrorCode::stale_data, L"Window is not owned by the bound game process", 0});
    }
    GameWindowState state;
    state.window = window;
    state.process = process;
    state.visible = IsWindowVisible(window) != FALSE;
    state.minimized = IsIconic(window) != FALSE;
    DWORD cloaked = 0;
    state.cloaked = SUCCEEDED(DwmGetWindowAttribute(
        window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
    const HWND foreground = GetForegroundWindow();
    state.foreground = foreground && GetAncestor(foreground, GA_ROOT) == window;
    RECT client{};
    if (GetClientRect(window, &client)) {
        POINT points[2]{{client.left, client.top}, {client.right, client.bottom}};
        SetLastError(ERROR_SUCCESS);
        const int mapped = MapWindowPoints(window, nullptr, points, 2);
        if (mapped != 0 || GetLastError() == ERROR_SUCCESS) {
            state.client_bounds = {points[0].x, points[0].y,
                                   points[1].x, points[1].y};
        }
    }
    MONITORINFO monitor_info{sizeof(monitor_info)};
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        state.monitor_work_bounds = monitor_info.rcWork;
    }
    if (!state.visible) state.reason = WindowUnavailableReason::hidden;
    else if (state.minimized) state.reason = WindowUnavailableReason::minimized;
    else if (state.cloaked) state.reason = WindowUnavailableReason::cloaked;
    else if (state.client_bounds.right <= state.client_bounds.left ||
             state.client_bounds.bottom <= state.client_bounds.top) {
        state.reason = WindowUnavailableReason::invalid_geometry;
    } else if (!state.foreground) state.reason = WindowUnavailableReason::not_foreground;
    else state.reason = WindowUnavailableReason::none;
    return Result<GameWindowState>::success(std::move(state));
}

bool is_game_area_covered(const GameWindowState& state, const RECT& area) {
    if (!state.window || area.right <= area.left || area.bottom <= area.top) {
        return true;
    }
    for (HWND candidate = GetWindow(state.window, GW_HWNDPREV); candidate;
         candidate = GetWindow(candidate, GW_HWNDPREV)) {
        if (is_overlay_window(candidate) || is_nonblocking_overlay(candidate) ||
            !IsWindowVisible(candidate) || IsIconic(candidate)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(candidate, DWMWA_CLOAKED,
                                            &cloaked, sizeof(cloaked))) &&
            cloaked != 0) continue;
        RECT rectangle{}, overlap{};
        if (GetWindowRect(candidate, &rectangle) &&
            IntersectRect(&overlap, &area, &rectangle)) return true;
    }
    return false;
}

Result<GameProcessIdentity> find_running_game_process(
    const std::filesystem::path& expected_executable) {
    const auto expected_name = expected_executable.filename().native();
    if (expected_name.empty()) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::invalid_argument,
             L"Game executable name is unavailable", 0});
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return Result<GameProcessIdentity>::failure(
        {ErrorCode::platform_failure, L"Process list cannot be inspected", GetLastError()});
    PROCESSENTRY32W entry{sizeof(entry)};
    std::optional<Error> inspection_error;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            // The snapshot already provides the executable name. Avoid
            // opening, querying and canonicalizing every unrelated Windows
            // process; a matching name still receives the complete path and
            // immutable creation-time verification in bind_game_process().
            if (CompareStringOrdinal(entry.szExeFile, -1,
                                     expected_name.c_str(), -1, TRUE) !=
                CSTR_EQUAL) {
                continue;
            }
            auto candidate = bind_game_process(entry.th32ProcessID, expected_executable);
            if (candidate.has_value()) {
                CloseHandle(snapshot);
                return candidate;
            }
            // Only a verified exit or different executable proves that this
            // exact-name candidate is irrelevant. Query failures are unsafe.
            if (candidate.error().code != ErrorCode::stale_data &&
                (!inspection_error ||
                 candidate.error().code == ErrorCode::access_denied)) {
                inspection_error = std::move(candidate.error());
            }
        } while (Process32NextW(snapshot, &entry));
    }
    const auto enumeration_error = GetLastError();
    CloseHandle(snapshot);
    if (inspection_error) {
        return Result<GameProcessIdentity>::failure(std::move(*inspection_error));
    }
    if (enumeration_error != ERROR_NO_MORE_FILES) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::platform_failure, L"Process list traversal failed",
             enumeration_error});
    }
    return Result<GameProcessIdentity>::failure(
        {ErrorCode::not_found, L"KF2 process is not running", 0});
}

bool game_process_may_be_running(
    const std::filesystem::path& expected_executable) {
    const auto process = find_running_game_process(expected_executable);
    return process.has_value() || process.error().code != ErrorCode::not_found;
}

Result<HWND> find_game_window(const GameProcessIdentity& process) {
    struct Search { DWORD pid; HWND best; LONG area; } search{process.pid, nullptr, 0};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
        if (owner != search.pid || GetAncestor(window, GA_ROOT) != window ||
            !IsWindowVisible(window)) return TRUE;
        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;
        const LONG area = (client.right - client.left) * (client.bottom - client.top);
        if (area > search.area) { search.area = area; search.best = window; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return Result<HWND>::failure(
        {ErrorCode::not_found, L"Visible KF2 window was not found", 0});
    return Result<HWND>::success(search.best);
}

}  // namespace kf2::game
