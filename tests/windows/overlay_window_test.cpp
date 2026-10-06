#include <Windows.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include "kf2/overlay/overlay_window.hpp"
#include "overlay_window_internal.hpp"

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

bool nearly_equal(float left, float right) {
    return std::fabs(left - right) < 0.0001F;
}

}  // namespace

namespace kf2::overlay {

struct OverlayWindowTestAccess {
    static void fail_next_draw_with_device_loss(OverlayWindow& overlay) {
        overlay.state_->test_end_draw_result = D2DERR_RECREATE_TARGET;
    }

    static bool guarded_resource_loading(OverlayWindow& overlay) {
        auto& state = *overlay.state_;
        if (!state.wic_factory || !state.mascot_bitmap ||
            !state.low_mascot_bitmap || state.test_bitmap_decode_attempts != 2) {
            return false;
        }
        state.test_bitmap_resource_failure = BitmapResourceFailure::missing;
        if (FAILED(detail::create_overlay_device_resources(state)) ||
            state.mascot_bitmap || state.low_mascot_bitmap ||
            state.test_bitmap_decode_attempts != 2) return false;
        std::size_t expected_attempts = 2;
        for (const auto failure : {BitmapResourceFailure::load,
                                   BitmapResourceFailure::lock,
                                   BitmapResourceFailure::empty}) {
            state.test_bitmap_resource_failure = failure;
            ++expected_attempts;  // Only the unaffected second PNG may decode.
            if (FAILED(detail::create_overlay_device_resources(state)) ||
                state.mascot_bitmap || !state.low_mascot_bitmap ||
                state.test_bitmap_decode_attempts != expected_attempts) {
                std::cerr << "Invalid bitmap resource reached decoder; attempts: "
                          << state.test_bitmap_decode_attempts << "; expected: "
                          << expected_attempts << '\n';
                return false;
            }
        }
        state.test_bitmap_resource_failure = BitmapResourceFailure::none;
        return SUCCEEDED(detail::create_overlay_device_resources(state)) &&
               state.mascot_bitmap && state.low_mascot_bitmap &&
               state.test_bitmap_decode_attempts == expected_attempts + 2;
    }
};

}  // namespace kf2::overlay

