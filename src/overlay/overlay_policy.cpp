#include "kf2/overlay/overlay_policy.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace kf2::overlay {
OverlayPresentation evaluate_overlay(const OverlayPolicyInput& input) {
    OverlayPresentation output;
    if (!input.enabled) return output;
    if ((input.window.reason != game::WindowUnavailableReason::none &&
         input.window.reason != game::WindowUnavailableReason::not_foreground) ||
        !input.window.visible || input.window.minimized || input.window.cloaked) {
        output.reason = OverlayHideReason::game_window; return output;
    }
    if (!input.frames.fps || !input.frames.frame_time_ms) {
        output.reason = input.frames.reason == telemetry::UnavailableReason::stale
                            ? OverlayHideReason::stale_telemetry
                            : OverlayHideReason::telemetry_unavailable;
        return output;
    }
    if (input.frames.quality == telemetry::SampleQuality::unavailable) {
        output.reason = OverlayHideReason::telemetry_unavailable; return output;
    }
    if (input.scale < 0.6F || input.scale > 2.0F || input.margin < 0) {
        output.reason = OverlayHideReason::invalid_geometry; return output;
    }
    const LONG available_width = input.window.client_bounds.right - input.window.client_bounds.left;
    LONG usable_bottom = input.window.client_bounds.bottom;
    if (input.window.monitor_work_bounds.bottom >
            input.window.monitor_work_bounds.top) {
        usable_bottom = std::min(usable_bottom,
                                 input.window.monitor_work_bounds.bottom);
    }
    const LONG available_height = usable_bottom - input.window.client_bounds.top;
    if (input.logical_size.cx <= 0 || input.logical_size.cy <= 0 ||
        available_width <= 0 || available_height <= 0) {
        output.reason = OverlayHideReason::invalid_geometry; return output;
    }
    // Preserve the requested 60-200% range whenever it fits.  On narrow game
    // clients, first consume the cosmetic outer margin and then fit the overlay
    // proportionally instead of hiding it at the upper end of the slider.
    const float effective_scale = std::min({
        input.scale,
        static_cast<float>(available_width) /
            static_cast<float>(input.logical_size.cx),
        static_cast<float>(available_height) /
            static_cast<float>(input.logical_size.cy)});
    if (effective_scale < 0.6F) {
        output.reason = OverlayHideReason::invalid_geometry; return output;
    }
    const LONG width = std::min(
        available_width, static_cast<LONG>(std::lround(
            input.logical_size.cx * effective_scale)));
    const LONG height = std::min(
        available_height, static_cast<LONG>(std::lround(
            input.logical_size.cy * effective_scale)));
    if (width <= 0 || height <= 0) {
        output.reason = OverlayHideReason::invalid_geometry; return output;
    }
    const LONG horizontal_margin = std::min(
        input.margin, std::max(0L, (available_width - width) / 2));
    const LONG vertical_margin = std::min(
        input.margin, std::max(0L, (available_height - height) / 2));
    const bool right = input.corner == OverlayCorner::top_right ||
                       input.corner == OverlayCorner::bottom_right;
    const bool bottom = input.corner == OverlayCorner::bottom_left ||
                        input.corner == OverlayCorner::bottom_right;
    output.bounds.left = right
        ? input.window.client_bounds.right - horizontal_margin - width
        : input.window.client_bounds.left + horizontal_margin;
    output.bounds.top = bottom
        ? usable_bottom - vertical_margin - height
        : input.window.client_bounds.top + vertical_margin;
    output.bounds.right = output.bounds.left + width;
    output.bounds.bottom = output.bounds.top + height;
    const auto rounded_fps = std::round(*input.frames.fps * 10.0) / 10.0;
    const auto rounded_average = std::round(
        input.frames.average_fps.value_or(*input.frames.fps) * 10.0) / 10.0;
    const auto rounded_low = std::round(
        input.frames.one_percent_low_fps.value_or(*input.frames.fps) * 10.0) / 10.0;
    const auto rounded_ms = std::round(*input.frames.frame_time_ms * 10.0) / 10.0;
    std::wostringstream text;
    text << L"KF2 PERFORMANCE\n" << std::fixed << std::setprecision(1)
         << L"FPS  " << rounded_fps << L"     AVG  " << rounded_average << L"\n"
         << L"1% LOW  " << rounded_low << L"     " << rounded_ms << L" ms";
    output.text = text.str();
    output.fps = rounded_fps;
    output.average_fps = rounded_average;
    output.one_percent_low_fps = rounded_low;
    output.frame_time_ms = rounded_ms;
    output.cpu_percent = std::round(input.cpu_percent.value_or(0.0) * 10.0) / 10.0;
    output.gpu_percent = std::round(input.gpu_percent.value_or(0.0) * 10.0) / 10.0;
    constexpr double bytes_per_gib = 1024.0 * 1024.0 * 1024.0;
    output.process_ram_gib = input.process_ram_bytes.value_or(0) / bytes_per_gib;
    output.dedicated_vram_gib = input.dedicated_vram_bytes.value_or(0) / bytes_per_gib;
    output.show_fps = input.show_fps;
    output.show_frame_time = input.show_frame_time;
    output.show_cpu = input.show_cpu;
    output.show_gpu = input.show_gpu;
    output.show_memory = input.show_memory;
    output.animations_enabled = input.animations_enabled;
    output.visible = true;
    output.reason = OverlayHideReason::none;
    return output;
}
}  // namespace kf2::overlay
