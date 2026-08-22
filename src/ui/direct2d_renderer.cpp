#include "kf2/ui/direct2d_renderer.hpp"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kf2::ui {
namespace {

using Microsoft::WRL::ComPtr;

Result<bool> platform_failure(const wchar_t* message, HRESULT result) {
    return Result<bool>::failure(
        {ErrorCode::platform_failure, message, static_cast<std::uint32_t>(result)});
}

D2D1_COLOR_F color(std::uint32_t argb) noexcept {
    constexpr float divisor = 255.0F;
    return {static_cast<float>((argb >> 16U) & 0xFFU) / divisor,
            static_cast<float>((argb >> 8U) & 0xFFU) / divisor,
            static_cast<float>(argb & 0xFFU) / divisor,
            static_cast<float>((argb >> 24U) & 0xFFU) / divisor};
}

D2D1_RECT_F rectangle(const DipRect& value) noexcept {
    return {value.x, value.y, value.x + value.width, value.y + value.height};
}

HRESULT draw_shell(ID2D1RenderTarget* target, IDWriteFactory* write_factory,
                   const ShellLayoutResult& layout, const Theme& theme) {
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT result = target->CreateSolidColorBrush(color(theme.background), &brush);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> body_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"de-de",
        &body_format);
    if (FAILED(result)) return result;
    body_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    ComPtr<IDWriteTextFormat> heading_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26.0F, L"de-de",
        &heading_format);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> brand_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0F, L"de-de",
        &brand_format);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> brand_mark_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 34.0F, L"de-de",
        &brand_mark_format);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> action_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0F, L"de-de",
        &action_format);
    if (FAILED(result)) return result;
    action_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    action_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDWriteTextFormat> metric_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"de-de",
        &metric_format);
    if (FAILED(result)) return result;
    metric_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    metric_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDWriteTextFormat> slider_label_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"de-de",
        &slider_label_format);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> slider_value_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0F, L"de-de",
        &slider_value_format);
    if (FAILED(result)) return result;
    slider_value_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    target->BeginDraw();
    target->Clear(color(theme.background));
    brush->SetColor(color(theme.surface));
    target->FillRectangle(rectangle(layout.header), brush.Get());
    target->FillRectangle(rectangle(layout.sidebar), brush.Get());
    target->FillRectangle(rectangle(layout.footer), brush.Get());
    brush->SetColor(color(theme.surface_raised));
    target->FillRectangle(rectangle(layout.status_strip), brush.Get());
    brush->SetColor(color(theme.border));
    target->DrawRectangle(rectangle(layout.header), brush.Get(), 1.0F);
    target->DrawRectangle(rectangle(layout.status_strip), brush.Get(), 1.0F);
    target->DrawRectangle(rectangle(layout.metrics_strip), brush.Get(), 1.0F);
    target->DrawRectangle(rectangle(layout.sidebar), brush.Get(), 1.0F);

    for (const auto& node : layout.nodes) {
        if (node.role == SemanticRole::root) continue;
        const bool header_action = node.role == SemanticRole::action &&
            node.action_id && node.action_id->starts_with("header-");
        const bool page_node = node.role == SemanticRole::page_heading ||
            node.role == SemanticRole::page_body ||
            node.role == SemanticRole::section_heading ||
            (node.role == SemanticRole::action && !header_action) ||
            node.role == SemanticRole::slider;
        if (page_node) {
            target->PushAxisAlignedClip(rectangle(layout.content),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        if (node.role == SemanticRole::brand) {
            const bool mark = node.id == "brand-mark";
            brush->SetColor(color(mark ? theme.accent : theme.text));
            target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                              mark ? brand_mark_format.Get() : brand_format.Get(),
                              rectangle(node.bounds), brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }

        if (node.role == SemanticRole::metric_card) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get());
            brush->SetColor(color(theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get(), 1.0F);
            brush->SetColor(color(theme.warning));
            target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                              metric_format.Get(), rectangle(node.bounds), brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }

        if (node.role == SemanticRole::slider && node.slider) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 7.0F, 7.0F}, brush.Get());
            brush->SetColor(color(node.focused ? theme.accent : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 7.0F, 7.0F}, brush.Get(),
                node.focused ? 2.0F : 1.0F);

            brush->SetColor(color(node.enabled ? theme.text : theme.muted_text));
            const D2D1_RECT_F label_bounds{
                node.bounds.x + 18.0F, node.bounds.y + 10.0F,
                node.bounds.x + node.bounds.width * 0.68F,
                node.bounds.y + 36.0F};
            target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                              slider_label_format.Get(), label_bounds, brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            const std::wstring visible_value =
                std::to_wstring(node.slider->value) + node.slider->unit;
            const D2D1_RECT_F value_bounds{
                node.bounds.x + node.bounds.width * 0.55F, node.bounds.y + 7.0F,
                node.bounds.x + node.bounds.width - 18.0F,
                node.bounds.y + 38.0F};
            brush->SetColor(color(node.enabled ? theme.accent_hover
                                               : theme.muted_text));
            target->DrawTextW(visible_value.c_str(),
                              static_cast<UINT32>(visible_value.size()),
                              slider_value_format.Get(), value_bounds, brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);

            const float track_left = node.bounds.x + 28.0F;
            const float track_right = node.bounds.x + node.bounds.width - 28.0F;
            const float track_y = node.bounds.y + node.bounds.height - 25.0F;
            const float ratio = node.slider->maximum > node.slider->minimum
                ? std::clamp(
                      static_cast<float>(node.slider->value - node.slider->minimum) /
                          static_cast<float>(node.slider->maximum -
                                             node.slider->minimum),
                      0.0F, 1.0F)
                : 0.0F;
            const float thumb_x = track_left + (track_right - track_left) * ratio;
            brush->SetColor(color(theme.border));
            target->FillRoundedRectangle(
                {{track_left, track_y - 2.0F, track_right, track_y + 2.0F},
                 2.0F, 2.0F}, brush.Get());
            brush->SetColor(color(node.enabled ? theme.accent : theme.muted_text));
            target->FillRoundedRectangle(
                {{track_left, track_y - 2.0F, thumb_x, track_y + 2.0F},
                 2.0F, 2.0F}, brush.Get());
            brush->SetColor(color(theme.text));
            target->FillEllipse({{thumb_x, track_y}, 7.0F, 7.0F}, brush.Get());
            brush->SetColor(color(node.enabled ? theme.accent : theme.muted_text));
            target->DrawEllipse({{thumb_x, track_y}, 8.0F, 8.0F},
                                brush.Get(), 2.0F);
            if (page_node) target->PopAxisAlignedClip();
            continue;
        }

        if (node.role == SemanticRole::navigation_item &&
            (node.selected || node.focused)) {
            brush->SetColor(color(node.selected ? theme.accent : theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get());
            brush->SetColor(color(node.selected ? theme.accent_hover : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get(), 1.0F);
        } else if (node.role == SemanticRole::navigation_item) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get());
            brush->SetColor(color(theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get(), 1.0F);
        } else if (node.role == SemanticRole::recovery_banner ||
                   node.role == SemanticRole::notice) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get());
            brush->SetColor(color(theme.warning));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get(), 2.0F);
        } else if (node.role == SemanticRole::action) {
            const bool primary = node.action_id &&
                (*node.action_id == "game-launch" ||
                 *node.action_id == "header-launch" ||
                 *node.action_id == "dashboard-launch" ||
                 *node.action_id == "optimizer-apply" ||
                 *node.action_id == "diagnostics-full-check");
            const bool emphasized = primary;
            if (node.attention && node.enabled) {
                brush->SetColor(color(theme.warning));
                target->DrawRoundedRectangle(
                    {rectangle({node.bounds.x - 3.0F, node.bounds.y - 3.0F,
                                node.bounds.width + 6.0F,
                                node.bounds.height + 6.0F}), 8.0F, 8.0F},
                    brush.Get(), 3.0F);
            }
            brush->SetColor(color(!node.enabled ? theme.surface_raised
                                                : node.attention ? theme.warning
                                                : emphasized ? theme.accent
                                                          : theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get());
            brush->SetColor(color(node.focused ? theme.accent_hover :
                                  node.enabled ? (node.selected ? theme.success
                                                : emphasized ? theme.accent_hover
                                                             : theme.border)
                                               : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get(),
                node.focused || node.selected ? 2.0F : 1.0F);
        } else if (node.role == SemanticRole::tooltip) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get());
            brush->SetColor(color(theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get(), 1.0F);
        }

        const bool heading = node.role == SemanticRole::page_heading;
        const bool section = node.role == SemanticRole::section_heading;
        const bool navigation_group =
            node.role == SemanticRole::navigation_group;
        brush->SetColor(color(node.role == SemanticRole::action
                                  ? (node.enabled ? theme.text : theme.muted_text)
                                  : section ? theme.warning
                                  : navigation_group ? theme.muted_text
                                  : node.role == SemanticRole::page_body ||
                                            node.role == SemanticRole::footer
                                      ? theme.muted_text
                                  : (node.selected &&
                                             node.role == SemanticRole::navigation_item
                                         ? theme.surface
                                         : theme.text)));
        const auto bounds = rectangle(node.bounds);
        target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                          heading ? heading_format.Get()
                                  : node.role == SemanticRole::action
                                      ? action_format.Get()
                                      : body_format.Get(),
                          bounds, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (page_node) target->PopAxisAlignedClip();
    }
    if (layout.scroll_extent > 0.0F && layout.content.height > 0.0F) {
        const float track_x = layout.content.x + layout.content.width + 5.0F;
        const float track_top = layout.content.y;
        const float track_height = layout.content.height;
        const float total_height = track_height + layout.scroll_extent;
        const float thumb_height = std::max(
            34.0F, track_height * track_height / total_height);
        const float travel = std::max(0.0F, track_height - thumb_height);
        const float ratio = std::clamp(
            layout.scroll_offset / layout.scroll_extent, 0.0F, 1.0F);
        brush->SetColor(color(theme.border));
        target->FillRoundedRectangle(
            {{track_x, track_top, track_x + 3.0F, track_top + track_height},
             1.5F, 1.5F}, brush.Get());
        brush->SetColor(color(theme.accent));
        target->FillRoundedRectangle(
            {{track_x - 1.0F, track_top + travel * ratio,
              track_x + 4.0F, track_top + travel * ratio + thumb_height},
             2.5F, 2.5F}, brush.Get());
    }
    return target->EndDraw();
}

}  // namespace

