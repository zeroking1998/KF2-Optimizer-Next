#include <Windows.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "kf2/game/game_session.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

LRESULT CALLBACK test_window_proc(HWND window, UINT message,
                                  WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2) {
        const auto process = kf2::game::find_running_game_process(argv[1]);
        if (!process.has_value()) {
            std::wcerr << L"process_error=" << process.error().message
                       << L" native=" << process.error().native_code << L'\n';
            return 20;
        }
        const auto window = kf2::game::find_game_window(process.value());
        if (!window.has_value()) {
            std::wcerr << L"window_error=" << window.error().message
                       << L" native=" << window.error().native_code << L'\n';
            return 21;
        }
        const auto state = kf2::game::inspect_game_window(process.value(),
                                                          window.value());
        if (!state.has_value()) {
            std::wcerr << L"inspect_error=" << state.error().message
                       << L" native=" << state.error().native_code << L'\n';
            return 22;
        }
        std::wcout << L"pid=" << process.value().pid
                   << L" window=" << reinterpret_cast<std::uintptr_t>(window.value())
                   << L" visible=" << state.value().visible
                   << L" foreground=" << state.value().foreground << L'\n';
        return EXIT_SUCCESS;
    }
    wchar_t executable[MAX_PATH]{};
    CHECK(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0);
    const auto bound = kf2::game::bind_game_process(
        GetCurrentProcessId(), std::filesystem::path{executable});
    CHECK(bound.has_value());
    CHECK(bound.value().pid == GetCurrentProcessId());
    CHECK(bound.value().process_start_id != 0);

    const auto wrong = kf2::game::bind_game_process(
        GetCurrentProcessId(), std::filesystem::path{executable}.parent_path() /
                                   L"KFGame.exe");
    CHECK(!wrong.has_value());
    CHECK(wrong.error().code == kf2::ErrorCode::stale_data);

    const wchar_t* class_name = L"KF2OptimizerNext-GameSessionTest";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = test_window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    CHECK(RegisterClassW(&window_class) != 0);
    HWND window = CreateWindowExW(0, class_name, L"fixture", WS_OVERLAPPEDWINDOW,
                                  50, 50, 640, 480, nullptr, nullptr,
                                  window_class.hInstance, nullptr);
    CHECK(window != nullptr);
    const auto found_process = kf2::game::find_running_game_process(executable);
    CHECK(found_process.has_value());

    auto hidden = kf2::game::inspect_game_window(bound.value(), window);
    CHECK(hidden.has_value());
    CHECK(!hidden.value().visible);
    CHECK(hidden.value().reason == kf2::game::WindowUnavailableReason::hidden);

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    const auto found_window = kf2::game::find_game_window(bound.value());
    CHECK(found_window.has_value());
    CHECK(found_window.value() == window);
    auto visible = kf2::game::inspect_game_window(bound.value(), window);
    CHECK(visible.has_value());
    CHECK(visible.value().visible);
    CHECK(!visible.value().minimized);
    CHECK(visible.value().client_bounds.right > visible.value().client_bounds.left);

    const RECT game_bounds = visible.value().client_bounds;
    HWND cover_window = CreateWindowExW(
        0, class_name, L"cover fixture", WS_POPUP,
        game_bounds.left, game_bounds.top,
        game_bounds.right - game_bounds.left,
        game_bounds.bottom - game_bounds.top,
        nullptr, nullptr, window_class.hInstance, nullptr);
    CHECK(cover_window != nullptr);
    ShowWindow(cover_window, SW_SHOW);
    SetWindowPos(cover_window, HWND_TOP, game_bounds.left, game_bounds.top,
                 game_bounds.right - game_bounds.left,
                 game_bounds.bottom - game_bounds.top, SWP_SHOWWINDOW);
    CHECK(kf2::game::is_game_area_covered(visible.value(), game_bounds));
    auto covered = kf2::game::inspect_game_window(bound.value(), window);
    CHECK(covered.has_value());
    CHECK(covered.value().fully_occluded);
    DestroyWindow(cover_window);

    SetWindowPos(window, HWND_TOPMOST, game_bounds.left, game_bounds.top,
                 game_bounds.right - game_bounds.left,
                 game_bounds.bottom - game_bounds.top,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    HWND overlay_cover = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        class_name, L"overlay fixture", WS_POPUP,
        game_bounds.left, game_bounds.top,
        game_bounds.right - game_bounds.left,
        game_bounds.bottom - game_bounds.top,
        nullptr, nullptr, window_class.hInstance, nullptr);
    CHECK(overlay_cover != nullptr);
    SetLayeredWindowAttributes(overlay_cover, 0, 180, LWA_ALPHA);
    ShowWindow(overlay_cover, SW_SHOWNA);
    SetWindowPos(overlay_cover, HWND_TOPMOST, game_bounds.left, game_bounds.top,
                 game_bounds.right - game_bounds.left,
                 game_bounds.bottom - game_bounds.top,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    CHECK(!kf2::game::is_game_area_covered(visible.value(), game_bounds));
    auto overlay_visible = kf2::game::inspect_game_window(bound.value(), window);
    CHECK(overlay_visible.has_value());
    CHECK(!overlay_visible.value().fully_occluded);
    DestroyWindow(overlay_cover);

    HWND origin_window = CreateWindowExW(0, class_name, L"origin fixture", WS_POPUP,
                                         0, 0, 640, 480, nullptr, nullptr,
                                         window_class.hInstance, nullptr);
    CHECK(origin_window != nullptr);
    ShowWindow(origin_window, SW_SHOWNA);
    auto origin = kf2::game::inspect_game_window(bound.value(), origin_window);
    CHECK(origin.has_value());
    CHECK(origin.value().client_bounds.left == 0);
    CHECK(origin.value().client_bounds.top == 0);
    CHECK(origin.value().client_bounds.right == 640);
    CHECK(origin.value().client_bounds.bottom == 480);
    DestroyWindow(origin_window);

    ShowWindow(window, SW_MINIMIZE);
    auto minimized = kf2::game::inspect_game_window(bound.value(), window);
    CHECK(minimized.has_value());
    CHECK(minimized.value().minimized);
    CHECK(minimized.value().reason == kf2::game::WindowUnavailableReason::minimized);

    auto stale_identity = bound.value();
    ++stale_identity.process_start_id;
    CHECK(!kf2::game::inspect_game_window(stale_identity, window).has_value());
    CHECK(!kf2::game::inspect_game_window(bound.value(), GetDesktopWindow()).has_value());

    DestroyWindow(window);
    UnregisterClassW(class_name, window_class.hInstance);
    return EXIT_SUCCESS;
}
