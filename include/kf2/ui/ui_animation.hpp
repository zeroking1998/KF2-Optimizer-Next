#pragma once

namespace kf2::ui {

// Progress per 8 ms UI tick. These intentionally calm timings borrow the
// movement principles of Glide, Appear, Sliding Name and button-hover motion
// without embedding external animation assets or runtimes.
inline constexpr float kTooltipAnimationStep = 1.0F / 45.0F;    // 360 ms
inline constexpr float kHoverAnimationStep = 1.0F / 35.0F;      // 280 ms
inline constexpr float kInteractionAnimationStep = 1.0F / 30.0F; // 240 ms
inline constexpr float kStartupAnimationStep = 1.0F / 80.0F;    // 640 ms
inline constexpr float kPageAnimationStep = 1.0F / 53.0F;       // 424 ms
inline constexpr float kNavigationAnimationStep = 1.0F / 58.0F; // 464 ms
inline constexpr float kExitAnimationStep = 1.0F / 45.0F;       // 360 ms
inline constexpr float kUpdateGlowAnimationStep = 1.0F / 200.0F; // 1.6 s

[[nodiscard]] float advance_tooltip_opacity(
    float current, bool visible, bool animations_enabled) noexcept;
[[nodiscard]] float advance_interaction_strength(
    float current, bool held, bool animations_enabled) noexcept;
[[nodiscard]] float advance_hover_strength(
    float current, bool hovered, bool animations_enabled) noexcept;
[[nodiscard]] float control_press_scale(float interaction_strength) noexcept;
[[nodiscard]] float advance_startup_progress(
    float current, bool animations_enabled) noexcept;
[[nodiscard]] float advance_page_progress(
    float current, bool animations_enabled) noexcept;
[[nodiscard]] float advance_navigation_progress(
    float current, bool animations_enabled) noexcept;
[[nodiscard]] float advance_exit_progress(
    float current, bool animations_enabled) noexcept;
[[nodiscard]] float advance_update_glow_progress(
    float current, bool animations_enabled) noexcept;

[[nodiscard]] float smooth_motion(float progress) noexcept;
[[nodiscard]] float startup_logo_scale(float progress) noexcept;
[[nodiscard]] float startup_title_offset_x(float progress) noexcept;
[[nodiscard]] float page_motion_opacity(float progress) noexcept;
[[nodiscard]] float page_motion_offset_x(float progress) noexcept;
[[nodiscard]] float tooltip_motion_offset_y(float opacity) noexcept;
[[nodiscard]] float update_glow_opacity(float progress) noexcept;
[[nodiscard]] float interpolate_motion(
    float from, float to, float progress) noexcept;

}  // namespace kf2::ui