struct Direct2DShellRenderer::Impl {
    HWND window{};
    float dpi{96.0F};
    PixelSize pixel_size{};
    ComPtr<ID2D1Factory1> d2d_factory;
    ComPtr<IDWriteFactory> write_factory;
    ComPtr<IWICImagingFactory> wic_factory;
    ComPtr<ID2D1HwndRenderTarget> window_target;

    HRESULT ensure_window_target() {
        if (window_target || pixel_size.width == 0 || pixel_size.height == 0) {
            return S_OK;
        }
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
        const auto hwnd_properties = D2D1::HwndRenderTargetProperties(
            window, D2D1::SizeU(pixel_size.width, pixel_size.height));
        return d2d_factory->CreateHwndRenderTarget(
            properties, hwnd_properties, &window_target);
    }
};

Direct2DShellRenderer::Direct2DShellRenderer(std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}

Direct2DShellRenderer::Direct2DShellRenderer(Direct2DShellRenderer&&) noexcept = default;
Direct2DShellRenderer& Direct2DShellRenderer::operator=(Direct2DShellRenderer&&) noexcept = default;
Direct2DShellRenderer::~Direct2DShellRenderer() = default;

Result<Direct2DShellRenderer> Direct2DShellRenderer::create(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return Result<Direct2DShellRenderer>::failure(
            {ErrorCode::invalid_argument, L"Renderer requires a valid window", 0});
    }
    auto implementation = std::make_unique<Impl>();
    implementation->window = window;
    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
        reinterpret_cast<void**>(implementation->d2d_factory.GetAddressOf()));
    if (SUCCEEDED(result)) {
        result = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(implementation->write_factory.GetAddressOf()));
    }
    if (SUCCEEDED(result)) {
        result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&implementation->wic_factory));
    }
    if (FAILED(result)) {
        return Result<Direct2DShellRenderer>::failure(
            {ErrorCode::platform_failure, L"Cannot create graphics factories",
             static_cast<std::uint32_t>(result)});
    }
    RECT area{};
    GetClientRect(window, &area);
    implementation->pixel_size = {
        static_cast<unsigned>(std::max<LONG>(0, area.right - area.left)),
        static_cast<unsigned>(std::max<LONG>(0, area.bottom - area.top))};
    return Result<Direct2DShellRenderer>::success(
        Direct2DShellRenderer{std::move(implementation)});
}

