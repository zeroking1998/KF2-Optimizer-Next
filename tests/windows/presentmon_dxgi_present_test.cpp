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
    HWND secondary_window = CreateWindowExW(
        0, window_class.lpszClassName, L"Secondary DXGI fixture",
        WS_OVERLAPPEDWINDOW, 340, 0, 320, 240, nullptr, nullptr, instance,
        nullptr);
    CHECK(secondary_window != nullptr);
    ShowWindow(window, SW_SHOW);
    ShowWindow(secondary_window, SW_SHOW);
    UpdateWindow(window);
    UpdateWindow(secondary_window);
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
    IDXGISwapChain* secondary_swap_chain = nullptr;
    ID3D11Device* secondary_device = nullptr;
    ID3D11DeviceContext* secondary_context = nullptr;
    D3D_FEATURE_LEVEL level{};
    // Keep this desktop-bound integration test off the user's GPU driver. A
    // paced WARP swap chain still emits the DXGI/DWM events PresentMon needs
    // without creating a burst workload that can trigger a kernel watchdog.
    const HRESULT created = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &description, &swap_chain, &device, &level,
        &context);
    CHECK(SUCCEEDED(created));

    description.OutputWindow = secondary_window;
    CHECK(SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &description, &secondary_swap_chain,
        &secondary_device, &level, &secondary_context)));

    ID3D11Texture2D* primary_buffer = nullptr;
    ID3D11RenderTargetView* primary_target = nullptr;
    CHECK(SUCCEEDED(swap_chain->GetBuffer(
        0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&primary_buffer))));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(
        primary_buffer, nullptr, &primary_target)));
    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 512};
    CHECK(source.start().has_value());
    auto session =
        kf2::platform::windows::PresentMonSession::start(identity, source);
    CHECK(session.has_value());
    Sleep(250);
    for (int frame = 0; frame < 120; ++frame) {
        pump_messages();
        const float shade = static_cast<float>(frame % 16) / 15.0F;
        const float color[4]{0.10F, shade, 0.30F, 1.0F};
        context->ClearRenderTargetView(primary_target, color);
        // WARP is not required to honor DXGI's refresh synchronization. Pace
        // the known input explicitly so its completed-display rate has an
        // independent, driver-neutral expectation.
        CHECK(SUCCEEDED(swap_chain->Present(0, 0)));
        CHECK(SUCCEEDED(secondary_swap_chain->Present(0, 0)));
        Sleep(16);
    }
    pump_messages();
    Sleep(500);
    CHECK(session.value()->stop().has_value());
    const auto metrics = source.drain(monotonic_ns(), 2'000'000'000ULL);
    CHECK(source.stop().has_value());
    if (metrics.fps) {
        std::cout << "Measured completed-present FPS: " << *metrics.fps << '\n';
        CHECK(*metrics.fps > 30.0);
        // A process can own several swap chains. Only one coherent stream is
        // allowed to contribute frames; otherwise both fixtures are mixed.
        CHECK(*metrics.fps < 90.0);
        CHECK(metrics.quality == SampleQuality::good);
    } else {
        CHECK(metrics.reason == UnavailableReason::no_samples ||
              metrics.reason == UnavailableReason::stale);
        primary_target->Release();
        primary_buffer->Release();
        context->Release();
        device->Release();
        swap_chain->Release();
        secondary_context->Release();
        secondary_device->Release();
        secondary_swap_chain->Release();
        DestroyWindow(window);
        DestroyWindow(secondary_window);
        std::cout << "PresentMon completed-present samples unavailable; "
                     "skipping desktop boundary\n";
        return 77;
    }

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

    primary_target->Release();
    primary_buffer->Release();
    context->Release();
    device->Release();
    swap_chain->Release();
    secondary_context->Release();
    secondary_device->Release();
    secondary_swap_chain->Release();
    DestroyWindow(window);
    DestroyWindow(secondary_window);
    return EXIT_SUCCESS;
}