int main() {
    const auto module = GetModuleHandleW(nullptr);
    const auto png_resource = FindResourceW(module, MAKEINTRESOURCEW(202),
                                               RT_RCDATA);
    CHECK(png_resource != nullptr);
    CHECK(SizeofResource(module, png_resource) > 0);
    CHECK(FindResourceW(module, MAKEINTRESOURCEW(201), RT_RCDATA) == nullptr);
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    {
        auto resource_probe = kf2::overlay::OverlayWindow::create();
        CHECK(resource_probe.has_value());
        CHECK(kf2::overlay::OverlayWindowTestAccess::guarded_resource_loading(
            resource_probe.value()));
    }
    CoUninitialize();
    const kf2::overlay::MascotAnimationAsset defaults;
    bool malformed_valid = true;
    const auto malformed =
        kf2::overlay::detail::parse_mascot_animation_asset(
            "sample_rate_fps=inf\n"
            "idle_period_ms=0\n"
            "idle_body_amplitude=-1\n"
            "idle_hand_amplitude=0.75\n"
            "dock_transition_ms=-4\n"
            "dock_reach=1e999\n"
            "dock_impact_reach=2junk\n"
            "grip_micro_amplitude=nan\n"
            "leg_step_amplitude=-0.1\n"
            "variant_count=7.5\n"
            "blend_in_fast=1.5\n"
            "blend_out_soft=-0.1\n",
            &malformed_valid);
    CHECK(!malformed_valid);
    CHECK(nearly_equal(malformed.sample_rate_fps,
                       defaults.sample_rate_fps));
    CHECK(nearly_equal(malformed.idle_period_ms, defaults.idle_period_ms));
    CHECK(nearly_equal(malformed.idle_body_amplitude,
                       defaults.idle_body_amplitude));
    CHECK(nearly_equal(malformed.idle_hand_amplitude, 0.75F));
    CHECK(nearly_equal(malformed.dock_transition_ms,
                       defaults.dock_transition_ms));
    CHECK(nearly_equal(malformed.dock_reach, defaults.dock_reach));
    CHECK(nearly_equal(malformed.dock_impact_reach,
                       defaults.dock_impact_reach));
    CHECK(nearly_equal(malformed.grip_micro_amplitude,
                       defaults.grip_micro_amplitude));
    CHECK(nearly_equal(malformed.leg_step_amplitude,
                       defaults.leg_step_amplitude));
    CHECK(malformed.variant_count == defaults.variant_count);
    CHECK(nearly_equal(malformed.blend_in_fast, defaults.blend_in_fast));
    CHECK(nearly_equal(malformed.blend_out_soft, defaults.blend_out_soft));

    bool unbounded_valid = true;
    const auto unbounded =
        kf2::overlay::detail::parse_mascot_animation_asset(
            "sample_rate_fps=1000000\n"
            "idle_period_ms=1000000000\n"
            "idle_body_amplitude=1000000\n"
            "dock_reach=1000000\n"
            "variant_count=1000000\n",
            &unbounded_valid);
    CHECK(!unbounded_valid);
    CHECK(nearly_equal(unbounded.sample_rate_fps,
                       defaults.sample_rate_fps));
    CHECK(nearly_equal(unbounded.idle_period_ms, defaults.idle_period_ms));
    CHECK(nearly_equal(unbounded.idle_body_amplitude,
                       defaults.idle_body_amplitude));
    CHECK(nearly_equal(unbounded.dock_reach, defaults.dock_reach));
    CHECK(unbounded.variant_count == defaults.variant_count);

    std::ifstream asset_file(KF2_MASCOT_ANIMATION_ASSET_PATH,
                             std::ios::binary);
    CHECK(asset_file.good());
    const std::string asset_text{
        std::istreambuf_iterator<char>(asset_file),
        std::istreambuf_iterator<char>()};
    bool checked_in_valid = false;
    const auto checked_in =
        kf2::overlay::detail::parse_mascot_animation_asset(
            asset_text, &checked_in_valid);
    CHECK(checked_in_valid);
    CHECK(nearly_equal(checked_in.sample_rate_fps, 120.0F));
    CHECK(nearly_equal(checked_in.idle_period_ms, 5000.0F));
    CHECK(nearly_equal(checked_in.idle_body_amplitude, 0.32F));
    CHECK(nearly_equal(checked_in.idle_hand_amplitude, 0.65F));
    CHECK(nearly_equal(checked_in.dock_transition_ms, 900.0F));
    CHECK(nearly_equal(checked_in.dock_reach, 8.0F));
    CHECK(nearly_equal(checked_in.dock_impact_reach, 2.0F));
    CHECK(nearly_equal(checked_in.grip_micro_amplitude, 0.55F));
    CHECK(nearly_equal(checked_in.leg_step_amplitude, 0.45F));
    CHECK(checked_in.variant_count == 6);
    CHECK(nearly_equal(checked_in.blend_in_fast, 0.24F));
    CHECK(nearly_equal(checked_in.blend_out_soft, 0.075F));

    HWND target_window = create_target_window();
    CHECK(target_window != nullptr);
    ShowWindow(target_window, SW_SHOWNA);

    auto created = kf2::overlay::OverlayWindow::create();
    CHECK(created.has_value());
    auto overlay = std::move(created.value());
    const auto disabled_diagnostics = overlay.diagnostics();
    CHECK(!disabled_diagnostics.enabled);
    CHECK(disabled_diagnostics.update_calls == 0);
    kf2::overlay::OverlayPresentation disabled_probe;
    disabled_probe.target_window = target_window;
    CHECK(overlay.update(disabled_probe).has_value());
    CHECK(overlay.diagnostics().update_calls == 0);
    CHECK(overlay.render_count() == 0);
    CHECK(overlay.graph_geometry_build_count() == 0);
    CHECK(overlay.static_layer_build_count() == 0);
    overlay.set_diagnostics_enabled(true);
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
    CHECK(overlay.update(shown).has_value());
    const auto initial_diagnostics = overlay.diagnostics();
    CHECK(initial_diagnostics.enabled);
    CHECK(initial_diagnostics.update_calls == 1);
    CHECK(initial_diagnostics.redraws == 1);
    CHECK(initial_diagnostics.maximum_render_us >=
          initial_diagnostics.last_render_us);
    CHECK(IsWindowVisible(window));
    CHECK(overlay.static_layer_build_count() == 1);
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
    CHECK(overlay.diagnostics().skipped_redraws > 0);
    // Debug rendering can itself cross one cadence boundary. Allow enough
    // wall time for the next idle frame without depending on scheduler jitter.
    Sleep(100);
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() > settled_render_count);
    CHECK(overlay.static_layer_build_count() == 1);
    // Decorative mascot motion uses a lower idle cadence than live metric and
    // transition animation. Keep the full layered-window upload below 25 FPS
    // while the caller continues to tick at the normal application cadence.
    const auto idle_cadence_start = overlay.render_count();
    const ULONGLONG idle_cadence_started_ms = GetTickCount64();
    while (GetTickCount64() - idle_cadence_started_ms < 260) {
        Sleep(5);
        CHECK(overlay.update(shown).has_value());
    }
    CHECK(overlay.render_count() - idle_cadence_start <= 6);
    ShowWindow(window, SW_MINIMIZE);
    CHECK(IsIconic(window));
    CHECK(overlay.update(shown).has_value());
    CHECK(!IsIconic(window));
    CHECK(GetForegroundWindow() != window);
    shown.animations_enabled = false;
    shown.fps = 120.0;
    shown.frame_time_ms = 0.0;
    shown.bounds = {110, 130, 350, 220};
    CHECK(overlay.update(shown).has_value());
    CHECK(GetWindowRect(window, &bounds));
    CHECK(bounds.left == 110 && bounds.top == 130);
    // Sub-display-resolution telemetry changes must not force a full layered
    // window upload. The visible rounded values have not changed.
    const auto subpixel_metric_render_count = overlay.render_count();
    shown.fps += 0.1;
    shown.average_fps += 0.1;
    shown.one_percent_low_fps += 0.1;
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() == subpixel_metric_render_count);
    // The graph is sampled independently, but its Direct2D path must be built
    // only when the history or its vertical layout changes. Other overlay
    // renders reuse the same geometry instead of issuing every line again.
    shown.show_memory = true;
    shown.frame_time_ms = 16.7;
    CHECK(overlay.update(shown).has_value());
    const auto memory_layer_build_count = overlay.static_layer_build_count();
    CHECK(memory_layer_build_count == 2);
    Sleep(110);
    shown.frame_time_ms = 17.2;
    CHECK(overlay.update(shown).has_value());
    const auto graph_build_count = overlay.graph_geometry_build_count();
    CHECK(graph_build_count > 0);
    shown.frame_time_ms = 0.0;
    shown.bounds = {111, 130, 351, 220};
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.static_layer_build_count() == memory_layer_build_count);
    CHECK(overlay.graph_geometry_build_count() == graph_build_count);
    shown.show_memory = false;
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.graph_geometry_build_count() == graph_build_count + 1);
    shown.show_memory = true;
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

    // A Direct2D device-loss result must rebuild the device-dependent resource
    // graph and allow the next bounded update to render successfully.
    shown.fps += 1.0;
    kf2::overlay::OverlayWindowTestAccess::fail_next_draw_with_device_loss(
        overlay);
    const auto device_loss = overlay.update(shown);
    CHECK(device_loss.has_value());
    const auto recovered_render_count = overlay.render_count();
    const auto recovered = overlay.update(shown);
    CHECK(recovered.has_value());
    CHECK(recovered.value());
    CHECK(overlay.render_count() == recovered_render_count + 1);
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
    shown.fps += 1.0;
    CHECK(overlay.update(shown).has_value());
    CHECK(overlay.render_count() > idle_render_count);
    const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int frame = 0; frame < 1000; ++frame) {
        shown.fps = 60.0 + static_cast<double>(frame % 3);
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
    overlay.set_diagnostics_enabled(false);
    const auto cleared_diagnostics = overlay.diagnostics();
    CHECK(!cleared_diagnostics.enabled);
    CHECK(cleared_diagnostics.update_calls == 0);
    CHECK(cleared_diagnostics.redraws == 0);
    DestroyWindow(replacement_window);
    return EXIT_SUCCESS;
}