Result<bool> Direct2DShellRenderer::resize(PixelSize size, float dpi) {
    if (!std::isfinite(dpi) || dpi <= 0.0F) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"DPI must be positive", 0});
    }
    implementation_->pixel_size = size;
    implementation_->dpi = dpi;
    if (!implementation_->window_target) return Result<bool>::success(true);
    implementation_->window_target->SetDpi(dpi, dpi);
    if (size.width == 0 || size.height == 0) return Result<bool>::success(true);
    const HRESULT result = implementation_->window_target->Resize(
        D2D1::SizeU(size.width, size.height));
    if (result == D2DERR_RECREATE_TARGET) {
        discard_device_resources();
        return Result<bool>::success(true);
    }
    return FAILED(result) ? platform_failure(L"Cannot resize render target", result)
                          : Result<bool>::success(true);
}

Result<bool> Direct2DShellRenderer::render(const ShellLayoutResult& layout,
                                           const Theme& theme) {
    if (implementation_->pixel_size.width == 0 ||
        implementation_->pixel_size.height == 0) {
        return Result<bool>::success(true);
    }
    const HRESULT created = implementation_->ensure_window_target();
    if (FAILED(created)) return platform_failure(L"Cannot create render target", created);
    const HRESULT result = draw_shell(implementation_->window_target.Get(),
                                      implementation_->write_factory.Get(), layout, theme);
    if (result == D2DERR_RECREATE_TARGET) {
        discard_device_resources();
        return Result<bool>::success(true);
    }
    return FAILED(result) ? platform_failure(L"Cannot render shell", result)
                          : Result<bool>::success(true);
}

