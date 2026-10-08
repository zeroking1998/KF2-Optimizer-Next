#include "kf2/overlay/overlay_window.hpp"
#include "overlay_window_internal.hpp"

#include <utility>

namespace kf2::overlay {

OverlayWindow::OverlayWindow(std::unique_ptr<OverlayWindowState> state)
    : state_{std::move(state)} {}
OverlayWindow::OverlayWindow(OverlayWindow&&) noexcept = default;
OverlayWindow& OverlayWindow::operator=(OverlayWindow&&) noexcept = default;
OverlayWindow::~OverlayWindow() = default;

void OverlayWindow::set_diagnostics_enabled(bool enabled) noexcept {
    if (!state_ || state_->diagnostics_enabled == enabled) return;
    state_->diagnostics_enabled = enabled;
    state_->diagnostic_update_calls = 0;
    state_->diagnostic_redraws = 0;
    state_->diagnostic_skipped_redraws = 0;
    state_->diagnostic_last_render_us = 0;
    state_->diagnostic_maximum_render_us = 0;
    state_->diagnostic_total_render_us = 0;
    state_->diagnostic_counter_frequency = 0;
    state_->graph_geometry_builds = 0;
    state_->static_layer_builds = 0;
    if (enabled) {
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
            state_->diagnostic_counter_frequency =
                static_cast<std::uint64_t>(frequency.QuadPart);
        }
    }
}

OverlayDiagnostics OverlayWindow::diagnostics() const noexcept {
    if (!state_ || !state_->diagnostics_enabled) return {};
    return {
        true,
        state_->diagnostic_update_calls,
        state_->diagnostic_redraws,
        state_->diagnostic_skipped_redraws,
        state_->diagnostic_last_render_us,
        state_->diagnostic_maximum_render_us,
        state_->diagnostic_total_render_us};
}

HWND OverlayWindow::native_handle() const noexcept {
    return state_ ? state_->window : nullptr;
}
std::size_t OverlayWindow::render_count() const noexcept {
    return state_ ? static_cast<std::size_t>(
        state_->diagnostic_redraws) : 0;
}
std::size_t OverlayWindow::graph_geometry_build_count() const noexcept {
    return state_ ? state_->graph_geometry_builds : 0;
}
std::size_t OverlayWindow::static_layer_build_count() const noexcept {
    return state_ ? state_->static_layer_builds : 0;
}
}  // namespace kf2::overlay
