#include "application_runtime.hpp"

namespace kf2::app {

void UiRuntime::update_animation_cadence() {
    if (!window) return;
    const bool animate = controller.theme().animations_enabled;
    if (animate && !high_resolution_animation_timer &&
        timeBeginPeriod(1) == TIMERR_NOERROR) {
        high_resolution_animation_timer = true;
    } else if (!animate && high_resolution_animation_timer) {
        timeEndPeriod(1);
        high_resolution_animation_timer = false;
    }
    SetTimer(static_cast<HWND>(window->native_handle_for_testing()), 1,
             animate ? 8U : 120U, nullptr);
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
    // High-contrast mode uses a low-frequency maintenance tick and does not
    // request the global 1 ms multimedia timer resolution.
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
    if (window) window->invalidate();
}

void UiRuntime::paint(const ui::ShellLayoutResult& layout) {
    if (!renderer || !window) return;
    RECT area{};
    const auto hwnd = static_cast<HWND>(window->native_handle_for_testing());
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
