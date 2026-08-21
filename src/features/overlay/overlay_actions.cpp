#include "features/overlay/overlay_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::overlay {
namespace {

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

app::runtime::DispatchResult toggle_metric(app::UiRuntime& runtime,
                                           bool& value) {
    value = !value;
    if (!runtime.optimizer_settings.overlay_show_fps &&
        !runtime.optimizer_settings.overlay_show_frame_time &&
        !runtime.optimizer_settings.overlay_show_cpu &&
        !runtime.optimizer_settings.overlay_show_gpu &&
        !runtime.optimizer_settings.overlay_show_memory) {
        value = true;
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"OVERLAY_METRICS_REQUIRED",
                    L"At least one overlay metric must remain enabled.");
        return app::runtime::DispatchResult::handled;
    }
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        value = !value;
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.overlay_show_fps = runtime.optimizer_settings.overlay_show_fps;
    status.overlay_show_frame_time =
        runtime.optimizer_settings.overlay_show_frame_time;
    status.overlay_show_cpu = runtime.optimizer_settings.overlay_show_cpu;
    status.overlay_show_gpu = runtime.optimizer_settings.overlay_show_gpu;
    status.overlay_show_memory = runtime.optimizer_settings.overlay_show_memory;
    runtime.model.set_status(std::move(status));
    show_notice(runtime, ui::NoticeSeverity::info,
                L"OVERLAY_METRICS_CHANGED",
                L"The selected overlay metrics were saved locally.");
    runtime.telemetry_tick();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult set_scale(app::UiRuntime& runtime,
                                       int direction, bool reset_value) {
    runtime.optimizer_settings.overlay_scale_percent = reset_value
        ? 100
        : std::clamp(
              runtime.optimizer_settings.overlay_scale_percent +
                  direction * 5,
              60, 200);
    runtime.overlay_scale = static_cast<float>(
        runtime.optimizer_settings.overlay_scale_percent) / 100.0F;
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.overlay_scale_percent =
        runtime.optimizer_settings.overlay_scale_percent;
    runtime.model.set_status(std::move(status));
    show_notice(runtime, ui::NoticeSeverity::info,
                L"OVERLAY_SCALE_CHANGED",
                L"Overlay size: " +
                    std::to_wstring(
                        runtime.optimizer_settings.overlay_scale_percent) +
                    L"%");
    runtime.telemetry_tick();
    return app::runtime::DispatchResult::handled;
}

}  // namespace

app::runtime::DispatchResult position(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    switch (runtime.overlay_corner) {
        case ::kf2::overlay::OverlayCorner::top_left:
            runtime.overlay_corner = ::kf2::overlay::OverlayCorner::top_right;
            break;
        case ::kf2::overlay::OverlayCorner::top_right:
            runtime.overlay_corner = ::kf2::overlay::OverlayCorner::bottom_right;
            break;
        case ::kf2::overlay::OverlayCorner::bottom_right:
            runtime.overlay_corner = ::kf2::overlay::OverlayCorner::bottom_left;
            break;
        case ::kf2::overlay::OverlayCorner::bottom_left:
            runtime.overlay_corner = ::kf2::overlay::OverlayCorner::top_left;
            break;
    }
    switch (runtime.overlay_corner) {
        case ::kf2::overlay::OverlayCorner::top_left:
            runtime.optimizer_settings.overlay_position = "top_left";
            break;
        case ::kf2::overlay::OverlayCorner::top_right:
            runtime.optimizer_settings.overlay_position = "top_right";
            break;
        case ::kf2::overlay::OverlayCorner::bottom_left:
            runtime.optimizer_settings.overlay_position = "bottom_left";
            break;
        case ::kf2::overlay::OverlayCorner::bottom_right:
            runtime.optimizer_settings.overlay_position = "bottom_right";
            break;
    }
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path,
        config::serialize_settings(runtime.optimizer_settings));
    if (!saved.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"SETTINGS_SAVE_FAILED", saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    auto status = runtime.model.status();
    status.overlay_position =
        runtime.optimizer_settings.overlay_position == "top_left"
            ? L"top left"
            : runtime.optimizer_settings.overlay_position == "bottom_left"
                ? L"bottom left"
                : runtime.optimizer_settings.overlay_position == "bottom_right"
                    ? L"bottom right" : L"top right";
    runtime.model.set_status(std::move(status));
    show_notice(runtime, ui::NoticeSeverity::info,
                L"OVERLAY_POSITION_CHANGED", L"Overlay position changed");
    runtime.telemetry_tick();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult scale_down(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return set_scale(runtime, -1, false);
}

app::runtime::DispatchResult scale_reset(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return set_scale(runtime, 0, true);
}

app::runtime::DispatchResult scale_up(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return set_scale(runtime, 1, false);
}

app::runtime::DispatchResult show_cpu(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return toggle_metric(runtime, runtime.optimizer_settings.overlay_show_cpu);
}

app::runtime::DispatchResult show_fps(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return toggle_metric(runtime, runtime.optimizer_settings.overlay_show_fps);
}

app::runtime::DispatchResult show_frame_time(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return toggle_metric(
        runtime, runtime.optimizer_settings.overlay_show_frame_time);
}

app::runtime::DispatchResult show_gpu(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return toggle_metric(runtime, runtime.optimizer_settings.overlay_show_gpu);
}

app::runtime::DispatchResult show_memory(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return toggle_metric(
        runtime, runtime.optimizer_settings.overlay_show_memory);
}

app::runtime::DispatchResult toggle(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto result = runtime.toggle_overlay();
    show_notice(runtime,
                result.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::error,
                result.has_value() ? L"OVERLAY_TOGGLED" : L"OVERLAY_FAILED",
                result.has_value()
                    ? (runtime.overlay_enabled ? L"Overlay enabled"
                                               : L"Overlay disabled")
                    : result.error().message);
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::overlay
