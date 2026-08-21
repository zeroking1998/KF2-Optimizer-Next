#include "overlay_window_internal.hpp"

#include <memory>
#include <utility>

namespace kf2::overlay {
namespace detail {

LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_SYSCOMMAND &&
        (wparam & 0xFFF0U) == SC_MINIMIZE) return 0;
    return DefWindowProcW(window, message, wparam, lparam);
}
HWND create_overlay_native_window(HINSTANCE instance) {
    SetLastError(ERROR_SUCCESS);
    return CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED |
            WS_EX_TOPMOST,
        kClassName, L"KF2 Performance Overlay", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
}
}  // namespace detail

OverlayWindowState::~OverlayWindowState() {
    if (memory_dc && old_bitmap) SelectObject(memory_dc, old_bitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memory_dc) DeleteDC(memory_dc);
    if (window) DestroyWindow(window);
}

Result<OverlayWindow> OverlayWindow::create() {
    WNDCLASSW type{};
    type.lpfnWndProc = detail::overlay_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = detail::kClassName;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return Result<OverlayWindow>::failure(
            {ErrorCode::platform_failure, L"Overlay window class cannot register", GetLastError()});
    }
    auto state = std::make_unique<OverlayWindowState>();
    state->mascot_animation = detail::load_mascot_animation_asset();
    state->window = detail::create_overlay_native_window(type.hInstance);
    if (!state->window) return Result<OverlayWindow>::failure(
        {ErrorCode::platform_failure, L"Overlay window cannot be created", GetLastError()});
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       state->d2d_factory.GetAddressOf());
    if (SUCCEEDED(result)) result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(state->write_factory.GetAddressOf()));
    if (SUCCEEDED(result)) result = state->write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0F,
        L"de-DE", &state->title_format);
    if (SUCCEEDED(result)) result = state->write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30.0F,
        L"de-DE", &state->value_format);
    if (SUCCEEDED(result)) result = state->write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F,
        L"de-DE", &state->metric_format);
    if (SUCCEEDED(result)) result = state->write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0F,
        L"de-DE", &state->summary_format);
    if (SUCCEEDED(result)) result = state->write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_CONDENSED, 11.8F,
        L"de-DE", &state->system_value_format);
    if (SUCCEEDED(result)) {
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));
        result = state->d2d_factory->CreateDCRenderTarget(
            &properties, &state->render_target);
    }
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.018F, 0.025F, 0.045F, 0.82F), &state->background);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.96F, 0.98F, 1.0F, 1.0F), &state->foreground);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.82F, 0.88F, 1.0F, 0.34F), &state->border);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.92F, 0.12F, 0.08F, 0.95F), &state->accent);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.78F, 0.84F, 0.93F, 0.98F), &state->muted);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.09F, 0.12F, 0.19F, 0.76F), &state->metric_panel);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.28F, 0.78F, 1.0F, 0.95F), &state->graph_line);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.30F, 0.58F, 0.42F, 1.0F), &state->mascot_fill);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.025F, 0.055F, 0.10F, 1.0F), &state->mascot_ink);
    if (SUCCEEDED(result)) result = state->render_target->CreateSolidColorBrush(
        D2D1::ColorF(0.78F, 0.94F, 0.70F, 0.82F), &state->mascot_highlight);
    if (SUCCEEDED(result) && SUCCEEDED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&state->wic_factory)))) {
        const HMODULE module = GetModuleHandleW(nullptr);
        const auto load_embedded_bitmap = [&](int resource_id,
                                               ID2D1Bitmap** output) {
            const HRSRC png_resource = FindResourceW(
                module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
            if (!png_resource) return;
            const HGLOBAL loaded = LoadResource(module, png_resource);
            const auto* png_bytes = static_cast<const BYTE*>(LockResource(loaded));
            const DWORD png_size = SizeofResource(module, png_resource);
            Microsoft::WRL::ComPtr<IWICStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            HRESULT image_result = state->wic_factory->CreateStream(&stream);
            if (SUCCEEDED(image_result)) image_result = stream->InitializeFromMemory(
                const_cast<BYTE*>(png_bytes), png_size);
            if (SUCCEEDED(image_result)) image_result =
                state->wic_factory->CreateDecoderFromStream(
                    stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(image_result)) image_result = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(image_result)) image_result =
                state->wic_factory->CreateFormatConverter(&converter);
            if (SUCCEEDED(image_result)) image_result = converter->Initialize(
                frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(image_result)) {
                state->render_target->CreateBitmapFromWicBitmap(
                    converter.Get(), nullptr, output);
            }
        };
        load_embedded_bitmap(detail::kPremiumMutantRigPngResource,
                             &state->mascot_bitmap);
        load_embedded_bitmap(detail::kPremiumMutantLowIdlePngResource,
                             &state->low_mascot_bitmap);
    }
    if (FAILED(result)) return Result<OverlayWindow>::failure(
        {ErrorCode::platform_failure, L"Overlay renderer cannot initialize",
         static_cast<std::uint32_t>(result)});
    state->memory_dc = CreateCompatibleDC(nullptr);
    if (!state->memory_dc) return Result<OverlayWindow>::failure(
        {ErrorCode::platform_failure, L"Overlay buffer cannot initialize", GetLastError()});
    return Result<OverlayWindow>::success(OverlayWindow{std::move(state)});
}

}  // namespace kf2::overlay
