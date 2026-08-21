#include "kf2/overlay/overlay_window.hpp"
#include "overlay_window_internal.hpp"

#include <utility>

namespace kf2::overlay {

OverlayWindow::OverlayWindow(std::unique_ptr<OverlayWindowState> state)
    : state_{std::move(state)} {}
OverlayWindow::OverlayWindow(OverlayWindow&&) noexcept = default;
OverlayWindow& OverlayWindow::operator=(OverlayWindow&&) noexcept = default;
OverlayWindow::~OverlayWindow() = default;

HWND OverlayWindow::native_handle() const noexcept {
    return state_ ? state_->window : nullptr;
}
std::size_t OverlayWindow::render_count() const noexcept {
    return state_ ? state_->renders : 0;
}
}  // namespace kf2::overlay