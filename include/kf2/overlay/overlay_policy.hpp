#pragma once
#include <string>
#include "kf2/game/game_session.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::overlay {
enum class OverlayCorner { top_left, top_right, bottom_left, bottom_right };
enum class OverlayHideReason {
    none, disabled, game_window, not_foreground, stale_telemetry,
    telemetry_unavailable, invalid_geometry
};
struct OverlayPolicyInput {
    bool enabled{false};
    game::GameWindowState window;
    telemetry::FrameMetrics frames;
    OverlayCorner corner{OverlayCorner::top_right};
    float scale{1.0F};
    SIZE logical_size{320, 118};
    LONG margin{12};
    std::optional<double> cpu_percent;
    std::optional<double> gpu_percent;
    std::optional<std::uint64_t> process_ram_bytes;
    std::optional<std::uint64_t> dedicated_vram_bytes;
    bool show_fps{true};
    bool show_frame_time{true};
    bool show_cpu{true};
    bool show_gpu{true};
    bool show_memory{false};
    bool animations_enabled{true};
};
struct OverlayPresentation {
    bool visible{false};
    OverlayHideReason reason{OverlayHideReason::disabled};
    RECT bounds{};
    std::wstring text;
    double fps{0.0};
    double average_fps{0.0};
    double one_percent_low_fps{0.0};
    double frame_time_ms{0.0};
    double cpu_percent{0.0};
    double gpu_percent{0.0};
    double process_ram_gib{0.0};
    double dedicated_vram_gib{0.0};
    bool show_fps{true};
    bool show_frame_time{true};
    bool show_cpu{true};
    bool show_gpu{true};
    bool show_memory{false};
    bool animations_enabled{true};
};
[[nodiscard]] OverlayPresentation evaluate_overlay(const OverlayPolicyInput& input);
}  // namespace kf2::overlay
