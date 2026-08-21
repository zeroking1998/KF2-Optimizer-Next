#include "kf2/overlay/overlay_window.hpp"
#include "overlay_window_internal.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace kf2::overlay {

Result<bool> OverlayWindow::update(const OverlayPresentation& presentation) {
    if (!state_) return Result<bool>::failure(
        {ErrorCode::internal_failure, L"Overlay state is unavailable", 0});
    if (!IsWindow(state_->window)) {
        state_->window = detail::create_overlay_native_window(GetModuleHandleW(nullptr));
        if (!state_->window) return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Overlay window could not be recovered", GetLastError()});
    }
    // A click-through tool window must never remain iconic.  Windows helpers
    // can otherwise select it as the process' main window and minimize it;
    // repeated layered-window frames then alternate between the iconic and
    // rendered rectangles, which appears as whole-overlay flicker.
    if (presentation.visible && IsIconic(state_->window)) {
        ShowWindow(state_->window, SW_SHOWNOACTIVATE);
        if (IsIconic(state_->window)) {
            return Result<bool>::failure(
                {ErrorCode::platform_failure,
                 L"Overlay window could not recover from a minimized state",
                 GetLastError()});
        }
    }
    if (!presentation.animations_enabled) {
        state_->animating = false;
        state_->visibility_animation = false;
        state_->metrics_animating = false;
        state_->fps_bounce_started_ms = 0;
        state_->average_bounce_started_ms = 0;
        state_->low_bounce_started_ms = 0;
        state_->frame_time_bounce_started_ms = 0;
        state_->cpu_bounce_started_ms = 0;
        state_->gpu_bounce_started_ms = 0;
        state_->fps_trend_started_ms = 0;
        state_->average_trend_started_ms = 0;
        state_->low_trend_started_ms = 0;
        state_->average_mood_reaction_ms = 0;
        state_->low_mood_reaction_ms = 0;
        state_->average_tug_offset = 0.0F;
        state_->average_tug_velocity = 0.0F;
        state_->average_tug_load = 0.0F;
        state_->low_tug_offset = 0.0F;
        state_->low_tug_velocity = 0.0F;
        state_->low_tug_load = 0.0F;
    }
    const bool geometry_changed = !state_->has_target ||
        state_->target.visible != presentation.visible ||
        (presentation.visible &&
         !detail::same_rect(state_->target.bounds, presentation.bounds));
    const bool content_changed = !state_->has_target ||
        state_->target.text != presentation.text ||
        state_->target.show_fps != presentation.show_fps ||
        state_->target.show_frame_time != presentation.show_frame_time ||
        state_->target.show_cpu != presentation.show_cpu ||
        state_->target.show_gpu != presentation.show_gpu ||
        state_->target.show_memory != presentation.show_memory ||
        state_->target.animations_enabled != presentation.animations_enabled;
    const OverlayPresentation previous_target = state_->target;
    const bool fps_changed = !state_->metrics_initialized ||
        std::fabs(state_->target.fps - presentation.fps) >= 1.0;
    const bool average_changed = !state_->metrics_initialized ||
        std::fabs(state_->target.average_fps - presentation.average_fps) >= 1.0;
    const bool low_changed = !state_->metrics_initialized ||
        std::fabs(state_->target.one_percent_low_fps -
                  presentation.one_percent_low_fps) >= 1.0;
    const bool frame_time_changed = !state_->metrics_initialized ||
        std::fabs(state_->target.frame_time_ms - presentation.frame_time_ms) >= 0.3;
    const ULONGLONG sample_now_ms = GetTickCount64();
    const bool system_metrics_due = !state_->metrics_initialized ||
        state_->system_metrics_sample_ms == 0 ||
        sample_now_ms - state_->system_metrics_sample_ms >= 1000;
    const bool cpu_changed = system_metrics_due &&
        (!state_->metrics_initialized ||
         std::fabs(state_->target.cpu_percent - presentation.cpu_percent) >= 1.0);
    const bool gpu_changed = system_metrics_due &&
        (!state_->metrics_initialized ||
         std::fabs(state_->target.gpu_percent - presentation.gpu_percent) >= 1.0);
    const bool ram_changed = system_metrics_due && presentation.show_memory &&
        (!state_->metrics_initialized ||
         std::fabs(state_->target.process_ram_gib - presentation.process_ram_gib) >= 0.05);
    const bool vram_changed = system_metrics_due && presentation.show_memory &&
        (!state_->metrics_initialized ||
         std::fabs(state_->target.dedicated_vram_gib -
                   presentation.dedicated_vram_gib) >= 0.05);
    const bool any_metric_changed = fps_changed || average_changed || low_changed ||
        frame_time_changed || cpu_changed || gpu_changed || ram_changed || vram_changed;
    if (cpu_changed || gpu_changed || ram_changed || vram_changed) {
        state_->system_metrics_sample_ms = sample_now_ms;
    }
    bool graph_sampled = false;
    if (presentation.visible && presentation.frame_time_ms > 0.0 &&
        (state_->frame_time_history_sample_ms == 0 ||
         sample_now_ms - state_->frame_time_history_sample_ms >= 100)) {
        state_->frame_time_history[state_->frame_time_history_next] =
            static_cast<float>(presentation.frame_time_ms);
        state_->frame_time_history_next =
            (state_->frame_time_history_next + 1) % state_->frame_time_history.size();
        state_->frame_time_history_count = std::min(
            state_->frame_time_history_count + 1,
            state_->frame_time_history.size());
        state_->frame_time_history_sample_ms = sample_now_ms;
        graph_sampled = true;
    }
    if (geometry_changed && presentation.animations_enabled) {
        const bool was_visible = state_->has_target && state_->target.visible;
        state_->visibility_animation = was_visible != presentation.visible;
        state_->appearing = state_->visibility_animation && presentation.visible;
        state_->animation_from_bounds = state_->has_visual
            ? state_->current_bounds
            : (state_->appearing
                ? detail::visibility_pose(presentation.bounds, 0.72F, 28)
                : presentation.bounds);
        state_->animation_from_opacity = state_->opacity;
        state_->animation_started_ms = GetTickCount64();
        state_->animating = true;
        if (presentation.visible) {
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromRect(&presentation.bounds,
                                                MONITOR_DEFAULTTONEAREST),
                                &monitor)) {
                const LONG left_gap = std::abs(presentation.bounds.left -
                                               monitor.rcWork.left);
                const LONG right_gap = std::abs(monitor.rcWork.right -
                                                presentation.bounds.right);
                const LONG top_gap = std::abs(presentation.bounds.top -
                                              monitor.rcWork.top);
                const LONG bottom_gap = std::abs(monitor.rcWork.bottom -
                                                 presentation.bounds.bottom);
                state_->dock_horizontal = left_gap <= right_gap ? -1 : 1;
                state_->dock_vertical = top_gap <= bottom_gap ? -1 : 1;
                const LONG travel_x = std::abs(
                    presentation.bounds.left - state_->animation_from_bounds.left);
                const LONG travel_y = std::abs(
                    presentation.bounds.top - state_->animation_from_bounds.top);
                state_->dock_impact = std::clamp(
                    static_cast<float>(travel_x + travel_y) / 420.0F,
                    0.18F, 1.0F);
                const int next_variant = static_cast<int>(
                    (GetTickCount64() / 137 + travel_x + travel_y) %
                    static_cast<ULONGLONG>(state_->mascot_animation.variant_count));
                state_->dock_variant = next_variant == state_->dock_variant
                    ? (next_variant + 1) % state_->mascot_animation.variant_count
                    : next_variant;
                state_->dock_changed_ms = GetTickCount64();
            }
        }
    }
    if (geometry_changed && !presentation.animations_enabled) {
        state_->current_bounds = presentation.bounds;
        state_->animation_from_bounds = presentation.bounds;
        state_->opacity = presentation.visible ? 1.0F : 0.0F;
        state_->animation_from_opacity = state_->opacity;
    }
    if (presentation.visible && any_metric_changed) {
        const ULONGLONG now_ms = GetTickCount64();
        if (state_->metrics_initialized) {
            const auto update_trend = [&](double current, double previous,
                                          double minimum_delta,
                                          double relative_dead_zone,
                                          ULONGLONG& started_ms,
                                          int& trend_direction,
                                          float& trend_intensity,
                                          double intensity_range,
                                          float maximum_intensity) {
                const double delta = current - previous;
                const double threshold = std::max(
                    minimum_delta, std::fabs(previous) * relative_dead_zone);
                if (std::fabs(delta) < threshold) return;
                const int next_direction = delta < 0.0 ? -1 : 1;
                const bool direction_changed = trend_direction != 0 &&
                                               trend_direction != next_direction;
                if (direction_changed && std::fabs(delta) < threshold * 1.35)
                    return;
                if (!direction_changed && started_ms != 0 &&
                    now_ms - started_ms < 1100)
                    return;
                trend_direction = next_direction;
                trend_intensity = std::clamp(
                    static_cast<float>((std::fabs(delta) - threshold) /
                                       intensity_range),
                    0.16F, maximum_intensity);
                started_ms = now_ms;
            };
            const auto update_bounce = [now_ms](double current, double previous,
                                                 ULONGLONG& started_ms,
                                                 float& strength) {
                const double delta = std::fabs(current - previous);
                const double quiet_zone = std::max(2.0, std::fabs(previous) * 0.025);
                if (delta <= quiet_zone ||
                    (started_ms != 0 && now_ms - started_ms < 520)) return;
                strength = std::clamp(
                    static_cast<float>((delta - quiet_zone) /
                                       std::max(8.0, std::fabs(previous) * 0.16)),
                    0.12F, 1.0F);
                started_ms = now_ms;
            };
            if (fps_changed) {
                update_bounce(presentation.fps, state_->target.fps,
                              state_->fps_bounce_started_ms,
                              state_->fps_bounce_strength);
                update_trend(presentation.fps, state_->target.fps,
                             4.0, 0.009, state_->fps_trend_started_ms,
                             state_->fps_trend_direction,
                             state_->fps_trend_intensity, 35.0, 1.0F);
            }
            if (average_changed) {
                update_bounce(presentation.average_fps, state_->target.average_fps,
                              state_->average_bounce_started_ms,
                              state_->average_bounce_strength);
                update_trend(presentation.average_fps, state_->target.average_fps,
                             3.0, 0.007, state_->average_trend_started_ms,
                             state_->average_trend_direction,
                             state_->average_trend_intensity, 30.0, 0.82F);
            }
            if (low_changed) {
                update_bounce(presentation.one_percent_low_fps,
                              state_->target.one_percent_low_fps,
                              state_->low_bounce_started_ms,
                              state_->low_bounce_strength);
                update_trend(presentation.one_percent_low_fps,
                             state_->target.one_percent_low_fps,
                             4.0, 0.012, state_->low_trend_started_ms,
                             state_->low_trend_direction,
                             state_->low_trend_intensity, 28.0, 0.88F);
            }
            if (frame_time_changed) state_->frame_time_bounce_started_ms = now_ms;
            const auto changed_digits = [](double previous, double current) {
                const auto padded = [](double value) {
                    std::wstring text = std::to_wstring(
                        std::clamp(static_cast<int>(std::lround(value)), 0, 100));
                    return std::wstring(3 - std::min<std::size_t>(3, text.size()), L' ') +
                           text;
                };
                const auto before = padded(previous);
                const auto after = padded(current);
                return std::array<bool, 3>{before[0] != after[0],
                                           before[1] != after[1],
                                           before[2] != after[2]};
            };
            if (cpu_changed) {
                state_->cpu_changed_digits = changed_digits(
                    state_->target.cpu_percent, presentation.cpu_percent);
                state_->cpu_bounce_started_ms = now_ms;
            }
            if (gpu_changed) {
                state_->gpu_changed_digits = changed_digits(
                    state_->target.gpu_percent, presentation.gpu_percent);
                state_->gpu_bounce_started_ms = now_ms;
            }
        }
        state_->metrics_from_fps = state_->displayed_fps;
        state_->metrics_from_average_fps = state_->displayed_average_fps;
        state_->metrics_from_one_percent_low_fps =
            state_->displayed_one_percent_low_fps;
        state_->metrics_from_frame_time_ms = state_->displayed_frame_time_ms;
        state_->metrics_from_cpu_percent = state_->displayed_cpu_percent;
        state_->metrics_from_gpu_percent = state_->displayed_gpu_percent;
        state_->metrics_from_process_ram_gib = state_->displayed_process_ram_gib;
        state_->metrics_from_dedicated_vram_gib =
            state_->displayed_dedicated_vram_gib;
        state_->metrics_started_ms = GetTickCount64();
        state_->metrics_animating = true;
        state_->metrics_initialized = true;
        if (!presentation.animations_enabled) {
            state_->displayed_fps = presentation.fps;
            state_->displayed_average_fps = presentation.average_fps;
            state_->displayed_one_percent_low_fps =
                presentation.one_percent_low_fps;
            state_->displayed_frame_time_ms = presentation.frame_time_ms;
            state_->displayed_cpu_percent = presentation.cpu_percent;
            state_->displayed_gpu_percent = presentation.gpu_percent;
            state_->displayed_process_ram_gib = presentation.process_ram_gib;
            state_->displayed_dedicated_vram_gib =
                presentation.dedicated_vram_gib;
            state_->metrics_animating = false;
        }
    }
    state_->target = presentation;
    if (!fps_changed) state_->target.fps = previous_target.fps;
    if (!average_changed) state_->target.average_fps = previous_target.average_fps;
    if (!low_changed) {
        state_->target.one_percent_low_fps = previous_target.one_percent_low_fps;
    }
    if (!frame_time_changed) {
        state_->target.frame_time_ms = previous_target.frame_time_ms;
    }
    if (!cpu_changed) state_->target.cpu_percent = previous_target.cpu_percent;
    if (!gpu_changed) state_->target.gpu_percent = previous_target.gpu_percent;
    if (!ram_changed) state_->target.process_ram_gib = previous_target.process_ram_gib;
    if (!vram_changed) {
        state_->target.dedicated_vram_gib = previous_target.dedicated_vram_gib;
    }
    if (presentation.visible && content_changed && presentation.average_fps > 1.0) {
        if (state_->normal_average_fps <= 1.0) {
            state_->normal_average_fps = presentation.average_fps;
        } else {
            const double ratio = presentation.average_fps / state_->normal_average_fps;
            const double learning = ratio >= 0.88 ? 0.025 : 0.004;
            state_->normal_average_fps +=
                (presentation.average_fps - state_->normal_average_fps) * learning;
        }
        const float average_ratio = static_cast<float>(
            presentation.average_fps / std::max(1.0, state_->normal_average_fps));
        const float new_average_mood = std::clamp(
            (average_ratio - 0.84F) / 0.16F * 2.0F - 1.0F, -1.0F, 1.0F);
        const float low_ratio = static_cast<float>(
            presentation.one_percent_low_fps /
            std::max(1.0, presentation.average_fps));
        const float new_low_mood = std::clamp(
            (low_ratio - 0.58F) / 0.32F * 2.0F - 1.0F, -1.0F, 1.0F);
        const ULONGLONG mood_now = GetTickCount64();
        if (std::fabs(new_average_mood - state_->average_mood_target) > 0.14F) {
            state_->average_mood_reaction = std::clamp(
                std::fabs(new_average_mood - state_->average_mood_target), 0.0F, 1.0F);
            state_->average_mood_target = new_average_mood;
            state_->average_mood_reaction_ms = mood_now;
        }
        if (std::fabs(new_low_mood - state_->low_mood_target) > 0.14F) {
            state_->low_mood_reaction = std::clamp(
                std::fabs(new_low_mood - state_->low_mood_target), 0.0F, 1.0F);
            state_->low_mood_target = new_low_mood;
            state_->low_mood_reaction_ms = mood_now;
        }
    }
    state_->has_target = true;
    if (presentation.visible) {
        state_->visual = presentation;
        state_->has_visual = true;
    }
    if (!state_->has_visual) {
        ShowWindow(state_->window, SW_HIDE);
        state_->animating = false;
        return Result<bool>::success(geometry_changed);
    }
    const ULONGLONG update_now_ms = GetTickCount64();
    const auto bounce_active = [update_now_ms](ULONGLONG started) {
        return started != 0 && update_now_ms - started < 420;
    };
    const bool number_bounce_animating =
        bounce_active(state_->fps_bounce_started_ms) ||
        bounce_active(state_->average_bounce_started_ms) ||
        bounce_active(state_->low_bounce_started_ms) ||
        bounce_active(state_->frame_time_bounce_started_ms) ||
        bounce_active(state_->cpu_bounce_started_ms) ||
        bounce_active(state_->gpu_bounce_started_ms) ||
        (state_->fps_trend_started_ms != 0 &&
         update_now_ms - state_->fps_trend_started_ms < 620) ||
        (state_->average_trend_started_ms != 0 &&
         update_now_ms - state_->average_trend_started_ms < 620) ||
        (state_->low_trend_started_ms != 0 &&
         update_now_ms - state_->low_trend_started_ms < 620);
    if (presentation.animations_enabled) {
        state_->average_mood +=
            (state_->average_mood_target - state_->average_mood) * 0.055F;
        state_->low_mood +=
            (state_->low_mood_target - state_->low_mood) * 0.055F;
    } else {
        state_->average_mood = state_->average_mood_target;
        state_->low_mood = state_->low_mood_target;
    }
    const bool mood_animating = presentation.animations_enabled && (
        std::fabs(state_->average_mood_target - state_->average_mood) > 0.01F ||
        std::fabs(state_->low_mood_target - state_->low_mood) > 0.01F ||
        (state_->average_mood_reaction_ms != 0 &&
         update_now_ms - state_->average_mood_reaction_ms < 720) ||
        (state_->low_mood_reaction_ms != 0 &&
         update_now_ms - state_->low_mood_reaction_ms < 720));
    const bool mascot_idle_animating = presentation.animations_enabled &&
        state_->has_target && state_->target.visible;
    const float rig_blend_in = state_->mascot_animation.blend_in_fast;
    const float rig_blend_out = state_->mascot_animation.blend_out_soft;
    const auto update_tug = [update_now_ms, rig_blend_in, rig_blend_out](
                                            float& offset, float& velocity,
                                            float& load,
                                            ULONGLONG started, int direction,
                                            float intensity, float strength) {
        const bool force_active = started != 0 &&
            update_now_ms - started < 300 && intensity >= 0.45F;
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
    const bool average_tug_animating = presentation.animations_enabled && update_tug(
        state_->average_tug_offset, state_->average_tug_velocity,
        state_->average_tug_load,
        state_->average_trend_started_ms, state_->average_trend_direction,
        state_->average_trend_intensity, 8.0F);
    const bool low_tug_animating = presentation.animations_enabled && update_tug(
        state_->low_tug_offset, state_->low_tug_velocity,
        state_->low_tug_load,
        state_->low_trend_started_ms, state_->low_trend_direction,
        state_->low_trend_intensity, 8.0F);
    if (!geometry_changed && !content_changed && !any_metric_changed && !graph_sampled &&
        !state_->animating &&
        !state_->metrics_animating && !number_bounce_animating && !mood_animating &&
        !average_tug_animating && !low_tug_animating && !mascot_idle_animating) {
        return Result<bool>::success(false);
    }

    if (state_->metrics_animating) {
        constexpr float metric_duration_ms = 280.0F;
        const float metric_linear = std::min(
            1.0F, static_cast<float>(GetTickCount64() - state_->metrics_started_ms) /
                      metric_duration_ms);
        // The value itself follows a precise, jerk-free quintic curve; only
        // the visual transform springs, so the displayed measurement stays true.
        const float metric_progress = metric_linear * metric_linear * metric_linear *
            (metric_linear * (metric_linear * 6.0F - 15.0F) + 10.0F);
        const auto animate_metric = [metric_progress](double from, double to) {
            return from + (to - from) * metric_progress;
        };
        state_->displayed_fps = animate_metric(
            state_->metrics_from_fps, state_->target.fps);
        state_->displayed_average_fps = animate_metric(
            state_->metrics_from_average_fps, state_->target.average_fps);
        state_->displayed_one_percent_low_fps = animate_metric(
            state_->metrics_from_one_percent_low_fps,
            state_->target.one_percent_low_fps);
        state_->displayed_frame_time_ms = animate_metric(
            state_->metrics_from_frame_time_ms, state_->target.frame_time_ms);
        state_->displayed_cpu_percent = animate_metric(
            state_->metrics_from_cpu_percent, state_->target.cpu_percent);
        state_->displayed_gpu_percent = animate_metric(
            state_->metrics_from_gpu_percent, state_->target.gpu_percent);
        state_->displayed_process_ram_gib = animate_metric(
            state_->metrics_from_process_ram_gib,
            state_->target.process_ram_gib);
        state_->displayed_dedicated_vram_gib = animate_metric(
            state_->metrics_from_dedicated_vram_gib,
            state_->target.dedicated_vram_gib);
        state_->metrics_animating = metric_linear < 1.0F;
    }

    const ULONGLONG elapsed = GetTickCount64() - state_->animation_started_ms;
    const float animation_duration_ms = state_->visibility_animation
        ? 720.0F : 600.0F;
    const float linear = state_->animating
        ? std::min(1.0F, static_cast<float>(elapsed) / animation_duration_ms)
        : 1.0F;
    const float shifted = linear - 1.0F;
    const float progress = 1.0F + 2.70158F * shifted * shifted * shifted +
                           1.70158F * shifted * shifted;
    const RECT target_bounds = presentation.visible
        ? presentation.bounds
        : (state_->visibility_animation
            ? detail::visibility_pose(state_->visual.bounds, 0.76F, 32)
            : state_->visual.bounds);
    const auto interpolate = [progress](LONG from, LONG to) {
        return static_cast<LONG>(std::lround(
            static_cast<float>(from) +
            (static_cast<float>(to - from) * progress)));
    };
    RECT animated_bounds{
        interpolate(state_->animation_from_bounds.left, target_bounds.left),
        interpolate(state_->animation_from_bounds.top, target_bounds.top),
        interpolate(state_->animation_from_bounds.right, target_bounds.right),
        interpolate(state_->animation_from_bounds.bottom, target_bounds.bottom)};
    const float from_center_x = (state_->animation_from_bounds.left +
                                 state_->animation_from_bounds.right) * 0.5F;
    const float from_center_y = (state_->animation_from_bounds.top +
                                 state_->animation_from_bounds.bottom) * 0.5F;
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
    state_->current_bounds = animated_bounds;
    const float target_opacity = presentation.visible ? 1.0F : 0.0F;
    const float opacity_progress = linear * linear * (3.0F - 2.0F * linear);
    state_->opacity = state_->animation_from_opacity +
        (target_opacity - state_->animation_from_opacity) * opacity_progress;
    if (linear >= 1.0F) {
        state_->animating = false;
        state_->visibility_animation = false;
    }
    if (!presentation.animations_enabled) {
        animated_bounds = presentation.bounds;
        state_->current_bounds = animated_bounds;
        state_->opacity = presentation.visible ? 1.0F : 0.0F;
    }

    if (!presentation.visible && !state_->animating) {
        state_->opacity = 0.0F;
        state_->has_visual = false;
        ShowWindow(state_->window, SW_HIDE);
        return Result<bool>::success(true);
    }
    const LONG width = animated_bounds.right - animated_bounds.left;
    const LONG height = animated_bounds.bottom - animated_bounds.top;
    if (width <= 0 || height <= 0) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Overlay dimensions are invalid", 0});
    if (state_->bitmap_size.cx != width || state_->bitmap_size.cy != height) {
        if (state_->old_bitmap) SelectObject(state_->memory_dc, state_->old_bitmap);
        if (state_->bitmap) DeleteObject(state_->bitmap);
        state_->bitmap = nullptr;
        state_->bitmap_size = {};
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP next_bitmap = CreateDIBSection(state_->memory_dc, &info,
                                                DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!next_bitmap || !pixels) return Result<bool>::failure(
            {ErrorCode::platform_failure, L"Overlay bitmap cannot be created", GetLastError()});
        const HGDIOBJ previous_bitmap = SelectObject(state_->memory_dc, next_bitmap);
        if (!previous_bitmap || previous_bitmap == HGDI_ERROR) {
            const DWORD native_error = GetLastError();
            DeleteObject(next_bitmap);
            return Result<bool>::failure(
                {ErrorCode::platform_failure,
                 L"Overlay bitmap cannot be selected", native_error});
        }
        state_->bitmap = next_bitmap;
        state_->old_bitmap = previous_bitmap;
        state_->bitmap_size = {width, height};
    }
    RECT local{0, 0, width, height};
    HRESULT result = state_->render_target->BindDC(state_->memory_dc, &local);
    if (SUCCEEDED(result)) {
        state_->render_target->BeginDraw();
        state_->render_target->Clear(D2D1::ColorF(0, 0.0F));
        constexpr float logical_canvas_width = 330.0F;
        constexpr float logical_content_width = 322.0F;
        constexpr float logical_height = 105.0F;
        const auto base_transform = D2D1::Matrix3x2F::Translation(8.0F, 0.0F) *
            D2D1::Matrix3x2F::Scale(
                static_cast<float>(width) / logical_canvas_width,
                static_cast<float>(height) / logical_height);
        state_->render_target->SetTransform(base_transform);
        const auto card = D2D1::RoundedRect(
            D2D1::RectF(2.0F, 2.0F, logical_content_width - 2.0F,
                         logical_height - 2.0F), 11, 11);
        state_->render_target->FillRoundedRectangle(card, state_->background.Get());
        state_->render_target->DrawRoundedRectangle(card, state_->border.Get(), 1.0F);
        state_->render_target->DrawLine(D2D1::Point2F(14, 9),
                                        D2D1::Point2F(
                                            72 + 22 * std::sin(
                                                linear * 3.14159265F) +
                                                5 * std::sin(
                                                linear * 9.42477796F), 9),
                                        state_->accent.Get(), 3.0F);
        const auto average_panel = D2D1::RoundedRect(
            D2D1::RectF(140.0F, 6.0F, 223.0F, 66.0F), 6.0F, 6.0F);
        const auto low_panel = D2D1::RoundedRect(
            D2D1::RectF(225.0F, 6.0F, 315.0F, 66.0F), 6.0F, 6.0F);
        const auto system_panel = D2D1::RoundedRect(
            D2D1::RectF(76.0F, 13.0F, 139.0F, 64.0F), 5.0F, 5.0F);
        state_->render_target->FillRoundedRectangle(
            system_panel, state_->metric_panel.Get());
        state_->render_target->DrawRoundedRectangle(
            system_panel, state_->border.Get(), 1.0F);
        state_->render_target->FillRoundedRectangle(
            average_panel, state_->metric_panel.Get());
        state_->render_target->DrawRoundedRectangle(
            average_panel, state_->border.Get(), 1.0F);
        state_->render_target->FillRoundedRectangle(
            low_panel, state_->metric_panel.Get());
        state_->render_target->DrawRoundedRectangle(
            low_panel, state_->border.Get(), 1.0F);
        const auto draw = [&](const std::wstring& value, IDWriteTextFormat* format,
                              ID2D1Brush* brush, D2D1_RECT_F bounds) {
            state_->render_target->DrawTextW(
                value.data(), static_cast<UINT32>(value.size()), format, bounds,
                brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        const auto draw_bouncing_number = [&](const std::wstring& value,
                                               IDWriteTextFormat* format,
                                               D2D1_RECT_F bounds,
                                               ULONGLONG bounce_started_ms) {
            constexpr float bounce_duration_ms = 420.0F;
            const float phase = bounce_started_ms == 0 ? 1.0F : std::min(
                1.0F, static_cast<float>(GetTickCount64() - bounce_started_ms) /
                          bounce_duration_ms);
            const float wave = std::sin(phase * 7.85398163F) *
                               std::exp(-2.8F * phase);
            const float bounce_scale = 1.0F + 0.14F * wave;
            const float center_x = (bounds.left + bounds.right) * 0.5F;
            const float center_y = (bounds.top + bounds.bottom) * 0.5F;
            const auto bounce_transform = D2D1::Matrix3x2F::Scale(
                bounce_scale, bounce_scale, D2D1::Point2F(center_x, center_y)) *
                D2D1::Matrix3x2F::Translation(0.0F, -4.0F * wave) *
                base_transform;
            state_->render_target->SetTransform(bounce_transform);
            const float accent_mix = std::clamp(std::fabs(wave) * 0.7F, 0.0F, 0.55F);
            state_->foreground->SetColor(D2D1::ColorF(
                0.96F, 0.98F - 0.42F * accent_mix,
                1.0F - 0.78F * accent_mix, 1.0F));
            draw(value, format, state_->foreground.Get(), bounds);
            state_->foreground->SetColor(D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F));
            state_->render_target->SetTransform(base_transform);
        };
        const auto draw_selective_percent = [&](double value, float x, float y,
                                                 const std::array<bool, 3>& changed,
                                                 ULONGLONG started_ms,
                                                 float percent_offset = 26.0F) {
            std::wstring digits = std::to_wstring(
                std::clamp(static_cast<int>(std::lround(value)), 0, 100));
            digits = std::wstring(3 - std::min<std::size_t>(3, digits.size()), L' ') +
                     digits;
            constexpr float advance = 6.2F;
            for (std::size_t index = 0; index < digits.size(); ++index) {
                if (digits[index] == L' ') continue;
                const std::wstring glyph(1, digits[index]);
                const auto bounds = D2D1::RectF(
                    x + static_cast<float>(index) * advance, y,
                    x + static_cast<float>(index + 1) * advance, y + 19.0F);
                if (changed[index]) {
                    draw_bouncing_number(glyph, state_->system_value_format.Get(), bounds,
                                         started_ms);
                } else {
                    draw(glyph, state_->system_value_format.Get(), state_->foreground.Get(),
                         bounds);
                }
            }
            draw(L"%", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(x + percent_offset, y + 1.0F,
                             x + percent_offset + 8.0F, y + 18.0F));
        };
        const auto draw_trend_number = [&](const std::wstring& value,
                                           IDWriteTextFormat* format,
                                           D2D1_RECT_F bounds,
                                           ULONGLONG trend_started_ms,
                                           int trend_direction,
                                           float trend_intensity,
                                           float arrow_x,
                                           float strength,
                                           float tug_offset = 0.0F,
                                           ULONGLONG bounce_started_ms = 0,
                                           float bounce_strength = 0.0F) {
            constexpr float trend_duration_ms = 900.0F;
            const float phase = trend_started_ms == 0 ? 1.0F :
                std::min(1.0F, static_cast<float>(GetTickCount64() -
                    trend_started_ms) / trend_duration_ms);
            const float intensity = trend_intensity;
            const bool falling = trend_direction < 0;
            const float motion = std::sin(phase * 3.14159265F) * intensity;
            const float offset_y = (falling ? 9.0F : -6.0F) * motion * strength +
                                   tug_offset;
            const float bounce_phase = bounce_started_ms == 0 ? 1.0F : std::min(
                1.0F, static_cast<float>(GetTickCount64() - bounce_started_ms) /
                          360.0F);
            const float bounce = std::sin(bounce_phase * 6.28318531F) *
                                 std::exp(-3.2F * bounce_phase) * bounce_strength;
            const float scale = 1.0F + (falling ? 0.08F : 0.05F) *
                                          motion * strength + 0.045F * bounce;
            const float center_x = (bounds.left + bounds.right) * 0.5F;
            const float center_y = (bounds.top + bounds.bottom) * 0.5F;
            state_->render_target->SetTransform(
                D2D1::Matrix3x2F::Scale(scale, scale,
                    D2D1::Point2F(center_x, center_y)) *
                D2D1::Matrix3x2F::Translation(0.0F, offset_y) * base_transform);

            D2D1_COLOR_F color = D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F);
            if (trend_direction != 0 && phase < 1.0F) {
                const float eased_phase = phase * phase * (3.0F - 2.0F * phase);
                const float mix = intensity * (1.0F - eased_phase);
                const D2D1_COLOR_F reaction = falling
                    ? D2D1::ColorF(1.0F, 0.18F, 0.12F, 1.0F)
                    : D2D1::ColorF(0.24F, 0.96F, 0.48F, 1.0F);
                color = D2D1::ColorF(
                    0.96F + (reaction.r - 0.96F) * mix,
                    0.98F + (reaction.g - 0.98F) * mix,
                    1.0F + (reaction.b - 1.0F) * mix, 1.0F);
            }
            state_->foreground->SetColor(color);
            draw(value, format, state_->foreground.Get(), bounds);
            state_->foreground->SetColor(D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F));
            state_->render_target->SetTransform(base_transform);

            if (trend_direction != 0 && phase < 1.0F) {
                const float arrow_motion = falling
                    ? std::min(1.0F, phase * 4.5F)
                    : phase * phase * (3.0F - 2.0F * phase);
                // Keep the complete arrow in the narrow strip below the value.
                // The previous upward arrow extended back into the digits.
                const float tip_y = falling
                    ? 65.0F - arrow_motion
                    : 58.0F + arrow_motion;
                const float tail_y = falling ? tip_y - 7.0F : tip_y + 7.0F;
                state_->accent->SetColor(falling
                    ? D2D1::ColorF(1.0F, 0.10F, 0.06F, 1.0F)
                    : D2D1::ColorF(0.22F, 0.95F, 0.46F, 1.0F));
                state_->accent->SetOpacity(std::clamp(
                    (1.0F - phase) * (0.45F + 0.55F * intensity), 0.0F, 1.0F));
                state_->render_target->DrawLine(D2D1::Point2F(arrow_x, tail_y),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state_->accent.Get(), 2.0F);
                state_->render_target->DrawLine(D2D1::Point2F(arrow_x - 3.5F, tip_y +
                                                     (falling ? -3.5F : 3.5F)),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state_->accent.Get(), 2.0F);
                state_->render_target->DrawLine(D2D1::Point2F(arrow_x + 3.5F, tip_y +
                                                     (falling ? -3.5F : 3.5F)),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state_->accent.Get(), 2.0F);
                state_->accent->SetOpacity(1.0F);
                state_->accent->SetColor(D2D1::ColorF(0.92F, 0.12F, 0.08F, 0.95F));
            }
        };
        const auto number = [](double value) {
            std::wostringstream stream;
            stream << std::fixed << std::setprecision(0) << value;
            return stream.str();
        };
        if (state_->target.show_fps) {
            draw(L"LIVE FPS", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(12, 14, 116, 30));
            draw_trend_number(number(state_->displayed_fps), state_->value_format.Get(),
                              D2D1::RectF(11, 28, 79, 66),
                              state_->fps_trend_started_ms,
                              state_->fps_trend_direction,
                              state_->fps_trend_intensity, 45.0F, 1.0F, 0.0F,
                              state_->fps_bounce_started_ms,
                              state_->fps_bounce_strength);
        }
        if (state_->target.show_cpu) {
            draw(L"CPU", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(79, 17, 104, 34));
            draw_selective_percent(state_->displayed_cpu_percent, 91, 16,
                                   state_->cpu_changed_digits,
                                   state_->cpu_bounce_started_ms, 31.0F);
        }
        state_->render_target->DrawLine(D2D1::Point2F(80, 38),
                                        D2D1::Point2F(128, 38),
                                        state_->border.Get(), 0.8F);
        if (state_->target.show_gpu) {
            draw(L"GPU", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(79, 43, 104, 60));
            draw_selective_percent(state_->displayed_gpu_percent, 96, 42,
                                   state_->gpu_changed_digits,
                                   state_->gpu_bounce_started_ms);
        }
        if (state_->target.show_fps) {
            draw(L"AVG", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(146, 12, 195, 28));
            detail::draw_mood_character(*state_, base_transform, linear, 200.0F, 20.0F, state_->average_mood,
                      state_->average_mood_reaction_ms,
                      state_->average_mood_reaction,
                      state_->average_tug_offset,
                      state_->average_tug_load);
            draw_trend_number(number(state_->displayed_average_fps),
                              state_->summary_format.Get(),
                              D2D1::RectF(146, 29, 193, 59),
                              state_->average_trend_started_ms,
                              state_->average_trend_direction,
                              state_->average_trend_intensity, 169.0F, 0.72F,
                              state_->average_tug_offset,
                              state_->average_bounce_started_ms,
                              state_->average_bounce_strength);
            draw(L"1% LOW", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(231, 12, 287, 28));
            detail::draw_mood_character(*state_, base_transform, linear, 292.0F, 20.0F, state_->low_mood,
                      state_->low_mood_reaction_ms,
                      state_->low_mood_reaction,
                      state_->low_tug_offset,
                      state_->low_tug_load);
            draw_trend_number(number(state_->displayed_one_percent_low_fps),
                              state_->summary_format.Get(),
                              D2D1::RectF(231, 29, 278, 59),
                              state_->low_trend_started_ms,
                              state_->low_trend_direction,
                              state_->low_trend_intensity, 254.0F, 0.72F,
                              state_->low_tug_offset,
                              state_->low_bounce_started_ms,
                              state_->low_bounce_strength);
        }
        state_->render_target->DrawLine(D2D1::Point2F(12, 70),
                                        D2D1::Point2F(310, 70),
                                        state_->border.Get(), 1.0F);
        if (state_->target.show_frame_time) {
            draw(L"FRAME TIME", state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(12, 77, 91, 94));
            draw_bouncing_number(number(state_->displayed_frame_time_ms) + L" ms",
                                 state_->metric_format.Get(),
                                 D2D1::RectF(92, 75, 139, 99),
                                 state_->frame_time_bounce_started_ms);
        }
        if (state_->target.show_memory) {
            std::wostringstream memory;
            memory << std::fixed << std::setprecision(1)
                   << L"RAM " << state_->displayed_process_ram_gib
                   << L"G  VRAM " << state_->displayed_dedicated_vram_gib << L"G";
            draw(memory.str(), state_->title_format.Get(), state_->muted.Get(),
                 D2D1::RectF(140, 76, 310, 96));
        }
        const D2D1_RECT_F graph_bounds = state_->target.show_memory
            ? D2D1::RectF(140, 95, 298, 102)
            : D2D1::RectF(140, 76, 298, 100);
        if (state_->target.show_frame_time) {
        state_->render_target->FillRoundedRectangle(
            D2D1::RoundedRect(graph_bounds, 4.0F, 4.0F), state_->metric_panel.Get());
        if (state_->frame_time_history_count > 1) {
            float graph_max = 16.7F;
            for (std::size_t i = 0; i < state_->frame_time_history_count; ++i) {
                graph_max = std::max(graph_max, state_->frame_time_history[i]);
            }
            graph_max *= 1.15F;
            const auto history_at = [&](std::size_t position) {
                const std::size_t oldest =
                    (state_->frame_time_history_next + state_->frame_time_history.size() -
                     state_->frame_time_history_count) % state_->frame_time_history.size();
                return state_->frame_time_history[
                    (oldest + position) % state_->frame_time_history.size()];
            };
            D2D1_POINT_2F previous{};
            for (std::size_t i = 0; i < state_->frame_time_history_count; ++i) {
                const float x = graph_bounds.left + 3.0F +
                    (graph_bounds.right - graph_bounds.left - 6.0F) *
                    static_cast<float>(i) /
                    static_cast<float>(state_->frame_time_history_count - 1);
                const float normalized = std::clamp(history_at(i) / graph_max, 0.0F, 1.0F);
                const float y = graph_bounds.bottom - 3.0F -
                    normalized * (graph_bounds.bottom - graph_bounds.top - 6.0F);
                const D2D1_POINT_2F point = D2D1::Point2F(x, y);
                if (i != 0) {
                    state_->render_target->DrawLine(previous, point,
                                                     state_->graph_line.Get(), 1.35F);
                }
                previous = point;
            }
        }
        }
        state_->render_target->SetTransform(D2D1::Matrix3x2F::Identity());
        result = state_->render_target->EndDraw();
    }
    if (FAILED(result)) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Overlay rendering failed",
         static_cast<std::uint32_t>(result)});
    POINT destination{animated_bounds.left, animated_bounds.top};
    POINT source{};
    SIZE size{width, height};
    const BYTE alpha = static_cast<BYTE>(std::clamp(
        std::lround(state_->opacity * 255.0F), 0L, 255L));
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(state_->window, nullptr, &destination, &size,
                             state_->memory_dc, &source, 0, &blend, ULW_ALPHA)) {
        const DWORD first_error = GetLastError();
        if (IsWindow(state_->window)) DestroyWindow(state_->window);
        state_->window = detail::create_overlay_native_window(GetModuleHandleW(nullptr));
        if (!state_->window ||
            !UpdateLayeredWindow(state_->window, nullptr, &destination, &size,
                                 state_->memory_dc, &source, 0, &blend, ULW_ALPHA)) {
            const DWORD recovery_error = GetLastError();
            std::wostringstream message;
            message << L"Overlay frame cannot be presented after recovery"
                    << L" (initial Windows error " << first_error << L")";
            return Result<bool>::failure(
                {ErrorCode::platform_failure, message.str(), recovery_error});
        }
    }
    if (!SetWindowPos(state_->window, HWND_TOPMOST, animated_bounds.left,
                      animated_bounds.top, width, height,
                      SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure, L"Overlay cannot be placed above the game",
             GetLastError()});
    }
    ++state_->renders;
    return Result<bool>::success(true);
}

}  // namespace kf2::overlay