#pragma once

namespace kf2::ui {

inline constexpr unsigned int kAnimationTimerIntervalMs = 16U;
inline constexpr unsigned int kActiveTimerIntervalMs = 120U;
inline constexpr unsigned int kIdleTimerIntervalMs = 500U;
inline constexpr unsigned int kRuntimeTimerId = 1U;
inline constexpr unsigned int kAnimationTimerId = 2U;

[[nodiscard]] constexpr unsigned int runtime_timer_interval_ms(
    bool game_active, bool background_work_active) noexcept {
    if (game_active || background_work_active) {
        return kActiveTimerIntervalMs;
    }
    return kIdleTimerIntervalMs;
}

}  // namespace kf2::ui
