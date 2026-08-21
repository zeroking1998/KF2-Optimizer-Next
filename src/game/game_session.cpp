#include "kf2/game/game_session.hpp"

#include <dwmapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <vector>

namespace kf2::game {
namespace {

std::uint64_t file_time_value(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

std::wstring folded(std::filesystem::path path) {
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error) return {};
    auto value = path.native();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return value;
}

bool is_nonblocking_overlay(HWND window) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_EX_LAYERED) == 0) return false;
    return (style & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) != 0;
}

bool is_fully_occluded(HWND window, const RECT& bounds) {
    HRGN visible = CreateRectRgn(bounds.left, bounds.top,
                                 bounds.right, bounds.bottom);
    if (!visible) return false;
    for (HWND candidate = GetWindow(window, GW_HWNDPREV); candidate;
         candidate = GetWindow(candidate, GW_HWNDPREV)) {
        if (!IsWindowVisible(candidate) || IsIconic(candidate) ||
            is_nonblocking_overlay(candidate)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(candidate, DWMWA_CLOAKED,
                                            &cloaked, sizeof(cloaked))) &&
            cloaked != 0) continue;
        RECT rectangle{};
        if (!GetWindowRect(candidate, &rectangle)) continue;
        RECT overlap{};
        if (!IntersectRect(&overlap, &bounds, &rectangle)) continue;
        HRGN covered = CreateRectRgn(rectangle.left, rectangle.top,
                                     rectangle.right, rectangle.bottom);
        if (!covered) continue;
        const int remaining = CombineRgn(visible, visible, covered, RGN_DIFF);
        DeleteObject(covered);
        if (remaining == NULLREGION) {
            DeleteObject(visible);
            return true;
        }
    }
    DeleteObject(visible);
    return false;
}

bool is_overlay_window(HWND window) {
    wchar_t class_name[64]{};
    if (GetClassNameW(window, class_name, 64) <= 0) return false;
    const std::wstring_view name{class_name};
    return name == L"KF2OptimizerNext-Overlay";
}

}  // namespace

Result<GameProcessIdentity> bind_game_process(
    std::uint32_t pid, const std::filesystem::path& expected_executable) {
    if (pid == 0 || expected_executable.empty()) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::invalid_argument, L"Game process identity is incomplete", 0});
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, pid);
    if (!process) return Result<GameProcessIdentity>::failure(
        {ErrorCode::access_denied, L"Game process cannot be inspected", GetLastError()});
    FILETIME creation{}, exit{}, kernel{}, user{};
    DWORD length = 32768;
    std::vector<wchar_t> path(length);
    const bool times_ok = GetProcessTimes(process, &creation, &exit, &kernel, &user);
    const bool path_ok = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!times_ok || !path_ok) return Result<GameProcessIdentity>::failure(
        {ErrorCode::platform_failure, L"Game process identity query failed",
         GetLastError()});
    path.resize(length);
    const std::filesystem::path actual{path.data()};
    if (folded(actual).empty() || folded(actual) != folded(expected_executable)) {
        return Result<GameProcessIdentity>::failure(
            {ErrorCode::stale_data, L"Game executable identity does not match", 0});
    }
    return Result<GameProcessIdentity>::success(
        {pid, file_time_value(creation), std::filesystem::weakly_canonical(actual)});
}

Result<GameWindowState> inspect_game_window(
    const GameProcessIdentity& process, HWND window) {
    if (!IsWindow(window)) return Result<GameWindowState>::failure(
        {ErrorCode::not_found, L"Game window no longer exists", 0});
    auto current = bind_game_process(process.pid, process.executable);
    if (!current.has_value() ||
        current.value().process_start_id != process.process_start_id) {
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
    if (state.visible && !state.minimized && !state.cloaked &&
        state.client_bounds.right > state.client_bounds.left &&
        state.client_bounds.bottom > state.client_bounds.top) {
        state.fully_occluded = is_fully_occluded(window, state.client_bounds);
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
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return Result<GameProcessIdentity>::failure(
        {ErrorCode::platform_failure, L"Process list cannot be inspected", GetLastError()});
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            auto candidate = bind_game_process(entry.th32ProcessID, expected_executable);
            if (candidate.has_value()) {
                CloseHandle(snapshot);
                return candidate;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return Result<GameProcessIdentity>::failure(
        {ErrorCode::not_found, L"KF2 process is not running", 0});
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
