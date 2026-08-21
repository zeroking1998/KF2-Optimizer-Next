#pragma once

#include <algorithm>
#include <cmath>

namespace kf2::optimizer {

inline constexpr int kTargetFpsMinimum = 30;
inline constexpr int kTargetFpsMaximum = 240;
inline constexpr int kTargetFpsValueCount =
    kTargetFpsMaximum - kTargetFpsMinimum + 1;

inline constexpr double kAdaptiveFpsToleranceFraction = 0.025;
inline constexpr double kAdaptiveFpsToleranceMinimum = 3.0;
inline constexpr double kAdaptiveFpsToleranceMaximum = 6.0;

struct AdaptiveStabilityBands final {
    double target_frame_time_ms{0.0};
    double warning_frame_time_ms{0.0};
    double corrective_frame_time_ms{0.0};
    double critical_frame_time_ms{0.0};
};

[[nodiscard]] constexpr bool valid_target_fps(int target_fps) noexcept {
    return target_fps >= kTargetFpsMinimum &&
           target_fps <= kTargetFpsMaximum;
}

[[nodiscard]] inline AdaptiveStabilityBands adaptive_stability_bands(
    int target_fps) noexcept {
    if (!valid_target_fps(target_fps)) return {};
    const double target_fps_value = static_cast<double>(target_fps);
    const double tolerance_fps = std::clamp(
        target_fps_value * kAdaptiveFpsToleranceFraction,
        kAdaptiveFpsToleranceMinimum,
        kAdaptiveFpsToleranceMaximum);
    const double warning_fps = target_fps_value - tolerance_fps / 3.0;
    const double corrective_fps =
        target_fps_value - tolerance_fps * 2.0 / 3.0;
    const double critical_fps = target_fps_value - tolerance_fps;
    if (!std::isfinite(critical_fps) || critical_fps <= 0.0) return {};
    return {
        1000.0 / target_fps_value,
        1000.0 / warning_fps,
        1000.0 / corrective_fps,
        1000.0 / critical_fps,
    };
}

}  // namespace kf2::optimizer
