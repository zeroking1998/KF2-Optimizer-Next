#include <cstdlib>
#include <iostream>
#include "kf2/overlay/overlay_policy.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2;
    game::GameWindowState window;
    window.visible = true; window.foreground = true;
    window.client_bounds = {100, 200, 1380, 920};
    window.reason = game::WindowUnavailableReason::none;
    telemetry::FrameMetrics frames;
    frames.fps = 60.25; frames.frame_time_ms = 16.60;
    frames.average_fps = 58.75; frames.one_percent_low_fps = 49.25;
    frames.quality = telemetry::SampleQuality::good;
    frames.reason = telemetry::UnavailableReason::none;

    overlay::OverlayPolicyInput input{true, window, frames,
        overlay::OverlayCorner::top_right, 1.0F, {240, 90}, 12};
    auto shown = overlay::evaluate_overlay(input);
    CHECK(shown.visible);
    CHECK(shown.bounds.right == 1368);
    CHECK(shown.bounds.top == 212);
    CHECK(shown.text.find(L"FPS  60.3") != std::wstring::npos);
    CHECK(shown.text.find(L"AVG  58.8") != std::wstring::npos);
    CHECK(shown.text.find(L"1% LOW  49.3") != std::wstring::npos);
    CHECK(shown.text.find(L"16.6 ms") != std::wstring::npos);
    input.process_ram_bytes = 3ULL * 1024 * 1024 * 1024;
    input.dedicated_vram_bytes = 8ULL * 1024 * 1024 * 1024;
    input.show_memory = true;
    shown = overlay::evaluate_overlay(input);
    CHECK(shown.show_memory);
    CHECK(shown.process_ram_gib == 3.0);
    CHECK(shown.dedicated_vram_gib == 8.0);
    input.animations_enabled = false;
    shown = overlay::evaluate_overlay(input);
    CHECK(!shown.animations_enabled);

    input.corner = overlay::OverlayCorner::top_left;
    const auto top_left = overlay::evaluate_overlay(input);
    CHECK(top_left.bounds.left == 112);
    CHECK(top_left.bounds.top == 212);

    input.corner = overlay::OverlayCorner::bottom_right;
    const auto bottom_right = overlay::evaluate_overlay(input);
    CHECK(bottom_right.bounds.right == 1368);
    CHECK(bottom_right.bounds.bottom == 908);

    input.corner = overlay::OverlayCorner::bottom_left;
    auto bottom = overlay::evaluate_overlay(input);
    CHECK(bottom.bounds.left == 112);
    CHECK(bottom.bounds.bottom == 908);
    input.window.monitor_work_bounds = {100, 200, 1380, 880};
    bottom = overlay::evaluate_overlay(input);
    CHECK(bottom.bounds.bottom == 868);
    input.window.monitor_work_bounds = {};
    input.scale = 2.0F;
    auto scaled = overlay::evaluate_overlay(input);
    CHECK(scaled.bounds.right - scaled.bounds.left == 480);

    // A requested scale near the upper limit must not make the overlay vanish
    // merely because the normal outer margin no longer fits.  The policy may
    // reduce that margin (and, as a last resort, the effective scale), but the
    // complete overlay must remain inside the game client.
    input.window.client_bounds = {100, 200, 760, 920};
    input.logical_size = {330, 105};
    input.scale = 1.95F;
    const auto narrow_195 = overlay::evaluate_overlay(input);
    CHECK(narrow_195.visible);
    CHECK(narrow_195.bounds.left >= input.window.client_bounds.left);
    CHECK(narrow_195.bounds.right <= input.window.client_bounds.right);
    input.scale = 2.0F;
    const auto narrow_200 = overlay::evaluate_overlay(input);
    CHECK(narrow_200.visible);
    CHECK(narrow_200.bounds.left == input.window.client_bounds.left);
    CHECK(narrow_200.bounds.right == input.window.client_bounds.right);
    input.window.client_bounds.right = 740;
    const auto fitted_200 = overlay::evaluate_overlay(input);
    CHECK(fitted_200.visible);
    CHECK(fitted_200.bounds.left == input.window.client_bounds.left);
    CHECK(fitted_200.bounds.right == input.window.client_bounds.right);
    CHECK(fitted_200.bounds.bottom - fitted_200.bounds.top == 204);

    input.window.client_bounds = {100, 200, 1380, 920};
    input.logical_size = {240, 90};
    input.scale = 0.6F;
    const auto compact = overlay::evaluate_overlay(input);
    CHECK(compact.visible);
    CHECK(compact.bounds.right - compact.bounds.left == 144);

    input.enabled = false;
    CHECK(overlay::evaluate_overlay(input).reason == overlay::OverlayHideReason::disabled);
    input.enabled = true; input.window.reason = game::WindowUnavailableReason::minimized;
    CHECK(overlay::evaluate_overlay(input).reason == overlay::OverlayHideReason::game_window);
    input.window.reason = game::WindowUnavailableReason::not_foreground;
    CHECK(overlay::evaluate_overlay(input).visible);
    input.window.fully_occluded = true;
    // An in-game provider such as Steam or Discord may cover the game while
    // KF2 remains the valid presentation target. It must not disable our OSD.
    CHECK(overlay::evaluate_overlay(input).visible);
    input.window.fully_occluded = false;
    input.window.reason = game::WindowUnavailableReason::none;
    input.frames.fps.reset(); input.frames.reason = telemetry::UnavailableReason::stale;
    CHECK(overlay::evaluate_overlay(input).reason == overlay::OverlayHideReason::stale_telemetry);
    input.frames.reason = telemetry::UnavailableReason::source_failure;
    CHECK(overlay::evaluate_overlay(input).reason == overlay::OverlayHideReason::telemetry_unavailable);
    input.frames.reason = telemetry::UnavailableReason::none;
    input.frames.fps = 60.0; input.frames.frame_time_ms = 16.7;
    input.window.client_bounds = {0, 0, 100, 50};
    CHECK(overlay::evaluate_overlay(input).reason == overlay::OverlayHideReason::invalid_geometry);
    return EXIT_SUCCESS;
}
