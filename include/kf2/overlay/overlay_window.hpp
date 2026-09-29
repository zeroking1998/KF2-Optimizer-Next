#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "kf2/core/result.hpp"
#include "kf2/overlay/overlay_policy.hpp"

namespace kf2::overlay {
#if defined(KF2_OVERLAY_WINDOW_TESTING)
struct OverlayWindowTestAccess;
#endif

struct OverlayDiagnostics final {
    bool enabled{false};
    std::uint64_t update_calls{0};
    std::uint64_t redraws{0};
    std::uint64_t skipped_redraws{0};
    std::uint64_t last_render_us{0};
    std::uint64_t maximum_render_us{0};
};

class OverlayWindow final {
public:
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    OverlayWindow(OverlayWindow&&) noexcept;
    OverlayWindow& operator=(OverlayWindow&&) noexcept;
    ~OverlayWindow();
    [[nodiscard]] static Result<OverlayWindow> create();
    [[nodiscard]] Result<bool> update(const OverlayPresentation& presentation);
    void set_diagnostics_enabled(bool enabled) noexcept;
    [[nodiscard]] OverlayDiagnostics diagnostics() const noexcept;
    [[nodiscard]] HWND native_handle() const noexcept;
    [[nodiscard]] std::size_t render_count() const noexcept;
    [[nodiscard]] std::size_t graph_geometry_build_count() const noexcept;
    [[nodiscard]] std::size_t static_layer_build_count() const noexcept;
private:
#if defined(KF2_OVERLAY_WINDOW_TESTING)
    friend struct OverlayWindowTestAccess;
#endif
    explicit OverlayWindow(std::unique_ptr<struct OverlayWindowState> state);
    std::unique_ptr<struct OverlayWindowState> state_;
};
}  // namespace kf2::overlay
