#pragma once

#include <string>

#include "kf2/core/result.hpp"
#include "kf2/platform/windows/window_events.hpp"

namespace kf2::platform::windows {

struct WindowOptions {
    std::wstring title;
    int width{960};
    int height{600};
    bool visible{true};
    WindowEventSink* sink{nullptr};
    bool renderer_owns_background{false};
    bool global_f10_hotkey{false};
};

class Window final {
public:
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    [[nodiscard]] static Result<Window> create(const WindowOptions& options);
    void show(int command) const noexcept;
    void invalidate() const noexcept;
    [[nodiscard]] WindowSize client_size_dip() const noexcept;
    [[nodiscard]] void* native_handle_for_testing() const noexcept;
    [[nodiscard]] Result<int> run_message_loop() const;

private:
    Window(void* native_handle, void* state) noexcept;
    void reset() noexcept;

    void* native_handle_{nullptr};
    void* state_{nullptr};
};

}  // namespace kf2::platform::windows
