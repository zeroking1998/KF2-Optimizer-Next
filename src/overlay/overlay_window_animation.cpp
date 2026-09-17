#include "overlay_window_internal.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::overlay::detail {

bool same_rect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}
RECT visibility_pose(const RECT& bounds, float scale, LONG outward) {
    const LONG center_x = (bounds.left + bounds.right) / 2;
    const LONG center_y = (bounds.top + bounds.bottom) / 2;
    const LONG half_width = static_cast<LONG>(std::lround(
        (bounds.right - bounds.left) * 0.5F * scale));
    const LONG half_height = static_cast<LONG>(std::lround(
        (bounds.bottom - bounds.top) * 0.5F * scale));
    RECT posed{center_x - half_width, center_y - half_height,
               center_x + half_width, center_y + half_height};
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const LONG monitor_x = (monitor.rcWork.left + monitor.rcWork.right) / 2;
        const LONG monitor_y = (monitor.rcWork.top + monitor.rcWork.bottom) / 2;
        OffsetRect(&posed, center_x < monitor_x ? -outward : outward,
                   center_y < monitor_y ? -outward : outward);
    }
    return posed;
}

void disable_overlay_animations(OverlayWindowState& state) noexcept {
    state.animating = false;
    state.visibility_animation = false;
    state.fps_bounce_started_ms = 0;
    state.average_bounce_started_ms = 0;
    state.low_bounce_started_ms = 0;
    state.frame_time_bounce_started_ms = 0;
    state.cpu_bounce_started_ms = 0;
    state.gpu_bounce_started_ms = 0;
    state.fps_trend_started_ms = 0;
    state.average_trend_started_ms = 0;
    state.low_trend_started_ms = 0;
    state.average_mood_reaction_ms = 0;
    state.low_mood_reaction_ms = 0;
    state.average_tug_offset = 0.0F;
    state.average_tug_velocity = 0.0F;
    state.average_tug_load = 0.0F;
    state.low_tug_offset = 0.0F;
    state.low_tug_velocity = 0.0F;
    state.low_tug_load = 0.0F;
}

void prepare_visibility_animation(
    OverlayWindowState& state,
    const OverlayPresentation& presentation,
    bool geometry_changed,
    ULONGLONG now_ms) {
    if (!geometry_changed) return;

    if (!presentation.animations_enabled) {
        state.current_bounds = presentation.bounds;
        state.animation_from_bounds = presentation.bounds;
        state.opacity = presentation.visible ? 1.0F : 0.0F;
        state.animation_from_opacity = state.opacity;
        return;
    }

    const bool was_visible = state.has_target && state.target.visible;
    state.visibility_animation = was_visible != presentation.visible;
    state.appearing = state.visibility_animation && presentation.visible;
    state.animation_from_bounds = state.has_visual
        ? state.current_bounds
        : (state.appearing
            ? visibility_pose(presentation.bounds, 0.72F, 28)
            : presentation.bounds);
    state.animation_from_opacity = state.opacity;
    state.animation_started_ms = now_ms;
    state.animating = true;
    if (!presentation.visible) return;

    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromRect(
            &presentation.bounds, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return;
    }
    const LONG left_gap = std::abs(presentation.bounds.left - monitor.rcWork.left);
    const LONG right_gap = std::abs(monitor.rcWork.right - presentation.bounds.right);
    const LONG top_gap = std::abs(presentation.bounds.top - monitor.rcWork.top);
    const LONG bottom_gap = std::abs(monitor.rcWork.bottom - presentation.bounds.bottom);
    state.dock_horizontal = left_gap <= right_gap ? -1 : 1;
    state.dock_vertical = top_gap <= bottom_gap ? -1 : 1;
    const LONG travel_x = std::abs(
        presentation.bounds.left - state.animation_from_bounds.left);
    const LONG travel_y = std::abs(
        presentation.bounds.top - state.animation_from_bounds.top);
    state.dock_impact = std::clamp(
        static_cast<float>(travel_x + travel_y) / 420.0F, 0.18F, 1.0F);
    const int next_variant = static_cast<int>(
        (now_ms / 137 + travel_x + travel_y) %
        static_cast<ULONGLONG>(state.mascot_animation.variant_count));
    state.dock_variant = next_variant == state.dock_variant
        ? (next_variant + 1) % state.mascot_animation.variant_count
        : next_variant;
    state.dock_changed_ms = now_ms;
}

