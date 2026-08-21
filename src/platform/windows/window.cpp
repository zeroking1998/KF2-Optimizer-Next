#include "kf2/platform/windows/window.hpp"

#include <Windows.h>
#include <powrprof.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace kf2::platform::windows {
namespace {

constexpr wchar_t kWindowClassName[] = L"KF2OptimizerNextMainWindow";
constexpr int kOverlayHotkeyId = 0x4B46;

void enable_dark_title_bar(HWND window) noexcept {
    HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (module == nullptr) return;
    using SetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const auto set_attribute = reinterpret_cast<SetWindowAttribute>(
        GetProcAddress(module, "DwmSetWindowAttribute"));
    if (set_attribute != nullptr) {
        const BOOL enabled = TRUE;
        constexpr DWORD immersive_dark_mode = 20;
        constexpr DWORD immersive_dark_mode_legacy = 19;
        if (FAILED(set_attribute(window, immersive_dark_mode, &enabled,
                                 sizeof(enabled)))) {
            static_cast<void>(set_attribute(
                window, immersive_dark_mode_legacy, &enabled,
                sizeof(enabled)));
        }
    }
    FreeLibrary(module);
}

struct WindowState {
    WindowEventSink* sink{nullptr};
    bool renderer_owns_background{false};
    float dpi{96.0F};
    bool global_f10_hotkey{false};
    bool tracking_mouse{false};
};

WindowSize client_size(HWND window, float dpi) {
    RECT rectangle{};
    if (GetClientRect(window, &rectangle) == FALSE || dpi <= 0.0F) {
        return {};
    }
    return {static_cast<float>(rectangle.right - rectangle.left) * 96.0F / dpi,
            static_cast<float>(rectangle.bottom - rectangle.top) * 96.0F / dpi};
}

ThemeChangedEvent system_theme() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high_contrast =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) !=
            FALSE &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    ANIMATIONINFO animation{sizeof(animation)};
    const bool animations =
        SystemParametersInfoW(SPI_GETANIMATION, sizeof(animation), &animation, 0) !=
            FALSE &&
        animation.iMinAnimate != 0;
    const COLORREF background = GetSysColor(COLOR_WINDOW);
    const unsigned brightness = GetRValue(background) + GetGValue(background) +
                                GetBValue(background);
    return {high_contrast, brightness < 384U, !animations};
}

