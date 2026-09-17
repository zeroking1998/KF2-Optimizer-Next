#include "overlay_window_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace kf2::overlay::detail {

namespace {

constexpr float kLogicalCanvasWidth = 330.0F;
constexpr float kLogicalHeight = 105.0F;

}  // namespace

HRESULT rebuild_frame_time_graph(
    OverlayWindowState& state,
    D2D1_RECT_F bounds) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    HRESULT result = state.d2d_factory->CreatePathGeometry(&geometry);
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(result)) result = geometry->Open(&sink);
    if (FAILED(result)) return result;

    float graph_max = 16.7F;
    for (std::size_t index = 0; index < state.frame_time_history_count; ++index) {
        graph_max = std::max(graph_max, state.frame_time_history[index]);
    }
    graph_max *= 1.15F;
    const std::size_t oldest =
        (state.frame_time_history_next + state.frame_time_history.size() -
         state.frame_time_history_count) % state.frame_time_history.size();
    const auto point_at = [&](std::size_t position) {
        const float sample = state.frame_time_history[
            (oldest + position) % state.frame_time_history.size()];
        const float x = bounds.left + 3.0F +
            (bounds.right - bounds.left - 6.0F) *
            static_cast<float>(position) /
            static_cast<float>(state.frame_time_history_count - 1);
        const float normalized = std::clamp(sample / graph_max, 0.0F, 1.0F);
        const float y = bounds.bottom - 3.0F - normalized *
            (bounds.bottom - bounds.top - 6.0F);
        return D2D1::Point2F(x, y);
    };

    sink->BeginFigure(point_at(0), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t index = 1; index < state.frame_time_history_count; ++index) {
        sink->AddLine(point_at(index));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    result = sink->Close();
    if (FAILED(result)) return result;
    state.frame_time_graph_geometry = std::move(geometry);
    state.frame_time_graph_source_sample_ms =
        state.frame_time_history_sample_ms;
    state.frame_time_graph_uses_memory_layout = state.target.show_memory;
    ++state.graph_geometry_builds;
    return S_OK;
}

const std::wstring& rounded_metric_text(
    double value,
    long& cached_value,
    std::wstring& cached_text) {
    const long rounded = std::lround(value);
    if (rounded != cached_value) {
        cached_value = rounded;
        cached_text = std::to_wstring(rounded);
    }
    return cached_text;
}

void refresh_frame_time_text(OverlayWindowState& state) {
    const long rounded_frame_time = std::lround(state.displayed_frame_time_ms);
    if (rounded_frame_time == state.displayed_frame_time_text_value) return;
    state.displayed_frame_time_text_value = rounded_frame_time;
    state.displayed_frame_time_text =
        std::to_wstring(rounded_frame_time) + L" ms";
}

void refresh_memory_text(OverlayWindowState& state) {
    const long ram_tenths = std::lround(state.displayed_process_ram_gib * 10.0);
    const long vram_tenths = std::lround(
        state.displayed_dedicated_vram_gib * 10.0);
    if (ram_tenths == state.displayed_ram_tenths &&
        vram_tenths == state.displayed_vram_tenths) {
        return;
    }
    const auto fixed_tenth = [](long tenths) {
        const bool negative = tenths < 0;
        const unsigned long magnitude = static_cast<unsigned long>(
            negative ? -tenths : tenths);
        return std::wstring(negative ? L"-" : L"") +
            std::to_wstring(magnitude / 10) + L"." +
            std::to_wstring(magnitude % 10);
    };
    state.displayed_ram_tenths = ram_tenths;
    state.displayed_vram_tenths = vram_tenths;
    state.displayed_memory_text =
        L"RAM " + fixed_tenth(ram_tenths) + L"G  VRAM " +
        fixed_tenth(vram_tenths) + L"G";
}

