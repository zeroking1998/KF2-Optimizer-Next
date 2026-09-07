#include "overlay_window_internal.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace kf2::overlay::detail {
namespace {

constexpr float kLogicalCanvasWidth = 330.0F;
constexpr float kLogicalContentWidth = 322.0F;
constexpr float kLogicalHeight = 105.0F;

}  // namespace

bool static_layer_matches(const OverlayWindowState& state, LONG width,
                          LONG height) noexcept {
    return state.static_layer_bitmap && state.static_layer_size.cx == width &&
        state.static_layer_size.cy == height &&
        state.static_layer_show_fps == state.target.show_fps &&
        state.static_layer_show_frame_time == state.target.show_frame_time &&
        state.static_layer_show_cpu == state.target.show_cpu &&
        state.static_layer_show_gpu == state.target.show_gpu &&
        state.static_layer_show_memory == state.target.show_memory;
}

HRESULT rebuild_static_layer(OverlayWindowState& state, LONG width,
                             LONG height) {
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> layer_target;
    HRESULT result = state.render_target->CreateCompatibleRenderTarget(
        D2D1::SizeF(static_cast<float>(width), static_cast<float>(height)),
        D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &layer_target);
    if (FAILED(result)) return result;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> metric_panel;
    result = layer_target->CreateSolidColorBrush(
        state.background->GetColor(), &background);
    if (SUCCEEDED(result)) result = layer_target->CreateSolidColorBrush(
        state.border->GetColor(), &border);
    if (SUCCEEDED(result)) result = layer_target->CreateSolidColorBrush(
        state.muted->GetColor(), &muted);
    if (SUCCEEDED(result)) result = layer_target->CreateSolidColorBrush(
        state.metric_panel->GetColor(), &metric_panel);
    if (FAILED(result)) return result;

    layer_target->BeginDraw();
    layer_target->Clear(D2D1::ColorF(0, 0.0F));
    const auto transform = D2D1::Matrix3x2F::Translation(8.0F, 0.0F) *
        D2D1::Matrix3x2F::Scale(
            static_cast<float>(width) / kLogicalCanvasWidth,
            static_cast<float>(height) / kLogicalHeight);
    layer_target->SetTransform(transform);
    const auto card = D2D1::RoundedRect(
        D2D1::RectF(2.0F, 2.0F, kLogicalContentWidth - 2.0F,
                    kLogicalHeight - 2.0F), 11, 11);
    layer_target->FillRoundedRectangle(card, background.Get());
    layer_target->DrawRoundedRectangle(card, border.Get(), 1.0F);
    const std::array panels{
        D2D1::RoundedRect(D2D1::RectF(76.0F, 13.0F, 139.0F, 64.0F),
                          5.0F, 5.0F),
        D2D1::RoundedRect(D2D1::RectF(140.0F, 6.0F, 223.0F, 66.0F),
                          6.0F, 6.0F),
        D2D1::RoundedRect(D2D1::RectF(225.0F, 6.0F, 315.0F, 66.0F),
                          6.0F, 6.0F)};
    for (const auto& panel : panels) {
        layer_target->FillRoundedRectangle(panel, metric_panel.Get());
        layer_target->DrawRoundedRectangle(panel, border.Get(), 1.0F);
    }
    const auto draw = [&](std::wstring_view value, D2D1_RECT_F bounds) {
        layer_target->DrawTextW(
            value.data(), static_cast<UINT32>(value.size()),
            state.title_format.Get(), bounds, muted.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    if (state.target.show_fps) {
        draw(L"LIVE FPS", D2D1::RectF(12, 14, 116, 30));
        draw(L"AVG", D2D1::RectF(146, 12, 195, 28));
        draw(L"1% LOW", D2D1::RectF(231, 12, 287, 28));
    }
    if (state.target.show_cpu) {
        draw(L"CPU", D2D1::RectF(79, 17, 104, 34));
    }
    if (state.target.show_gpu) {
        draw(L"GPU", D2D1::RectF(79, 43, 104, 60));
    }
    layer_target->DrawLine(D2D1::Point2F(80, 38),
                           D2D1::Point2F(128, 38), border.Get(), 0.8F);
    layer_target->DrawLine(D2D1::Point2F(12, 70),
                           D2D1::Point2F(310, 70), border.Get(), 1.0F);
    if (state.target.show_frame_time) {
        draw(L"FRAME TIME", D2D1::RectF(12, 77, 91, 94));
        const D2D1_RECT_F graph_bounds = state.target.show_memory
            ? D2D1::RectF(140, 95, 298, 102)
            : D2D1::RectF(140, 76, 298, 100);
        layer_target->FillRoundedRectangle(
            D2D1::RoundedRect(graph_bounds, 4.0F, 4.0F), metric_panel.Get());
    }
    result = layer_target->EndDraw();
    if (FAILED(result)) return result;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    result = layer_target->GetBitmap(&bitmap);
    if (FAILED(result)) return result;
    state.static_layer_bitmap = std::move(bitmap);
    state.static_layer_size = {width, height};
    state.static_layer_show_fps = state.target.show_fps;
    state.static_layer_show_frame_time = state.target.show_frame_time;
    state.static_layer_show_cpu = state.target.show_cpu;
    state.static_layer_show_gpu = state.target.show_gpu;
    state.static_layer_show_memory = state.target.show_memory;
    ++state.static_layer_builds;
    return S_OK;
}

}  // namespace kf2::overlay::detail
