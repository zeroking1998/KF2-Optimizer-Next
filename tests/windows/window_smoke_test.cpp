#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "kf2/platform/windows/window.hpp"
#include "kf2/platform/windows/window_events.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

class TestSink final : public kf2::platform::windows::WindowEventSink {
public:
    void on_paint() override { ++paints; }
    void on_resize(kf2::platform::windows::WindowSize size) override {
        ++resizes;
        last_size = size;
    }
    void on_dpi_changed(
        kf2::platform::windows::DpiChangedEvent event) override {
        ++dpi_changes;
        last_dpi = event.dpi;
    }
    void on_key(kf2::platform::windows::KeyEvent event) override {
        ++keys;
        last_key = event.key;
    }
    void on_pointer(kf2::platform::windows::PointerEvent event) override {
        ++pointers;
        pointer_kinds.push_back(event.kind);
    }
    void on_theme_changed(
        kf2::platform::windows::ThemeChangedEvent) override {
        ++theme_changes;
    }
    void on_system_resume() override { ++resumes; }
    void on_visibility_changed(bool value) override { visible = value; }
    bool on_close() override {
        ++closes;
        return true;
    }

    int paints{0};
    int resizes{0};
    int dpi_changes{0};
    int keys{0};
    int pointers{0};
    int theme_changes{0};
    int closes{0};
    int resumes{0};
    bool visible{true};
    std::vector<kf2::platform::windows::PointerKind> pointer_kinds;
    float last_dpi{0};
    kf2::platform::windows::WindowSize last_size{};
    kf2::platform::windows::WindowKey last_key{};
};

class ThrowingSink final : public kf2::platform::windows::WindowEventSink {
public:
    enum class Failure { none, paint, timer, pointer, accessibility, close, theme };

    void on_paint() override {
        ++paints;
        fail(Failure::paint);
    }
    void on_resize(kf2::platform::windows::WindowSize) override {}
    void on_dpi_changed(
        kf2::platform::windows::DpiChangedEvent) override {}
    void on_key(kf2::platform::windows::KeyEvent) override {}
    void on_pointer(kf2::platform::windows::PointerEvent) override {
        ++pointers;
        fail(Failure::pointer);
    }
    void on_theme_changed(
        kf2::platform::windows::ThemeChangedEvent) override {
        ++themes;
        fail(Failure::theme);
    }
    void on_system_resume() override {}
    bool on_close() override {
        ++closes;
        fail(Failure::close);
        return true;
    }
    LRESULT on_get_object(WPARAM, LPARAM) override {
        ++accessibility_requests;
        fail(Failure::accessibility);
        return 1;
    }
    void on_timer(UINT_PTR) override {
        ++timers;
        fail(Failure::timer);
    }

    Failure failure{Failure::none};
    int paints{0};
    int timers{0};
    int pointers{0};
    int accessibility_requests{0};
    int closes{0};
    int themes{0};

private:
    void fail(Failure point) const {
        if (failure == point) throw std::runtime_error{"injected callback failure"};
    }
};

