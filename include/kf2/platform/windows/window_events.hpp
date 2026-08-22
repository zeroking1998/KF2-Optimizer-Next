#pragma once

#include <Windows.h>

namespace kf2::platform::windows {

struct WindowSize {
    float width_dip{0};
    float height_dip{0};
};

struct WindowPoint {
    float x_dip{0};
    float y_dip{0};
};

struct DpiChangedEvent {
    float dpi{96};
    WindowSize size;
};

enum class WindowKey {
    tab,
    shift_tab,
    up,
    down,
    left,
    right,
    home,
    end,
    enter,
    space,
    page_up,
    page_down,
    f10,
};

struct KeyEvent {
    WindowKey key{WindowKey::tab};
};

enum class PointerKind { press, release, activate, move, leave, wheel };

struct PointerEvent {
    PointerKind kind{PointerKind::activate};
    WindowPoint position;
    float wheel_delta{0};
};

struct ThemeChangedEvent {
    bool high_contrast{false};
    bool dark{false};
};

class WindowEventSink {
public:
    virtual ~WindowEventSink() = default;
    virtual void on_paint() = 0;
    virtual void on_resize(WindowSize size) = 0;
    virtual void on_dpi_changed(DpiChangedEvent event) = 0;
    virtual void on_key(KeyEvent event) = 0;
    virtual void on_pointer(PointerEvent event) = 0;
    virtual void on_theme_changed(ThemeChangedEvent event) = 0;
    [[nodiscard]] virtual bool on_close() = 0;
    virtual LRESULT on_get_object(WPARAM, LPARAM) { return 0; }
    virtual void on_timer() {}
    virtual void on_system_resume() {}
};

}  // namespace kf2::platform::windows
