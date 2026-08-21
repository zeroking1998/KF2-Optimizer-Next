#include <array>
#include <cstdlib>
#include <iostream>

#include "kf2/overlay/overlay_policy.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2;
    game::GameWindowState window;
    window.visible = true;
    window.foreground = true;
    window.client_bounds = {200, 100, 2120, 1180};
    window.monitor_work_bounds = {0, 0, 2560, 1400};
    window.reason = game::WindowUnavailableReason::none;
    telemetry::FrameMetrics frames;
    frames.fps = 120.0;
    frames.average_fps = 118.0;
    frames.one_percent_low_fps = 105.0;
    frames.frame_time_ms = 8.3;
    frames.quality = telemetry::SampleQuality::good;
    frames.reason = telemetry::UnavailableReason::none;

    constexpr std::array corners{
        overlay::OverlayCorner::top_left,
        overlay::OverlayCorner::top_right,
        overlay::OverlayCorner::bottom_left,
        overlay::OverlayCorner::bottom_right,
    };
    for (const auto corner : corners) {
        const auto result = overlay::evaluate_overlay(
            {true, window, frames, corner, 1.25F, {330, 105}, 10});
        CHECK(result.visible);
        CHECK(result.bounds.left >= window.client_bounds.left);
        CHECK(result.bounds.top >= window.client_bounds.top);
        CHECK(result.bounds.right <= window.client_bounds.right);
        CHECK(result.bounds.bottom <= window.client_bounds.bottom);
        CHECK(result.bounds.right - result.bounds.left == 413);
        CHECK(result.bounds.bottom - result.bounds.top == 131);
    }
    return EXIT_SUCCESS;
}