MetricUpdateResult update_overlay_metrics(
    OverlayWindowState& state,
    const OverlayPresentation& presentation,
    ULONGLONG frame_now_ms,
    bool content_changed) {
    const OverlayPresentation previous_target = state.target;
    const bool fps_changed = !state.metrics_initialized ||
        std::fabs(state.target.fps - presentation.fps) >= 1.0;
    const bool average_changed = !state.metrics_initialized ||
        std::fabs(state.target.average_fps - presentation.average_fps) >= 1.0;
    const bool low_changed = !state.metrics_initialized ||
        std::fabs(state.target.one_percent_low_fps -
                  presentation.one_percent_low_fps) >= 1.0;
    const bool frame_time_changed = !state.metrics_initialized ||
        std::fabs(state.target.frame_time_ms - presentation.frame_time_ms) >= 0.3;
    const bool system_metrics_due = !state.metrics_initialized ||
        state.system_metrics_sample_ms == 0 ||
        frame_now_ms - state.system_metrics_sample_ms >= 1000;
    const bool cpu_changed = system_metrics_due &&
        (!state.metrics_initialized ||
         std::fabs(state.target.cpu_percent - presentation.cpu_percent) >= 1.0);
    const bool gpu_changed = system_metrics_due &&
        (!state.metrics_initialized ||
         std::fabs(state.target.gpu_percent - presentation.gpu_percent) >= 1.0);
    const bool ram_changed = system_metrics_due && presentation.show_memory &&
        (!state.metrics_initialized ||
         std::fabs(state.target.process_ram_gib - presentation.process_ram_gib) >= 0.05);
    const bool vram_changed = system_metrics_due && presentation.show_memory &&
        (!state.metrics_initialized ||
         std::fabs(state.target.dedicated_vram_gib -
                   presentation.dedicated_vram_gib) >= 0.05);
    const bool any_metric_changed = fps_changed || average_changed || low_changed ||
        frame_time_changed || cpu_changed || gpu_changed || ram_changed || vram_changed;
    if (cpu_changed || gpu_changed || ram_changed || vram_changed) {
        state.system_metrics_sample_ms = frame_now_ms;
    }
    bool graph_sampled = false;
    if (presentation.visible && presentation.frame_time_ms > 0.0 &&
        (state.frame_time_history_sample_ms == 0 ||
         frame_now_ms - state.frame_time_history_sample_ms >= 100)) {
        state.frame_time_history[state.frame_time_history_next] =
            static_cast<float>(presentation.frame_time_ms);
        state.frame_time_history_next =
            (state.frame_time_history_next + 1) % state.frame_time_history.size();
        state.frame_time_history_count = std::min(
            state.frame_time_history_count + 1,
            state.frame_time_history.size());
        state.frame_time_history_sample_ms = frame_now_ms;
        graph_sampled = true;
    }
    if (presentation.visible && any_metric_changed) {
        const ULONGLONG now_ms = frame_now_ms;
        if (state.metrics_initialized) {
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
                update_bounce(presentation.fps, state.target.fps,
                              state.fps_bounce_started_ms,
                              state.fps_bounce_strength);
                update_trend(presentation.fps, state.target.fps,
                             4.0, 0.009, state.fps_trend_started_ms,
                             state.fps_trend_direction,
                             state.fps_trend_intensity, 35.0, 1.0F);
            }
            if (average_changed) {
                update_bounce(presentation.average_fps, state.target.average_fps,
                              state.average_bounce_started_ms,
                              state.average_bounce_strength);
                update_trend(presentation.average_fps, state.target.average_fps,
                             3.0, 0.007, state.average_trend_started_ms,
                             state.average_trend_direction,
                             state.average_trend_intensity, 30.0, 0.82F);
            }
            if (low_changed) {
                update_bounce(presentation.one_percent_low_fps,
                              state.target.one_percent_low_fps,
                              state.low_bounce_started_ms,
                              state.low_bounce_strength);
                update_trend(presentation.one_percent_low_fps,
                             state.target.one_percent_low_fps,
                             4.0, 0.012, state.low_trend_started_ms,
                             state.low_trend_direction,
                             state.low_trend_intensity, 28.0, 0.88F);
            }
            if (frame_time_changed) state.frame_time_bounce_started_ms = now_ms;
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
                state.cpu_changed_digits = changed_digits(
                    state.target.cpu_percent, presentation.cpu_percent);
                state.cpu_bounce_started_ms = now_ms;
            }
            if (gpu_changed) {
                state.gpu_changed_digits = changed_digits(
                    state.target.gpu_percent, presentation.gpu_percent);
                state.gpu_bounce_started_ms = now_ms;
            }
        }
        state.displayed_cpu_percent = presentation.cpu_percent;
        state.displayed_gpu_percent = presentation.gpu_percent;
        state.displayed_process_ram_gib = presentation.process_ram_gib;
        state.displayed_dedicated_vram_gib =
            presentation.dedicated_vram_gib;
        state.metrics_initialized = true;
    }
    // Keep frame telemetry exact on every presentation update. The surrounding
    // bounce and trend transforms provide motion without interpolating values.
    state.displayed_fps = presentation.fps;
    state.displayed_average_fps = presentation.average_fps;
    state.displayed_one_percent_low_fps = presentation.one_percent_low_fps;
    state.displayed_frame_time_ms = presentation.frame_time_ms;
    state.target = presentation;
    if (!cpu_changed) state.target.cpu_percent = previous_target.cpu_percent;
    if (!gpu_changed) state.target.gpu_percent = previous_target.gpu_percent;
    if (!ram_changed) state.target.process_ram_gib = previous_target.process_ram_gib;
    if (!vram_changed) {
        state.target.dedicated_vram_gib = previous_target.dedicated_vram_gib;
    }
    if (presentation.visible && content_changed && presentation.average_fps > 1.0) {
        if (state.normal_average_fps <= 1.0) {
            state.normal_average_fps = presentation.average_fps;
        } else {
            const double ratio = presentation.average_fps / state.normal_average_fps;
            const double learning = ratio >= 0.88 ? 0.025 : 0.004;
            state.normal_average_fps +=
                (presentation.average_fps - state.normal_average_fps) * learning;
        }
        const float average_ratio = static_cast<float>(
            presentation.average_fps / std::max(1.0, state.normal_average_fps));
        const float new_average_mood = std::clamp(
            (average_ratio - 0.84F) / 0.16F * 2.0F - 1.0F, -1.0F, 1.0F);
        const float low_ratio = static_cast<float>(
            presentation.one_percent_low_fps /
            std::max(1.0, presentation.average_fps));
        const float new_low_mood = std::clamp(
            (low_ratio - 0.58F) / 0.32F * 2.0F - 1.0F, -1.0F, 1.0F);
        const ULONGLONG mood_now = frame_now_ms;
        if (std::fabs(new_average_mood - state.average_mood_target) > 0.14F) {
            state.average_mood_reaction = std::clamp(
                std::fabs(new_average_mood - state.average_mood_target), 0.0F, 1.0F);
            state.average_mood_target = new_average_mood;
            state.average_mood_reaction_ms = mood_now;
        }
        if (std::fabs(new_low_mood - state.low_mood_target) > 0.14F) {
            state.low_mood_reaction = std::clamp(
                std::fabs(new_low_mood - state.low_mood_target), 0.0F, 1.0F);
            state.low_mood_target = new_low_mood;
            state.low_mood_reaction_ms = mood_now;
        }
    }
    state.has_target = true;
    if (presentation.visible) {
        state.visual = presentation;
        state.has_visual = true;
    }
    return {any_metric_changed, graph_sampled};
}

