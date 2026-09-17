#include "kf2/overlay/overlay_window.hpp"
#include "overlay_window_internal.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::overlay {

Result<bool> OverlayWindow::update(const OverlayPresentation& presentation) {
    if (!state_) return Result<bool>::failure(
        {ErrorCode::internal_failure, L"Overlay state is unavailable", 0});
    const ULONGLONG frame_now_ms = GetTickCount64();
    bool window_recreated = false;
    if (!IsWindow(state_->window)) {
        state_->window = detail::create_overlay_native_window(GetModuleHandleW(nullptr));
        if (!state_->window) return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Overlay window could not be recovered", GetLastError()});
        window_recreated = true;
    }
    const HWND verified_target = IsWindow(presentation.target_window)
        ? presentation.target_window : nullptr;
    const bool owner_changed =
        GetWindow(state_->window, GW_OWNER) != verified_target;
    const DWORD owner_error = detail::bind_overlay_target_window(
        state_->window, presentation.target_window);
    if (owner_error != ERROR_SUCCESS) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Overlay cannot bind to the current KF2 window", owner_error});
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
        detail::disable_overlay_animations(*state_);
    }
    const bool geometry_changed = !state_->has_target ||
        state_->target.visible != presentation.visible ||
        (presentation.visible &&
         !detail::same_rect(state_->target.bounds, presentation.bounds));
    const bool content_changed = !state_->has_target ||
        state_->target.show_fps != presentation.show_fps ||
        state_->target.show_frame_time != presentation.show_frame_time ||
        state_->target.show_cpu != presentation.show_cpu ||
        state_->target.show_gpu != presentation.show_gpu ||
        state_->target.show_memory != presentation.show_memory ||
        state_->target.animations_enabled != presentation.animations_enabled;
    detail::prepare_visibility_animation(
        *state_, presentation, geometry_changed, frame_now_ms);
    const auto metric_update = detail::update_overlay_metrics(
        *state_, presentation, frame_now_ms, content_changed);
    if (!state_->has_visual) {
        ShowWindow(state_->window, SW_HIDE);
        state_->animating = false;
        return Result<bool>::success(geometry_changed);
    }
    const ULONGLONG update_now_ms = frame_now_ms;
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
    const bool mascot_idle_animating = presentation.animations_enabled &&
        state_->has_target && state_->target.visible;
    const bool metric_reaction_animating =
        detail::advance_metric_reaction_animation(
            *state_, presentation.animations_enabled, update_now_ms);
    const bool presentation_changed = window_recreated || owner_changed ||
        geometry_changed || content_changed ||
        metric_update.any_metric_changed || metric_update.graph_sampled;
    const bool active_animation = state_->animating || number_bounce_animating ||
        metric_reaction_animating;
    if (!presentation_changed && !active_animation && !mascot_idle_animating) {
        return Result<bool>::success(false);
    }
    // New information is always presented immediately. Between data updates,
    // cap active transitions near the display rate and the subtle mascot idle
    // motion near 20 FPS. This matches the 15 ms application cadence without
    // forcing duplicate full Direct2D layered-window uploads.
    if (!presentation_changed && state_->last_rendered_ms != 0 &&
        update_now_ms >= state_->last_rendered_ms) {
        const ULONGLONG minimum_interval_ms = active_animation ? 15 : 50;
        if (update_now_ms - state_->last_rendered_ms < minimum_interval_ms) {
            return Result<bool>::success(false);
        }
    }


    const RECT animated_bounds = detail::advance_visibility_animation(
        *state_, presentation, frame_now_ms);
    const float linear = state_->animating
        ? std::min(1.0F, static_cast<float>(
              frame_now_ms - state_->animation_started_ms) /
              (state_->visibility_animation ? 720.0F : 600.0F))
        : 1.0F;

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
    const D2D1_RECT_F graph_bounds = state_->target.show_memory
        ? D2D1::RectF(140, 95, 298, 102)
        : D2D1::RectF(140, 76, 298, 100);
    if (state_->target.show_frame_time &&
        state_->frame_time_history_count > 1 &&
        (!state_->frame_time_graph_geometry ||
         state_->frame_time_graph_source_sample_ms !=
             state_->frame_time_history_sample_ms ||
         state_->frame_time_graph_uses_memory_layout !=
             state_->target.show_memory)) {
        const HRESULT graph_result =
            detail::rebuild_frame_time_graph(*state_, graph_bounds);
        if (FAILED(graph_result)) {
            return Result<bool>::failure(
                {ErrorCode::platform_failure,
                 L"Overlay frame-time graph cannot be prepared",
                 static_cast<std::uint32_t>(graph_result)});
        }
    }
    const RECT& static_bounds = presentation.visible
        ? presentation.bounds : state_->visual.bounds;
    const LONG static_width = static_bounds.right - static_bounds.left;
    const LONG static_height = static_bounds.bottom - static_bounds.top;
    if (static_width <= 0 || static_height <= 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Overlay static layer dimensions are invalid", 0});
    }
    RECT local{0, 0, width, height};
    HRESULT result = state_->render_target->BindDC(state_->memory_dc, &local);
    if (SUCCEEDED(result) &&
        !detail::static_layer_matches(*state_, static_width, static_height)) {
        result = detail::rebuild_static_layer(
            *state_, static_width, static_height);
    }
    if (SUCCEEDED(result)) {
        state_->render_target->BeginDraw();
        state_->render_target->Clear(D2D1::ColorF(0, 0.0F));
        state_->render_target->SetTransform(D2D1::Matrix3x2F::Identity());
        state_->render_target->DrawBitmap(
            state_->static_layer_bitmap.Get(),
            D2D1::RectF(0.0F, 0.0F, static_cast<float>(width),
                        static_cast<float>(height)),
            1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        detail::draw_overlay_metrics(
            *state_, width, height, frame_now_ms, linear);
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
        const DWORD recovery_owner_error = detail::bind_overlay_target_window(
            state_->window, presentation.target_window);
        if (!state_->window || recovery_owner_error != ERROR_SUCCESS ||
            !UpdateLayeredWindow(state_->window, nullptr, &destination, &size,
                                 state_->memory_dc, &source, 0, &blend, ULW_ALPHA)) {
            const DWORD recovery_error = recovery_owner_error != ERROR_SUCCESS
                ? recovery_owner_error : GetLastError();
            return Result<bool>::failure(
                {ErrorCode::platform_failure,
                 L"Overlay frame cannot be presented after recovery"
                 L" (initial Windows error " + std::to_wstring(first_error) +
                     L")",
                 recovery_error});
        }
    }
    if (!SetWindowPos(state_->window, HWND_TOPMOST, animated_bounds.left,
                      animated_bounds.top, width, height,
                      SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure, L"Overlay cannot be placed above the game",
             GetLastError()});
    }
    state_->last_rendered_ms = update_now_ms;
    ++state_->renders;
    return Result<bool>::success(true);
}

}  // namespace kf2::overlay
