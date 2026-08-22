#include "kf2/ui/ui_animation.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::ui {

float advance_tooltip_opacity(
    float current, bool visible, bool animations_enabled) noexcept {
    current = std::clamp(current, 0.0F, 1.0F);
    if (!animations_enabled) return visible ? 1.0F : 0.0F;
    return visible ? std::min(1.0F, current + kTooltipAnimationStep)
                   : std::max(0.0F, current - kTooltipAnimationStep);
}

float advance_interaction_strength(
    float current, bool held, bool animations_enabled) noexcept {
    current = std::clamp(current, 0.0F, 1.0F);
    if (held) return 1.0F;
    if (!animations_enabled) return 0.0F;
    return std::max(0.0F, current - kInteractionAnimationStep);
}

float advance_hover_strength(
    float current, bool hovered, bool animations_enabled) noexcept {
    current = std::clamp(current, 0.0F, 1.0F);
    if (!animations_enabled) return hovered ? 1.0F : 0.0F;
    return hovered ? std::min(1.0F, current + kHoverAnimationStep)
                   : std::max(0.0F, current - kHoverAnimationStep);
}

float control_press_scale(float interaction_strength) noexcept {
    return 1.0F - 0.008F *
        std::clamp(interaction_strength, 0.0F, 1.0F);
}

float advance_startup_progress(
    float current, bool animations_enabled) noexcept {
    if (!animations_enabled) return 1.0F;
    return std::min(
        1.0F,
        std::clamp(current, 0.0F, 1.0F) + kStartupAnimationStep);
}

float advance_page_progress(
    float current, bool animations_enabled) noexcept {
    if (!animations_enabled) return 1.0F;
    return std::min(1.0F,
                    std::clamp(current, 0.0F, 1.0F) + kPageAnimationStep);
}

float advance_navigation_progress(
    float current, bool animations_enabled) noexcept {
    if (!animations_enabled) return 1.0F;
    return std::min(
        1.0F,
        std::clamp(current, 0.0F, 1.0F) + kNavigationAnimationStep);
}

float advance_exit_progress(
    float current, bool animations_enabled) noexcept {
    if (!animations_enabled) return 1.0F;
    return std::min(
        1.0F,
        std::clamp(current, 0.0F, 1.0F) + kExitAnimationStep);
}

float advance_update_glow_progress(
    float current, bool animations_enabled) noexcept {
    if (!animations_enabled) return 1.0F;
    return std::min(
        1.0F,
        std::clamp(current, 0.0F, 1.0F) + kUpdateGlowAnimationStep);
}

float smooth_motion(float progress) noexcept {
    progress = std::clamp(progress, 0.0F, 1.0F);
    return progress * progress * (3.0F - 2.0F * progress);
}

float startup_logo_scale(float progress) noexcept {
    return interpolate_motion(0.82F, 1.0F, smooth_motion(progress));
}

float startup_title_offset_x(float progress) noexcept {
    return interpolate_motion(-18.0F, 0.0F, smooth_motion(progress));
}

float page_motion_opacity(float progress) noexcept {
    return smooth_motion(progress);
}

float page_motion_offset_x(float progress) noexcept {
    return interpolate_motion(14.0F, 0.0F, smooth_motion(progress));
}

float tooltip_motion_offset_y(float opacity) noexcept {
    return interpolate_motion(4.0F, 0.0F, smooth_motion(opacity));
}

float update_glow_opacity(float progress) noexcept {
    progress = std::clamp(progress, 0.0F, 1.0F);
    constexpr float kPi = 3.14159265358979323846F;
    return std::sin(progress * kPi);
}

float interpolate_motion(float from, float to, float progress) noexcept {
    return from + (to - from) * std::clamp(progress, 0.0F, 1.0F);
}

}  // namespace kf2::ui