void draw_overlay_metrics(
    OverlayWindowState& state,
    LONG width,
    LONG height,
    ULONGLONG frame_now_ms,
    float animation_progress) {
    const float linear = animation_progress;
        const auto base_transform = D2D1::Matrix3x2F::Translation(8.0F, 0.0F) *
            D2D1::Matrix3x2F::Scale(
                static_cast<float>(width) / kLogicalCanvasWidth,
                static_cast<float>(height) / kLogicalHeight);
        state.render_target->SetTransform(base_transform);
        state.render_target->DrawLine(D2D1::Point2F(14, 9),
                                        D2D1::Point2F(
                                            72 + 22 * std::sin(
                                                linear * 3.14159265F) +
                                                5 * std::sin(
                                                linear * 9.42477796F), 9),
                                        state.accent.Get(), 3.0F);
        const auto draw = [&](std::wstring_view value, IDWriteTextFormat* format,
                              ID2D1Brush* brush, D2D1_RECT_F bounds) {
            state.render_target->DrawTextW(
                value.data(), static_cast<UINT32>(value.size()), format, bounds,
                brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        const auto draw_bouncing_number = [&](std::wstring_view value,
                                               IDWriteTextFormat* format,
                                               D2D1_RECT_F bounds,
                                               ULONGLONG bounce_started_ms) {
            constexpr float bounce_duration_ms = 420.0F;
            const float phase = bounce_started_ms == 0 ? 1.0F : std::min(
                1.0F, static_cast<float>(frame_now_ms - bounce_started_ms) /
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
            state.render_target->SetTransform(bounce_transform);
            const float accent_mix = std::clamp(std::fabs(wave) * 0.7F, 0.0F, 0.55F);
            state.foreground->SetColor(D2D1::ColorF(
                0.96F, 0.98F - 0.42F * accent_mix,
                1.0F - 0.78F * accent_mix, 1.0F));
            draw(value, format, state.foreground.Get(), bounds);
            state.foreground->SetColor(D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F));
            state.render_target->SetTransform(base_transform);
        };
        const auto draw_selective_percent = [&](double value, float x, float y,
                                                 const std::array<bool, 3>& changed,
                                                 ULONGLONG started_ms,
                                                 float percent_offset = 26.0F) {
            const int rounded = std::clamp(
                static_cast<int>(std::lround(value)), 0, 100);
            const std::array<wchar_t, 3> digits{
                rounded >= 100 ? static_cast<wchar_t>(L'0' + rounded / 100)
                               : L' ',
                rounded >= 10 ? static_cast<wchar_t>(L'0' + (rounded / 10) % 10)
                              : L' ',
                static_cast<wchar_t>(L'0' + rounded % 10)};
            constexpr float advance = 6.2F;
            for (std::size_t index = 0; index < digits.size(); ++index) {
                if (digits[index] == L' ') continue;
                const auto bounds = D2D1::RectF(
                    x + static_cast<float>(index) * advance, y,
                    x + static_cast<float>(index + 1) * advance, y + 19.0F);
                const std::wstring_view glyph{&digits[index], 1};
                if (changed[index]) {
                    draw_bouncing_number(glyph, state.system_value_format.Get(), bounds,
                                         started_ms);
                } else {
                    draw(glyph, state.system_value_format.Get(), state.foreground.Get(),
                         bounds);
                }
            }
            draw(L"%", state.title_format.Get(), state.muted.Get(),
                 D2D1::RectF(x + percent_offset, y + 1.0F,
                             x + percent_offset + 8.0F, y + 18.0F));
        };
        const auto draw_trend_number = [&](std::wstring_view value,
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
                std::min(1.0F, static_cast<float>(frame_now_ms -
                    trend_started_ms) / trend_duration_ms);
            const float intensity = trend_intensity;
            const bool falling = trend_direction < 0;
            const float motion = std::sin(phase * 3.14159265F) * intensity;
            const float offset_y = (falling ? 9.0F : -6.0F) * motion * strength +
                                   tug_offset;
            const float bounce_phase = bounce_started_ms == 0 ? 1.0F : std::min(
                1.0F, static_cast<float>(frame_now_ms - bounce_started_ms) /
                          360.0F);
            const float bounce = std::sin(bounce_phase * 6.28318531F) *
                                 std::exp(-3.2F * bounce_phase) * bounce_strength;
            const float scale = 1.0F + (falling ? 0.08F : 0.05F) *
                                          motion * strength + 0.045F * bounce;
            const float center_x = (bounds.left + bounds.right) * 0.5F;
            const float center_y = (bounds.top + bounds.bottom) * 0.5F;
            state.render_target->SetTransform(
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
            state.foreground->SetColor(color);
            draw(value, format, state.foreground.Get(), bounds);
            state.foreground->SetColor(D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F));
            state.render_target->SetTransform(base_transform);

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
                state.accent->SetColor(falling
                    ? D2D1::ColorF(1.0F, 0.10F, 0.06F, 1.0F)
                    : D2D1::ColorF(0.22F, 0.95F, 0.46F, 1.0F));
                state.accent->SetOpacity(std::clamp(
                    (1.0F - phase) * (0.45F + 0.55F * intensity), 0.0F, 1.0F));
                state.render_target->DrawLine(D2D1::Point2F(arrow_x, tail_y),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state.accent.Get(), 2.0F);
                state.render_target->DrawLine(D2D1::Point2F(arrow_x - 3.5F, tip_y +
                                                     (falling ? -3.5F : 3.5F)),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state.accent.Get(), 2.0F);
                state.render_target->DrawLine(D2D1::Point2F(arrow_x + 3.5F, tip_y +
                                                     (falling ? -3.5F : 3.5F)),
                                                 D2D1::Point2F(arrow_x, tip_y),
                                                 state.accent.Get(), 2.0F);
                state.accent->SetOpacity(1.0F);
                state.accent->SetColor(D2D1::ColorF(0.92F, 0.12F, 0.08F, 0.95F));
            }
        };
        if (state.target.show_fps) {
            draw_trend_number(rounded_metric_text(
                                  state.displayed_fps,
                                  state.displayed_fps_text_value,
                                  state.displayed_fps_text),
                              state.value_format.Get(),
                              D2D1::RectF(11, 28, 79, 66),
                              state.fps_trend_started_ms,
                              state.fps_trend_direction,
                              state.fps_trend_intensity, 45.0F, 1.0F, 0.0F,
                              state.fps_bounce_started_ms,
                              state.fps_bounce_strength);
        }
        if (state.target.show_cpu) {
            draw_selective_percent(state.displayed_cpu_percent, 91, 16,
                                   state.cpu_changed_digits,
                                   state.cpu_bounce_started_ms, 31.0F);
        }
        if (state.target.show_gpu) {
            draw_selective_percent(state.displayed_gpu_percent, 96, 42,
                                   state.gpu_changed_digits,
                                   state.gpu_bounce_started_ms);
        }
        if (state.target.show_fps) {
            draw_mood_character(state, base_transform, frame_now_ms,
                      linear, 200.0F, 20.0F, state.average_mood,
                      state.average_mood_reaction_ms,
                      state.average_mood_reaction,
                      state.average_tug_offset,
                      state.average_tug_load);
            draw_trend_number(rounded_metric_text(
                                  state.displayed_average_fps,
                                  state.displayed_average_text_value,
                                  state.displayed_average_text),
                              state.summary_format.Get(),
                              D2D1::RectF(146, 29, 193, 59),
                              state.average_trend_started_ms,
                              state.average_trend_direction,
                              state.average_trend_intensity, 169.0F, 0.72F,
                              state.average_tug_offset,
                              state.average_bounce_started_ms,
                              state.average_bounce_strength);
            draw_mood_character(state, base_transform, frame_now_ms,
                      linear, 292.0F, 20.0F, state.low_mood,
                      state.low_mood_reaction_ms,
                      state.low_mood_reaction,
                      state.low_tug_offset,
                      state.low_tug_load);
            draw_trend_number(rounded_metric_text(
                                  state.displayed_one_percent_low_fps,
                                  state.displayed_low_text_value,
                                  state.displayed_low_text),
                              state.summary_format.Get(),
                              D2D1::RectF(231, 29, 278, 59),
                              state.low_trend_started_ms,
                              state.low_trend_direction,
                              state.low_trend_intensity, 254.0F, 0.72F,
                              state.low_tug_offset,
                              state.low_bounce_started_ms,
                              state.low_bounce_strength);
        }
        if (state.target.show_frame_time) {
            refresh_frame_time_text(state);
            draw_bouncing_number(state.displayed_frame_time_text,
                                 state.metric_format.Get(),
                                 D2D1::RectF(92, 75, 139, 99),
                                 state.frame_time_bounce_started_ms);
        }
        if (state.target.show_memory) {
            refresh_memory_text(state);
            draw(state.displayed_memory_text, state.title_format.Get(),
                 state.muted.Get(),
                 D2D1::RectF(140, 76, 310, 96));
        }
        if (state.target.show_frame_time) {
            if (state.frame_time_graph_geometry) {
                state.render_target->DrawGeometry(
                    state.frame_time_graph_geometry.Get(),
                    state.graph_line.Get(), 1.35F);
            }
        }
        state.render_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

}  // namespace kf2::overlay::detail
