#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include "kf2/overlay/overlay_window.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

LRESULT CALLBACK target_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND create_target_window(const wchar_t* title = L"KF2 target fixture") {
    WNDCLASSW type{};
    type.lpfnWndProc = target_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"KF2OptimizerOverlayTargetTest";
    if (!RegisterClassW(&type) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    return CreateWindowExW(
        WS_EX_TOPMOST, type.lpszClassName, title,
        WS_OVERLAPPEDWINDOW, 40, 40, 640, 480, nullptr, nullptr,
        type.hInstance, nullptr);
}

}  // namespace

int main() {
    HWND target_window = create_target_window();
    CHECK(target_window != nullptr);
    ShowWindow(target_window, SW_SHOWNA);

    auto created = kf2::overlay::OverlayWindow::create();
    CHECK(created.has_value());
    auto overlay = std::move(created.value());
    HWND window = overlay.native_handle();
    CHECK(window != nullptr);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    CHECK((style & WS_EX_TOOLWINDOW) != 0);
    CHECK((style & WS_EX_NOACTIVATE) != 0);
    CHECK((style & WS_EX_TRANSPARENT) != 0);
    CHECK((style & WS_EX_LAYERED) != 0);
    CHECK((GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0);

    kf2::overlay::OverlayPresentation shown;
    shown.visible = true; shown.reason = kf2::overlay::OverlayHideReason::none;
    shown.target_window = target_window;
    shown.bounds = {100, 120, 340, 210};
    shown.text = L"60.0 FPS\n16.7 ms";
    CHECK(overlay.update(shown).has_value());
    CHECK(IsWindowVisible(window));
    CHECK(GetWindow(window, GW_OWNER) == target_window);
    CHECK(GetForegroundWindow() != window);
    for (int frame = 0; frame < 52; ++frame) {
        Sleep(16);
        CHECK(overlay.update(shown).has_value());
    }
    CHECK(overlay.render_count() > 1);
    RECT bounds{}; CHECK(GetWindowRect(window, &bounds));
    CHECK(bounds.left == 100 && bounds.top == 120);
    const auto settled_render_count = overlay.render_count();
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() == settled_render_count);
    // Debug rendering can itself cross one cadence boundary. Allow enough
    // wall time for the next idle frame without depending on scheduler jitter.
    Sleep(100);
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() > settled_render_count);
    ShowWindow(window, SW_MINIMIZE);
    CHECK(IsIconic(window));
    CHECK(overlay.update(shown).has_value());
    CHECK(!IsIconic(window));
    CHECK(GetForegroundWindow() != window);
    shown.animations_enabled = false;
    shown.fps = 120.0;
    shown.bounds = {110, 130, 350, 220};
    CHECK(overlay.update(shown).has_value());
    CHECK(GetWindowRect(window, &bounds));
    CHECK(bounds.left == 110 && bounds.top == 130);
    shown.animations_enabled = true;

    // A layered tool window must recover if Windows or another desktop helper
    // destroys the native surface while the owning overlay object remains alive.
    CHECK(DestroyWindow(window));
    CHECK(!IsWindow(window));
    CHECK(overlay.update(shown).has_value());
    window = overlay.native_handle();
    CHECK(IsWindow(window));
    const LONG_PTR recovered_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    CHECK((recovered_style & WS_EX_TOOLWINDOW) != 0);
    CHECK((recovered_style & WS_EX_NOACTIVATE) != 0);
    CHECK((recovered_style & WS_EX_TRANSPARENT) != 0);
    CHECK((recovered_style & WS_EX_LAYERED) != 0);
    CHECK(GetWindow(window, GW_OWNER) == target_window);

    // Replacing or closing a KF2 window can also destroy its owned overlay.
    // An unchanged presentation must recreate, rebind and show the native
    // surface immediately instead of waiting for a later metric animation.
    shown.animations_enabled = false;
    shown.fps = 121.0;
    CHECK(overlay.update(shown).has_value());
    HWND replacement_window = create_target_window(L"Replacement KF2 target");
    CHECK(replacement_window != nullptr);
    ShowWindow(replacement_window, SW_SHOWNA);
    CHECK(DestroyWindow(target_window));
    shown.target_window = replacement_window;
    const auto rebound = overlay.update(shown);
    CHECK(rebound.has_value());
    CHECK(rebound.value());
    window = overlay.native_handle();
    CHECK(IsWindow(window));
    CHECK(IsWindowVisible(window));
    CHECK(GetWindow(window, GW_OWNER) == replacement_window);
    CHECK(SetWindowPos(replacement_window, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                           SWP_SHOWWINDOW));
    CHECK(overlay.update(shown).has_value());
    CHECK(IsWindowVisible(window));

    // Exercise the exact resize/relocate path used by scaling and automatic
    // corner selection, including negative desktop coordinates.
    shown.animations_enabled = false;
    for (int frame = 0; frame < 600; ++frame) {
        const LONG width = 180 + (frame % 151);
        const LONG height = 70 + (frame % 47);
        const LONG left = (frame % 2 == 0) ? -320 + (frame % 53)
                                            : 1200 + (frame % 71);
        const LONG top = 40 + (frame % 109);
        shown.bounds = {left, top, left + width, top + height};
        shown.fps = 45.0 + static_cast<double>(frame % 180);
        shown.average_fps = 90.0 + static_cast<double>(frame % 70);
        shown.one_percent_low_fps = 35.0 + static_cast<double>(frame % 90);
        shown.frame_time_ms = 1000.0 / shown.fps;
        CHECK(overlay.update(shown).has_value());
    }
    shown.animations_enabled = true;
    for (int transition = 0; transition < 40; ++transition) {
        const LONG left = transition % 2 == 0 ? 20 : 1500;
        const LONG top = transition % 3 == 0 ? 20 : 760;
        shown.bounds = {left, top, left + 330, top + 105};
        for (int frame = 0; frame < 8; ++frame) {
            Sleep(2);
            CHECK(overlay.update(shown).has_value());
        }
    }

    const auto idle_render_count = overlay.render_count();
    shown.text = L"61.0 FPS\n16.4 ms";
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() > idle_render_count);
    const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int frame = 0; frame < 1000; ++frame) {
        shown.text = std::to_wstring(60 + (frame % 3)) + L".0 FPS\n16.7 ms";
        CHECK(overlay.update(shown).has_value());
    }
    CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before + 2);
    CHECK(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= user_before + 2);
    shown.visible = false;
    CHECK(overlay.update(shown).has_value());
    CHECK(IsWindowVisible(window));
    for (int frame = 0; frame < 52; ++frame) {
        Sleep(16);
        CHECK(overlay.update(shown).has_value());
    }
    CHECK(!IsWindowVisible(window));
    DestroyWindow(replacement_window);
    return EXIT_SUCCESS;
}