RECT advance_visibility_animation(
    OverlayWindowState& state,
    const OverlayPresentation& presentation,
    ULONGLONG now_ms) {
    const ULONGLONG elapsed = now_ms - state.animation_started_ms;
    const float animation_duration_ms = state.visibility_animation
        ? 720.0F : 600.0F;
    const float linear = state.animating
        ? std::min(1.0F, static_cast<float>(elapsed) / animation_duration_ms)
        : 1.0F;
    const float shifted = linear - 1.0F;
    const float progress = 1.0F + 2.70158F * shifted * shifted * shifted +
                           1.70158F * shifted * shifted;
    const RECT target_bounds = presentation.visible
        ? presentation.bounds
        : (state.visibility_animation
            ? visibility_pose(state.visual.bounds, 0.76F, 32)
            : state.visual.bounds);
    const auto interpolate = [progress](LONG from, LONG to) {
        return static_cast<LONG>(std::lround(
            static_cast<float>(from) +
            (static_cast<float>(to - from) * progress)));
    };
    RECT animated_bounds{
        interpolate(state.animation_from_bounds.left, target_bounds.left),
        interpolate(state.animation_from_bounds.top, target_bounds.top),
        interpolate(state.animation_from_bounds.right, target_bounds.right),
        interpolate(state.animation_from_bounds.bottom, target_bounds.bottom)};
    const float from_center_x = (state.animation_from_bounds.left +
                                 state.animation_from_bounds.right) * 0.5F;
    const float from_center_y = (state.animation_from_bounds.top +
                                 state.animation_from_bounds.bottom) * 0.5F;
    const float to_center_x = (target_bounds.left + target_bounds.right) * 0.5F;
    const float to_center_y = (target_bounds.top + target_bounds.bottom) * 0.5F;
    const float delta_x = to_center_x - from_center_x;
    const float delta_y = to_center_y - from_center_y;
    const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (distance > 1.0F && presentation.visible) {
        const float arc = std::min(30.0F, distance * 0.08F) *
                          std::sin(linear * 3.14159265F);
        const LONG arc_x = static_cast<LONG>(std::lround(-delta_y / distance * arc));
        const LONG arc_y = static_cast<LONG>(std::lround(delta_x / distance * arc));
        OffsetRect(&animated_bounds, arc_x, arc_y);
        const float snap_scale = 1.0F -
            0.055F * std::sin(linear * 3.14159265F);
        const LONG center_x = (animated_bounds.left + animated_bounds.right) / 2;
        const LONG center_y = (animated_bounds.top + animated_bounds.bottom) / 2;
        const LONG half_width = static_cast<LONG>(std::lround(
            (animated_bounds.right - animated_bounds.left) * 0.5F * snap_scale));
        const LONG half_height = static_cast<LONG>(std::lround(
            (animated_bounds.bottom - animated_bounds.top) * 0.5F * snap_scale));
        animated_bounds = {center_x - half_width, center_y - half_height,
                           center_x + half_width, center_y + half_height};
    }
    state.current_bounds = animated_bounds;
    const float target_opacity = presentation.visible ? 1.0F : 0.0F;
    const float opacity_progress = linear * linear * (3.0F - 2.0F * linear);
    state.opacity = state.animation_from_opacity +
        (target_opacity - state.animation_from_opacity) * opacity_progress;
    if (linear >= 1.0F) {
        state.animating = false;
        state.visibility_animation = false;
    }
    if (!presentation.animations_enabled) {
        animated_bounds = presentation.bounds;
        state.current_bounds = animated_bounds;
        state.opacity = presentation.visible ? 1.0F : 0.0F;
    }
    return animated_bounds;
}

bool advance_metric_reaction_animation(
    OverlayWindowState& state,
    bool animations_enabled,
    ULONGLONG now_ms) noexcept {
    if (animations_enabled) {
        state.average_mood +=
            (state.average_mood_target - state.average_mood) * 0.055F;
        state.low_mood +=
            (state.low_mood_target - state.low_mood) * 0.055F;
    } else {
        state.average_mood = state.average_mood_target;
        state.low_mood = state.low_mood_target;
    }
    const bool mood_animating = animations_enabled && (
        std::fabs(state.average_mood_target - state.average_mood) > 0.01F ||
        std::fabs(state.low_mood_target - state.low_mood) > 0.01F ||
        (state.average_mood_reaction_ms != 0 &&
         now_ms - state.average_mood_reaction_ms < 720) ||
        (state.low_mood_reaction_ms != 0 &&
         now_ms - state.low_mood_reaction_ms < 720));

    const float rig_blend_in = state.mascot_animation.blend_in_fast;
    const float rig_blend_out = state.mascot_animation.blend_out_soft;
    const auto update_tug = [now_ms, rig_blend_in, rig_blend_out](
                                float& offset, float& velocity, float& load,
                                ULONGLONG started, int direction,
                                float intensity, float strength) {
        const bool force_active = started != 0 &&
            now_ms - started < 300 && intensity >= 0.45F;
        const float force = force_active
            ? static_cast<float>(direction) * intensity * strength : 0.0F;
        velocity += (force - offset) * 0.075F;
        velocity *= force_active ? 0.82F : 0.76F;
        offset += velocity;
        const float target_load = force_active ? intensity : 0.0F;
        const float load_response = target_load > load
            ? rig_blend_in : rig_blend_out;
        load += (target_load - load) * load_response;
        if (!force_active && std::fabs(offset) < 0.025F &&
            std::fabs(velocity) < 0.025F) {
            offset = 0.0F;
            velocity = 0.0F;
        }
        if (!force_active && load < 0.01F) load = 0.0F;
        return force_active || std::fabs(offset) >= 0.025F ||
               std::fabs(velocity) >= 0.025F || load >= 0.01F;
    };
    const bool average_tug_animating = animations_enabled && update_tug(
        state.average_tug_offset, state.average_tug_velocity,
        state.average_tug_load,
        state.average_trend_started_ms, state.average_trend_direction,
        state.average_trend_intensity, 8.0F);
    const bool low_tug_animating = animations_enabled && update_tug(
        state.low_tug_offset, state.low_tug_velocity,
        state.low_tug_load,
        state.low_trend_started_ms, state.low_trend_direction,
        state.low_trend_intensity, 8.0F);
    return mood_animating || average_tug_animating || low_tug_animating;
}

}  // namespace kf2::overlay::detail
