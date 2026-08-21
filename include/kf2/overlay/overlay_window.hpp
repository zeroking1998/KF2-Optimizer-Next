#pragma once
#include <Windows.h>
#include <cstddef>
#include <memory>
#include "kf2/core/result.hpp"
#include "kf2/overlay/overlay_policy.hpp"

namespace kf2::overlay {
class OverlayWindow final {
public:
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    OverlayWindow(OverlayWindow&&) noexcept;
    OverlayWindow& operator=(OverlayWindow&&) noexcept;
    ~OverlayWindow();
    [[nodiscard]] static Result<OverlayWindow> create();
    [[nodiscard]] Result<bool> update(const OverlayPresentation& presentation);
    [[nodiscard]] HWND native_handle() const noexcept;
    [[nodiscard]] std::size_t render_count() const noexcept;
private:
    explicit OverlayWindow(std::unique_ptr<struct OverlayWindowState> state);
    std::unique_ptr<struct OverlayWindowState> state_;
};
}  // namespace kf2::overlay
