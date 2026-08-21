#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdlib>
#include <iostream>

#include "kf2/platform/windows/presentmon_session.hpp"
#include "kf2/overlay/overlay_policy.hpp"
#include "kf2/overlay/overlay_window.hpp"

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": check failed: " #x << '\n';                      \
            return EXIT_FAILURE;                                               \
        }                                                                      \
    } while (false)

namespace {

std::uint64_t monotonic_ns() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<std::uint64_t>(
        static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L /
        static_cast<long double>(frequency.QuadPart));
}

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

void pump_messages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int main() {
    using namespace kf2::telemetry;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = test_window_proc;
    window_class.lpszClassName = L"KF2OptimizerPresentMonDxgiTest";
    CHECK(RegisterClassW(&window_class) != 0 ||
          GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"DXGI fixture",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr,
                                  nullptr, instance, nullptr);
    CHECK(window != nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetForegroundWindow(window);

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 320;
    description.BufferDesc.Height = 240;
    description.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};
    HRESULT created = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &description, &swap_chain, &device, &level, &context);
    if (FAILED(created)) {
        created = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &description, &swap_chain, &device, &level,
            &context);
    }
    CHECK(SUCCEEDED(created));

    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 512};
    CHECK(source.start().has_value());
    auto session =
        kf2::platform::windows::PresentMonSession::start(identity, source);
    CHECK(session.has_value());
    Sleep(150);
    for (int frame = 0; frame < 90; ++frame) {
        pump_messages();
        CHECK(SUCCEEDED(swap_chain->Present(1, 0)));
    }
    pump_messages();
    Sleep(500);
    const auto metrics = source.drain(monotonic_ns(), 2'000'000'000ULL);

    CHECK(session.value()->stop().has_value());
    CHECK(source.stop().has_value());
    CHECK(metrics.fps.has_value());
    CHECK(*metrics.fps > 20.0);
    CHECK(metrics.quality == SampleQuality::good);

    kf2::game::GameWindowState game_window;
    game_window.window = window;
    game_window.visible = true;
    game_window.foreground = true;
    game_window.client_bounds = {100, 100, 1380, 820};
    game_window.reason = kf2::game::WindowUnavailableReason::none;
    auto overlay = kf2::overlay::OverlayWindow::create();
    CHECK(overlay.has_value());
    const auto shown = kf2::overlay::evaluate_overlay(
        {true, game_window, metrics, kf2::overlay::OverlayCorner::top_right,
         1.0F, {240, 90}, 12});
    CHECK(shown.visible);
    CHECK(overlay.value().update(shown).has_value());
    CHECK(IsWindowVisible(overlay.value().native_handle()));
    game_window.minimized = true;
    game_window.reason = kf2::game::WindowUnavailableReason::minimized;
    const auto hidden = kf2::overlay::evaluate_overlay(
        {true, game_window, metrics, kf2::overlay::OverlayCorner::top_right,
         1.0F, {240, 90}, 12});
    CHECK(!hidden.visible);
    CHECK(overlay.value().update(hidden).has_value());
    CHECK(IsWindowVisible(overlay.value().native_handle()));
    for (int frame = 0; frame < 52; ++frame) {
        Sleep(16);
        CHECK(overlay.value().update(hidden).has_value());
    }
    CHECK(!IsWindowVisible(overlay.value().native_handle()));

    context->Release();
    device->Release();
    swap_chain->Release();
    DestroyWindow(window);
    return EXIT_SUCCESS;
}