void Direct2DShellRenderer::discard_device_resources() noexcept {
    implementation_->window_target.Reset();
}

Result<bool> Direct2DShellRenderer::capture_wic_png(
    const std::filesystem::path& path, const ShellLayoutResult& layout,
    const Theme& theme, PixelSize pixel_size, float dpi) {
    if (pixel_size.width == 0 || pixel_size.height == 0 || dpi <= 0.0F) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Capture dimensions and DPI must be positive", 0});
    }
    ComPtr<IWICBitmap> bitmap;
    HRESULT result = implementation_->wic_factory->CreateBitmap(
        pixel_size.width, pixel_size.height, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad, &bitmap);
    ComPtr<ID2D1RenderTarget> target;
    if (SUCCEEDED(result)) {
        result = implementation_->d2d_factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(
                              D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                D2D1_ALPHA_MODE_PREMULTIPLIED),
                              dpi, dpi),
            &target);
    }
    if (SUCCEEDED(result)) {
        result = draw_shell(target.Get(), implementation_->write_factory.Get(),
                            layout, theme);
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(result)) result = implementation_->wic_factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result)) {
        result = implementation_->wic_factory->CreateEncoder(
            GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(result)) result = frame->Initialize(nullptr);
    if (SUCCEEDED(result)) result = frame->SetSize(pixel_size.width, pixel_size.height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result)) result = frame->WriteSource(bitmap.Get(), nullptr);
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    return FAILED(result) ? platform_failure(L"Cannot capture PNG", result)
                          : Result<bool>::success(true);
}

}  // namespace kf2::ui
