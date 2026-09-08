#include "application_runtime.hpp"
#include "kf2/ui/ui_cadence.hpp"

namespace kf2::app {

void UiRuntime::update_animation_cadence() {
    if (!window) return;
    const bool background_work_active = package_repair_state ||
        update_check_state || update_install_state;
    const auto interval = ui::runtime_timer_interval_ms(
        model.status().game_detected, background_work_active);
    const auto hwnd = static_cast<HWND>(window->native_handle_for_testing());
    if (interval != current_ui_timer_interval_ms &&
        SetTimer(hwnd, ui::kRuntimeTimerId, interval, nullptr) != 0) {
        current_ui_timer_interval_ms = interval;
    }
    const bool animation_active = controller.animation_active();
    if (animation_active && !animation_timer_active) {
        animation_timer_active =
            SetTimer(hwnd, ui::kAnimationTimerId,
                     ui::kAnimationTimerIntervalMs, nullptr) != 0;
    } else if (!animation_active && animation_timer_active) {
        KillTimer(hwnd, ui::kAnimationTimerId);
        animation_timer_active = false;
    }
}


Result<bool> UiRuntime::create_window(const std::wstring& title) {
    auto created = platform::windows::Window::create(
        {.title = title, .width = 1440, .height = 900, .visible = false,
         .sink = &controller, .renderer_owns_background = true,
         .global_f10_hotkey = true});
    if (!created.has_value()) return Result<bool>::failure(created.error());
    window.emplace(std::move(created.value()));
    const auto hwnd = static_cast<HWND>(window->native_handle_for_testing());
    auto overlay_created = overlay::OverlayWindow::create();
    if (overlay_created.has_value()) {
        overlay_window.emplace(std::move(overlay_created.value()));
    } else {
        events->append({0, diagnostics::Severity::warning, "OVERLAY_UNAVAILABLE",
                        overlay_created.error().message, L"overlay"});
    }
    update_animation_cadence();

    auto graphics = ui::Direct2DShellRenderer::create(hwnd);
    if (graphics.has_value()) {
        renderer.emplace(std::move(graphics.value()));
    } else {
        events->append({0, diagnostics::Severity::error, "RENDERER_DEGRADED",
                        graphics.error().message, L"renderer"});
    }
    auto accessible = ui::AutomationProvider::create(
        hwnd, model, controller.layout(),
        [this](std::string_view action) { execute_action(action); },
        [this] { invalidate(); },
        [this](std::string_view id, int value) {
            set_slider_value(id, value);
        });
    if (accessible.has_value()) {
        automation.emplace(std::move(accessible.value()));
    } else {
        events->append({0, diagnostics::Severity::warning, "UIA_DEGRADED",
                        accessible.error().message, L"accessibility"});
    }
    invalidate();
    return Result<bool>::success(true);
}

void UiRuntime::invalidate() {
    if (!callbacks_ready) return;
    controller.synchronize_model();
    if (automation) automation->update_layout(controller.layout());
    update_animation_cadence();
    if (window) window->invalidate();
}

void UiRuntime::paint(const ui::ShellLayoutResult& layout) {
    if (!renderer || !window) return;
    RECT area{};
    const auto hwnd = static_cast<HWND>(window->native_handle_for_testing());
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return;
    GetClientRect(hwnd, &area);
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
    auto resized = renderer->resize(
        {static_cast<unsigned>(std::max<LONG>(0, area.right - area.left)),
         static_cast<unsigned>(std::max<LONG>(0, area.bottom - area.top))},
        dpi);
    auto rendered = resized.has_value()
                        ? renderer->render(layout, controller.theme())
                        : std::move(resized);
    if (!rendered.has_value()) {
        events->append({0, diagnostics::Severity::error, "RENDER_FAILED",
                        rendered.error().message, L"renderer"});
        renderer.reset();
    }
}

}  // namespace kf2::app