bool take_close_request(HWND window) {
    MSG message{};
    return PeekMessageW(&message, window, WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE;
}

int main() {
    {
        TestSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer Next test",
             .width = 640,
             .height = 360,
             .visible = false,
             .sink = &sink,
             .renderer_owns_background = true});
        CHECK(created.has_value());
        CHECK(sink.theme_changes == 1);
        CHECK(!sink.visible);
        CHECK(created.value().native_handle_for_testing() != nullptr);
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        created.value().show(SW_SHOWNOACTIVATE);
        CHECK(sink.visible);
        created.value().show(SW_MINIMIZE);
        CHECK(!sink.visible);
        created.value().show(SW_SHOWNOACTIVATE);
        CHECK(sink.visible);
        created.value().show(SW_HIDE);
        CHECK(!sink.visible);
        SendMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(800, 600));
        CHECK(sink.resizes >= 1);
        CHECK(sink.last_size.width_dip == 800);
        SendMessageW(window, WM_KEYDOWN, VK_END, 0);
        CHECK(sink.keys == 1);
        CHECK(sink.last_key == kf2::platform::windows::WindowKey::end);
        SendMessageW(window, WM_SYSKEYDOWN, VK_F10, 0);
        CHECK(sink.keys == 2);
        CHECK(sink.last_key == kf2::platform::windows::WindowKey::f10);
        SendMessageW(window, WM_HOTKEY, 0x4B46, 0);
        CHECK(sink.keys == 3);
        CHECK(sink.last_key == kf2::platform::windows::WindowKey::f10);
        using kf2::platform::windows::PointerKind;
        const auto normal_drag = sink.pointer_kinds.size();
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(200, 100));
        CHECK(GetCapture() == window);
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(220, 100));
        CHECK(sink.pointer_kinds.size() == normal_drag + 3);
        CHECK(sink.pointer_kinds[normal_drag] == PointerKind::press);
        CHECK(sink.pointer_kinds[normal_drag + 1] == PointerKind::release);
        CHECK(sink.pointer_kinds[normal_drag + 2] == PointerKind::capture_lost);
        const auto interrupted_drag = sink.pointer_kinds.size();
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(200, 100));
        CHECK(GetCapture() == window);
        CHECK(ReleaseCapture());
        CHECK(sink.pointer_kinds.size() == interrupted_drag + 2);
        CHECK(sink.pointer_kinds[interrupted_drag] == PointerKind::press);
        CHECK(sink.pointer_kinds[interrupted_drag + 1] ==
              PointerKind::capture_lost);
        SendMessageW(window, WM_THEMECHANGED, 0, 0);
        CHECK(sink.theme_changes == 2);
        CHECK(SendMessageW(window, WM_POWERBROADCAST,
                           PBT_APMRESUMEAUTOMATIC, 0) == TRUE);
        CHECK(sink.resumes == 1);
        created.value().invalidate();
        SendMessageW(window, WM_PAINT, 0, 0);
        CHECK(sink.paints >= 1);
        const auto size = created.value().client_size_dip();
        CHECK(size.width_dip > 0 && size.height_dip > 0);
        SendMessageW(window, WM_CLOSE, 0, 0);
        CHECK(sink.closes == 1);
    }

    {
        ThrowingSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer paint failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(created.has_value());
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        sink.failure = ThrowingSink::Failure::paint;
        CHECK(InvalidateRect(window, nullptr, FALSE));
        SendMessageW(window, WM_PAINT, 0, 0);
        CHECK(sink.paints == 1);
        CHECK(GetUpdateRect(window, nullptr, FALSE) == FALSE);
        CHECK(take_close_request(window));
        SendMessageW(window, WM_PAINT, 0, 0);
        CHECK(sink.paints == 1);
    }

    {
        ThrowingSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer timer failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(created.has_value());
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        sink.failure = ThrowingSink::Failure::timer;
        SendMessageW(window, WM_TIMER, 1, 0);
        CHECK(sink.timers == 1);
        CHECK(take_close_request(window));
        SendMessageW(window, WM_TIMER, 1, 0);
        CHECK(sink.timers == 1);
    }

    {
        ThrowingSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer pointer failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(created.has_value());
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        sink.failure = ThrowingSink::Failure::pointer;
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        CHECK(sink.pointers == 1);
        CHECK(GetCapture() != window);
        CHECK(take_close_request(window));
    }

    {
        ThrowingSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer accessibility failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(created.has_value());
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        sink.failure = ThrowingSink::Failure::accessibility;
        SendMessageW(window, WM_GETOBJECT, 0, OBJID_CLIENT);
        CHECK(sink.accessibility_requests == 1);
        CHECK(take_close_request(window));
    }

    {
        ThrowingSink sink;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer close failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(created.has_value());
        const auto window = static_cast<HWND>(
            created.value().native_handle_for_testing());
        sink.failure = ThrowingSink::Failure::close;
        SendMessageW(window, WM_CLOSE, 0, 0);
        CHECK(sink.closes == 1);
        CHECK(IsWindow(window) == FALSE);
    }

    {
        ThrowingSink sink;
        sink.failure = ThrowingSink::Failure::theme;
        const auto created = kf2::platform::windows::Window::create(
            {.title = L"KF2 Optimizer initial theme failure test",
             .width = 320, .height = 180, .visible = false, .sink = &sink});
        CHECK(!created.has_value());
        CHECK(created.error().code == kf2::ErrorCode::internal_failure);
        CHECK(sink.themes == 1);
    }

    const auto second = kf2::platform::windows::Window::create(
        {.title = L"KF2 Optimizer Next second test",
         .width = 320,
         .height = 180,
         .visible = false});
    CHECK(second.has_value());
    return EXIT_SUCCESS;
}