bool translate_key(WPARAM key, WindowKey& translated) {
    switch (key) {
        case VK_TAB:
            translated = (GetKeyState(VK_SHIFT) & 0x8000) != 0
                             ? WindowKey::shift_tab
                             : WindowKey::tab;
            return true;
        case VK_UP: translated = WindowKey::up; return true;
        case VK_DOWN: translated = WindowKey::down; return true;
        case VK_LEFT: translated = WindowKey::left; return true;
        case VK_RIGHT: translated = WindowKey::right; return true;
        case VK_HOME: translated = WindowKey::home; return true;
        case VK_END: translated = WindowKey::end; return true;
        case VK_RETURN: translated = WindowKey::enter; return true;
        case VK_SPACE: translated = WindowKey::space; return true;
        case VK_PRIOR: translated = WindowKey::page_up; return true;
        case VK_NEXT: translated = WindowKey::page_down; return true;
        case VK_F10: translated = WindowKey::f10; return true;
        default: return false;
    }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    }

    WindowEventSink* sink = state != nullptr ? state->sink : nullptr;
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            if (sink != nullptr) {
                sink->on_paint();
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SIZE:
            if (state != nullptr) {
                state->dpi = static_cast<float>(GetDpiForWindow(window));
                if (sink != nullptr) {
                    const float scale = 96.0F / state->dpi;
                    sink->on_resize(
                        {static_cast<float>(LOWORD(lparam)) * scale,
                         static_cast<float>(HIWORD(lparam)) * scale});
                }
            }
            return 0;
        case WM_DPICHANGED:
            if (state != nullptr) {
                state->dpi = static_cast<float>(HIWORD(wparam));
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                if (suggested != nullptr) {
                    SetWindowPos(window, nullptr, suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOACTIVATE | SWP_NOZORDER);
                }
                if (sink != nullptr) {
                    sink->on_dpi_changed(
                        {state->dpi, client_size(window, state->dpi)});
                }
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (sink != nullptr) {
                WindowKey key{};
                if (translate_key(wparam, key)) {
                    if (key == WindowKey::f10 && state != nullptr &&
                        state->global_f10_hotkey) {
                        return 0;
                    }
                    sink->on_key({key});
                    return 0;
                }
            }
            break;
        case WM_HOTKEY:
            if (wparam == kOverlayHotkeyId && sink != nullptr) {
                sink->on_key({WindowKey::f10});
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (sink != nullptr && state != nullptr) {
                SetCapture(window);
                const float scale = 96.0F / state->dpi;
                sink->on_pointer({PointerKind::press,
                                  {static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                                   static_cast<float>(GET_Y_LPARAM(lparam)) * scale},
                                  0});
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (sink != nullptr && state != nullptr) {
                if (GetCapture() == window) ReleaseCapture();
                const float scale = 96.0F / state->dpi;
                sink->on_pointer({PointerKind::release,
                                  {static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                                   static_cast<float>(GET_Y_LPARAM(lparam)) * scale},
                                  0});
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (sink != nullptr && state != nullptr) {
                if (!state->tracking_mouse) {
                    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
                    if (TrackMouseEvent(&tracking)) state->tracking_mouse = true;
                }
                const float scale = 96.0F / state->dpi;
                sink->on_pointer({PointerKind::move,
                                  {static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                                   static_cast<float>(GET_Y_LPARAM(lparam)) * scale},
                                  0});
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            if (state != nullptr) state->tracking_mouse = false;
            if (sink != nullptr) sink->on_pointer({PointerKind::leave, {}, 0});
            return 0;
        case WM_MOUSEWHEEL:
            if (sink != nullptr && state != nullptr) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                const float scale = 96.0F / state->dpi;
                sink->on_pointer({PointerKind::wheel,
                                  {static_cast<float>(point.x) * scale,
                                   static_cast<float>(point.y) * scale},
                                  static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))});
                return 0;
            }
            break;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE:
            if (sink != nullptr) {
                sink->on_theme_changed(system_theme());
            }
            break;
        case WM_TIMER:
            if (sink != nullptr) sink->on_timer();
            return 0;
        case WM_POWERBROADCAST:
            if ((wparam == PBT_APMRESUMEAUTOMATIC ||
                 wparam == PBT_APMRESUMESUSPEND) && sink != nullptr) {
                sink->on_system_resume();
            }
            return TRUE;
        case WM_ERASEBKGND:
            if (state != nullptr && state->renderer_owns_background) {
                return 1;
            }
            break;
        case WM_CLOSE:
            if (sink == nullptr || sink->on_close()) {
                DestroyWindow(window);
            }
            return 0;
        case WM_GETOBJECT:
            if (state != nullptr && state->sink != nullptr) {
                return state->sink->on_get_object(wparam, lparam);
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (state != nullptr && state->global_f10_hotkey) {
                UnregisterHotKey(window, kOverlayHotkeyId);
                state->global_f10_hotkey = false;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

Result<ATOM> ensure_window_class(HINSTANCE instance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;
    const ATOM atom = RegisterClassExW(&window_class);
    if (atom != 0) {
        return Result<ATOM>::success(atom);
    }
    const DWORD error = GetLastError();
    if (error == ERROR_CLASS_ALREADY_EXISTS) {
        return Result<ATOM>::success(1);
    }
    return Result<ATOM>::failure(
        {ErrorCode::platform_failure, L"Window class registration failed", error});
}

}  // namespace

Window::Window(void* native_handle, void* state) noexcept
    : native_handle_{native_handle}, state_{state} {}

Window::Window(Window&& other) noexcept
    : native_handle_{std::exchange(other.native_handle_, nullptr)},
      state_{std::exchange(other.state_, nullptr)} {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        reset();
        native_handle_ = std::exchange(other.native_handle_, nullptr);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

Window::~Window() { reset(); }

Result<Window> Window::create(const WindowOptions& options) {
    if (options.title.empty() || options.width <= 0 || options.height <= 0) {
        return Result<Window>::failure(
            {ErrorCode::invalid_argument, L"Invalid window options", 0});
    }
    static_cast<void>(SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const auto registered = ensure_window_class(instance);
    if (!registered.has_value()) {
        return Result<Window>::failure(registered.error());
    }

    auto* state = new WindowState{options.sink, options.renderer_owns_background,
                                  96, false};
    const DWORD style = WS_OVERLAPPEDWINDOW |
                        (options.visible ? static_cast<DWORD>(WS_VISIBLE) : 0U);
    HWND window = CreateWindowExW(0, kWindowClassName, options.title.c_str(), style,
                                  CW_USEDEFAULT, CW_USEDEFAULT, options.width,
                                  options.height, nullptr, nullptr, instance, state);
    if (window == nullptr) {
        const DWORD error = GetLastError();
        delete state;
        return Result<Window>::failure(
            {ErrorCode::platform_failure, L"Window creation failed", error});
    }
    enable_dark_title_bar(window);
    if (options.global_f10_hotkey) {
        if (RegisterHotKey(window, kOverlayHotkeyId, MOD_NOREPEAT, VK_F10)) {
            state->global_f10_hotkey = true;
        }
    }
    state->dpi = static_cast<float>(GetDpiForWindow(window));
    return Result<Window>::success(Window{window, state});
}

void Window::show(int command) const noexcept {
    if (native_handle_ != nullptr) {
        ShowWindow(static_cast<HWND>(native_handle_), command);
        UpdateWindow(static_cast<HWND>(native_handle_));
    }
}

void Window::invalidate() const noexcept {
    if (native_handle_ != nullptr) {
        InvalidateRect(static_cast<HWND>(native_handle_), nullptr, FALSE);
    }
}

WindowSize Window::client_size_dip() const noexcept {
    if (native_handle_ == nullptr || state_ == nullptr) {
        return {};
    }
    return client_size(static_cast<HWND>(native_handle_),
                       static_cast<WindowState*>(state_)->dpi);
}

void* Window::native_handle_for_testing() const noexcept { return native_handle_; }

Result<int> Window::run_message_loop() const {
    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        if (result == 0) {
            return Result<int>::success(static_cast<int>(message.wParam));
        }
        return Result<int>::failure(
            {ErrorCode::platform_failure, L"Windows message loop failed",
             GetLastError()});
    }
}

void Window::reset() noexcept {
    if (native_handle_ != nullptr) {
        HWND window = static_cast<HWND>(native_handle_);
        if (IsWindow(window) != FALSE) {
            DestroyWindow(window);
        }
        native_handle_ = nullptr;
    }
    delete static_cast<WindowState*>(state_);
    state_ = nullptr;
}

}  // namespace kf2::platform::windows
