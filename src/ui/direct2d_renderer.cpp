#include "kf2/ui/direct2d_renderer.hpp"
#include "kf2/ui/ui_animation.hpp"

#include <d2d1_1.h>
#include <d2d1helper.h>
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
    body_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                19.0F, 14.5F);

    ComPtr<IDWriteTextFormat> section_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5F, L"de-de",
        &section_format);
    if (FAILED(result)) return result;
    section_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    section_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                   17.0F, 13.0F);

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

    ComPtr<IDWriteTextFormat> navigation_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"de-de",
        &navigation_format);
    if (FAILED(result)) return result;
    navigation_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    navigation_format->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDWriteTextFormat> metric_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"de-de",
        &metric_format);
    if (FAILED(result)) return result;
    metric_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    metric_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDWriteTextFormat> metric_value_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"de-de",
        &metric_value_format);
    if (FAILED(result)) return result;
    metric_value_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    metric_value_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    metric_value_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                        19.0F, 14.5F);

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

    ComPtr<IDWriteTextFormat> tooltip_title_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"de-de",
        &tooltip_title_format);
    if (FAILED(result)) return result;

    ComPtr<IDWriteTextFormat> tooltip_body_format;
    result = write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0F, L"de-de",
        &tooltip_body_format);
    if (FAILED(result)) return result;
    tooltip_body_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    tooltip_body_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                        18.0F, 13.5F);

    target->BeginDraw();
    target->Clear(color(theme.background));
    if (layout.exit_progress >= 1.0F) {
        return target->EndDraw();
    }
    const float exit_visibility =
        1.0F - smooth_motion(layout.exit_progress);
    brush->SetOpacity(1.0F);
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
    if (layout.header.width > 230.0F) {
        brush->SetOpacity(exit_visibility * 0.85F);
        brush->SetColor(color(theme.accent));
        target->FillRoundedRectangle(
            {{22.0F, layout.header.y + layout.header.height - 3.0F,
              210.0F, layout.header.y + layout.header.height - 1.0F},
             1.0F, 1.0F},
            brush.Get());
        brush->SetOpacity(exit_visibility);
    }

    if (layout.navigation_indicator) {
        DipRect rail = *layout.navigation_indicator;
        rail.width = std::min(4.0F, rail.width);
        brush->SetOpacity(exit_visibility);
        brush->SetColor(color(theme.accent));
        target->FillRoundedRectangle(
            {rectangle(rail), 2.0F, 2.0F},
            brush.Get());
    }

    for (const auto& node : layout.nodes) {
        if (node.role == SemanticRole::root) continue;
        const float visible_opacity = node.role == SemanticRole::tooltip
            ? smooth_motion(node.opacity) : node.opacity;
        float node_opacity =
            std::clamp(visible_opacity, 0.0F, 1.0F) * exit_visibility;
        const bool header_action = node.role == SemanticRole::action &&
            node.id.starts_with("header-");
        const bool page_node = node.role == SemanticRole::page_heading ||
            node.role == SemanticRole::page_body ||
            node.role == SemanticRole::section_heading ||
            node.role == SemanticRole::recovery_banner ||
            node.role == SemanticRole::notice ||
            (node.role == SemanticRole::action && !header_action) ||
            node.role == SemanticRole::slider;
        if (page_node) {
            node_opacity *= page_motion_opacity(
                layout.page_transition_progress);
        }
        brush->SetOpacity(node_opacity);
        if (page_node) {
            target->PushAxisAlignedClip(rectangle(layout.content),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        D2D1_MATRIX_3X2_F original_transform{};
        bool animated_transform = false;
        D2D1_MATRIX_3X2_F motion_transform =
            D2D1::Matrix3x2F::Identity();
        if (page_node) {
            motion_transform = motion_transform * D2D1::Matrix3x2F::Translation(
                page_motion_offset_x(layout.page_transition_progress), 0.0F);
            animated_transform = true;
        }
        if (node.role == SemanticRole::tooltip) {
            motion_transform = motion_transform * D2D1::Matrix3x2F::Translation(
                0.0F, tooltip_motion_offset_y(node.opacity));
            animated_transform = true;
        }
        if ((node.role == SemanticRole::navigation_item ||
             node.role == SemanticRole::action) &&
            node.hover > 0.0F && !node.pressed) {
            motion_transform = motion_transform * D2D1::Matrix3x2F::Translation(
                0.0F, -1.5F * emphasized_motion(node.hover));
            animated_transform = true;
        }
        if ((node.role == SemanticRole::navigation_item ||
             node.role == SemanticRole::action ||
             node.role == SemanticRole::slider) &&
            node.interaction > 0.0F) {
            const float scale = control_press_scale(node.interaction);
            const D2D1_POINT_2F center{
                node.bounds.x + node.bounds.width / 2.0F,
                node.bounds.y + node.bounds.height / 2.0F};
            motion_transform = motion_transform *
                D2D1::Matrix3x2F::Scale(scale, scale, center);
            animated_transform = true;
        }
        if (animated_transform) {
            target->GetTransform(&original_transform);
            target->SetTransform(motion_transform * original_transform);
        }

        if (node.role == SemanticRole::brand) {
            const bool mark = node.id == "brand-mark";
            const float brand_progress = std::min(
                layout.startup_progress, 1.0F - layout.exit_progress);
            target->GetTransform(&original_transform);
            const D2D1_POINT_2F center{
                node.bounds.x + node.bounds.width / 2.0F,
                node.bounds.y + node.bounds.height / 2.0F};
            const auto brand_transform = mark
                ? D2D1::Matrix3x2F::Scale(
                      startup_logo_scale(brand_progress),
                      startup_logo_scale(brand_progress), center)
                : D2D1::Matrix3x2F::Translation(
                      startup_title_offset_x(brand_progress), 0.0F);
            target->SetTransform(brand_transform * original_transform);
            brush->SetOpacity(smooth_motion(brand_progress));
            brush->SetColor(color(mark ? theme.accent : theme.text));
            target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                              mark ? brand_mark_format.Get() : brand_format.Get(),
                              rectangle(node.bounds), brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            target->SetTransform(original_transform);
            continue;
        }

        if (node.role == SemanticRole::metric_card) {
            brush->SetOpacity(node_opacity * 0.55F);
            brush->SetColor(color(theme.background));
            target->FillRoundedRectangle(
                {rectangle({node.bounds.x, node.bounds.y + 3.0F,
                            node.bounds.width, node.bounds.height}),
                 9.0F, 9.0F},
                brush.Get());
            brush->SetOpacity(node_opacity);
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 9.0F, 9.0F}, brush.Get());
            brush->SetColor(color(theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 9.0F, 9.0F}, brush.Get(), 1.0F);
            brush->SetColor(color(theme.accent));
            target->FillRoundedRectangle(
                {{node.bounds.x + 14.0F, node.bounds.y + 7.0F,
                  node.bounds.x + 46.0F, node.bounds.y + 9.0F},
                 1.0F, 1.0F},
                brush.Get());
            const auto divider = node.text.find(L'\n');
            const std::wstring title = node.text.substr(0, divider);
            const std::wstring value = divider == std::wstring::npos
                ? std::wstring{} : node.text.substr(divider + 1);
            const D2D1_RECT_F title_bounds{
                node.bounds.x + 8.0F, node.bounds.y + 10.0F,
                node.bounds.x + node.bounds.width - 8.0F,
                node.bounds.y + 34.0F};
            const D2D1_RECT_F value_bounds{
                node.bounds.x + 8.0F, node.bounds.y + 30.0F,
                node.bounds.x + node.bounds.width - 8.0F,
                node.bounds.y + node.bounds.height - 8.0F};
            brush->SetColor(color(theme.warning));
            target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()),
                              metric_format.Get(), title_bounds, brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush->SetColor(color(value.empty() ? theme.muted_text : theme.text));
            target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()),
                              metric_value_format.Get(), value_bounds,
                              brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }

        if (node.role == SemanticRole::status) {
            brush->SetColor(color(theme.success));
            target->FillEllipse(
                {{node.bounds.x - 15.0F,
                  node.bounds.y + node.bounds.height / 2.0F},
                 3.5F, 3.5F},
                brush.Get());
        }

        if (node.role == SemanticRole::slider && node.slider) {
            brush->SetOpacity(node_opacity * 0.45F);
            brush->SetColor(color(theme.background));
            target->FillRoundedRectangle(
                {rectangle({node.bounds.x, node.bounds.y + 3.0F,
                            node.bounds.width, node.bounds.height}),
                 10.0F, 10.0F},
                brush.Get());
            brush->SetOpacity(node_opacity);
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 10.0F, 10.0F}, brush.Get());
            if (node.hover > 0.0F) {
                brush->SetOpacity(node_opacity * node.hover * 0.12F);
                brush->SetColor(color(theme.info));
                target->FillRoundedRectangle(
                    {rectangle(node.bounds), 10.0F, 10.0F}, brush.Get());
                brush->SetOpacity(node_opacity);
            }
            brush->SetColor(color(node.focused ? theme.accent : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 10.0F, 10.0F}, brush.Get(),
                node.focused ? 2.0F : 1.0F);
            if (node.interaction > 0.0F) {
                brush->SetOpacity(node_opacity * node.interaction);
                brush->SetColor(color(theme.info));
                target->DrawRoundedRectangle(
                    {rectangle(node.bounds), 7.0F, 7.0F}, brush.Get(), 3.0F);
                brush->SetOpacity(node_opacity);
            }

            brush->SetColor(color(node.enabled ? theme.text : theme.muted_text));
            const D2D1_RECT_F value_pill{
                node.bounds.x + node.bounds.width - 148.0F,
                node.bounds.y + 8.0F,
                node.bounds.x + node.bounds.width - 18.0F,
                node.bounds.y + 38.0F};
            const D2D1_RECT_F label_bounds{
                node.bounds.x + 18.0F, node.bounds.y + 10.0F,
                value_pill.left - 12.0F,
                node.bounds.y + 36.0F};
            target->DrawTextW(node.text.c_str(), static_cast<UINT32>(node.text.size()),
                              slider_label_format.Get(), label_bounds, brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush->SetColor(color(theme.background));
            target->FillRoundedRectangle(
                {value_pill, 6.0F, 6.0F}, brush.Get());
            brush->SetColor(color(node.focused ? theme.accent : theme.border));
            target->DrawRoundedRectangle(
                {value_pill, 6.0F, 6.0F}, brush.Get(), 1.0F);
            const std::wstring visible_value =
                std::to_wstring(node.slider->value) + node.slider->unit;
            const D2D1_RECT_F value_bounds{
                value_pill.left + 10.0F, value_pill.top,
                value_pill.right - 10.0F,
                node.bounds.y + 38.0F};
            brush->SetColor(color(node.enabled ? theme.accent_hover
                                               : theme.muted_text));
            target->DrawTextW(visible_value.c_str(),
                              static_cast<UINT32>(visible_value.size()),
                              slider_value_format.Get(), value_bounds, brush.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);

            const float track_left = node.bounds.x + 28.0F;
            const float track_right = node.bounds.x + node.bounds.width - 28.0F;
            const float track_y = node.bounds.y + node.bounds.height - 27.0F;
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
                {{track_left, track_y - 3.0F, track_right, track_y + 3.0F},
                 3.0F, 3.0F}, brush.Get());
            brush->SetColor(color(node.enabled ? theme.accent : theme.muted_text));
            target->FillRoundedRectangle(
                {{track_left, track_y - 3.0F, thumb_x, track_y + 3.0F},
                 3.0F, 3.0F}, brush.Get());
            brush->SetColor(color(theme.surface_raised));
            target->FillEllipse({{thumb_x, track_y}, 9.0F, 9.0F}, brush.Get());
            brush->SetColor(color(node.enabled ? theme.accent : theme.muted_text));
            target->DrawEllipse({{thumb_x, track_y}, 9.0F, 9.0F},
                                brush.Get(), 2.0F);
            target->FillEllipse({{thumb_x, track_y}, 3.0F, 3.0F}, brush.Get());
            if (animated_transform) target->SetTransform(original_transform);
            if (page_node) target->PopAxisAlignedClip();
            continue;
        }

        if (node.role == SemanticRole::page_heading) {
            brush->SetColor(color(theme.accent));
            target->FillRoundedRectangle(
                {{node.bounds.x, node.bounds.y + node.bounds.height - 3.0F,
                  node.bounds.x + 46.0F,
                  node.bounds.y + node.bounds.height - 1.0F},
                 1.0F, 1.0F},
                brush.Get());
        } else if (node.role == SemanticRole::section_heading) {
            brush->SetColor(color(theme.border));
            target->FillRectangle(
                {node.bounds.x, node.bounds.y + node.bounds.height - 2.0F,
                 node.bounds.x + node.bounds.width,
                 node.bounds.y + node.bounds.height - 1.0F},
                brush.Get());
            brush->SetColor(color(theme.warning));
            target->FillRoundedRectangle(
                {{node.bounds.x, node.bounds.y + node.bounds.height - 3.0F,
                  node.bounds.x + 30.0F,
                  node.bounds.y + node.bounds.height},
                 1.5F, 1.5F},
                brush.Get());
        } else if (node.role == SemanticRole::navigation_item &&
            (node.selected || node.focused)) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get());
            if (node.selected) {
                brush->SetOpacity(node_opacity * 0.10F);
                brush->SetColor(color(theme.accent));
                target->FillRoundedRectangle(
                    {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get());
                brush->SetOpacity(node_opacity);
            }
            brush->SetColor(color(node.selected ? theme.accent_hover : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get(),
                node.selected ? 2.0F : 1.0F);
        } else if (node.role == SemanticRole::navigation_item) {
            if (!node.selected) {
                brush->SetOpacity(node_opacity * 0.72F);
                brush->SetColor(color(theme.surface_raised));
                target->FillRoundedRectangle(
                    {rectangle(node.bounds), 7.0F, 7.0F}, brush.Get());
                brush->SetColor(color(theme.border));
                target->DrawRoundedRectangle(
                    {rectangle(node.bounds), 7.0F, 7.0F}, brush.Get(), 1.0F);
                brush->SetOpacity(node_opacity);
            }
        } else if (node.role == SemanticRole::recovery_banner ||
                   node.role == SemanticRole::notice) {
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 5.0F, 5.0F}, brush.Get());
            const std::uint32_t notice_color =
                node.role == SemanticRole::recovery_banner
                    ? theme.warning
                    : node.notice_severity == NoticeSeverity::error
                        ? theme.error
                        : node.notice_severity == NoticeSeverity::warning
                            ? theme.warning
                            : theme.info;
            brush->SetColor(color(notice_color));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get(), 1.5F);
            target->FillRoundedRectangle(
                {{node.bounds.x, node.bounds.y + 8.0F,
                  node.bounds.x + 4.0F,
                  node.bounds.y + node.bounds.height - 8.0F},
                 2.0F, 2.0F},
                brush.Get());
        } else if (node.role == SemanticRole::action) {
            const bool primary = node.action_id &&
                (*node.action_id == "dashboard-launch" ||
                 *node.action_id == "diagnostics-full-check");
            const bool emphasized = primary;
            if (node.attention && node.enabled &&
                layout.update_glow_progress < 1.0F) {
                brush->SetOpacity(node_opacity *
                    update_glow_opacity(layout.update_glow_progress));
                brush->SetColor(color(theme.warning));
                target->DrawRoundedRectangle(
                    {rectangle({node.bounds.x - 3.0F, node.bounds.y - 3.0F,
                                node.bounds.width + 6.0F,
                                node.bounds.height + 6.0F}), 8.0F, 8.0F},
                    brush.Get(), 3.0F);
                brush->SetOpacity(node_opacity);
            }
            brush->SetOpacity(node_opacity * 0.50F);
            brush->SetColor(color(theme.background));
            target->FillRoundedRectangle(
                {rectangle({node.bounds.x, node.bounds.y + 2.0F,
                            node.bounds.width, node.bounds.height}),
                 8.0F, 8.0F},
                brush.Get());
            brush->SetOpacity(node_opacity);
            brush->SetColor(color(!node.enabled ? theme.surface_raised
                                                : node.attention ? theme.warning
                                                : emphasized ? theme.accent
                                                          : theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get());
            if (node.selected && node.enabled && !emphasized) {
                brush->SetOpacity(node_opacity * 0.10F);
                brush->SetColor(color(theme.success));
                target->FillRoundedRectangle(
                    {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get());
                brush->SetOpacity(node_opacity);
            }
            brush->SetColor(color(node.focused ? theme.info :
                                  node.enabled ? (node.selected ? theme.success
                                                : emphasized ? theme.accent_hover
                                                             : theme.border)
                                               : theme.border));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get(),
                node.focused || node.selected ? 2.0F : 1.0F);
            if (node.interaction > 0.0F) {
                brush->SetOpacity(node_opacity * node.interaction);
                brush->SetColor(color(theme.info));
                target->DrawRoundedRectangle(
                    {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get(), 3.0F);
                brush->SetOpacity(node_opacity);
            }
        } else if (node.role == SemanticRole::tooltip) {
            const float tooltip_card_opacity =
                node_opacity > 0.0F ? 1.0F : 0.0F;
            brush->SetOpacity(tooltip_card_opacity * 0.60F);
            brush->SetColor(color(theme.background));
            target->FillRoundedRectangle(
                {rectangle({node.bounds.x + 2.0F, node.bounds.y + 5.0F,
                            node.bounds.width, node.bounds.height}),
                 10.0F, 10.0F},
                brush.Get());
            brush->SetOpacity(tooltip_card_opacity);
            brush->SetColor(color(theme.surface_raised));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get());
            brush->SetColor(color(theme.info));
            target->DrawRoundedRectangle(
                {rectangle(node.bounds), 8.0F, 8.0F}, brush.Get(), 1.5F);

            const D2D1_RECT_F title_bounds{
                node.bounds.x + 14.0F, node.bounds.y + 9.0F,
                node.bounds.x + node.bounds.width - 14.0F,
                node.bounds.y + 31.0F};
            brush->SetColor(color(theme.text));
            target->DrawTextW(
                node.text.c_str(), static_cast<UINT32>(node.text.size()),
                tooltip_title_format.Get(), title_bounds, brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);

            const D2D1_RECT_F body_bounds{
                node.bounds.x + 14.0F, node.bounds.y + 32.0F,
                node.bounds.x + node.bounds.width - 14.0F,
                node.bounds.y + node.bounds.height - 10.0F};
            brush->SetColor(color(theme.muted_text));
            target->DrawTextW(
                node.detail_text.c_str(),
                static_cast<UINT32>(node.detail_text.size()),
                tooltip_body_format.Get(), body_bounds, brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if ((node.role == SemanticRole::navigation_item ||
             node.role == SemanticRole::action) && node.hover > 0.0F) {
            brush->SetOpacity(node_opacity *
                              emphasized_motion(node.hover) * 0.12F);
            brush->SetColor(color(theme.info));
            target->FillRoundedRectangle(
                {rectangle(node.bounds), 6.0F, 6.0F}, brush.Get());
            brush->SetOpacity(node_opacity);
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
                                  : theme.text));
        auto bounds = rectangle(node.bounds);
        if (node.role == SemanticRole::navigation_item) {
            bounds.left += 12.0F;
            bounds.right -= 8.0F;
        } else if (node.role == SemanticRole::notice ||
                   node.role == SemanticRole::recovery_banner) {
            bounds.left += 16.0F;
            bounds.top += 5.0F;
            bounds.right -= 12.0F;
            bounds.bottom -= 4.0F;
        }
        if (node.role != SemanticRole::tooltip) {
            IDWriteTextFormat* text_format = body_format.Get();
            if (heading) {
                text_format = heading_format.Get();
            } else if (node.role == SemanticRole::action) {
                text_format = action_format.Get();
            } else if (node.role == SemanticRole::navigation_item) {
                text_format = navigation_format.Get();
            } else if (section) {
                text_format = section_format.Get();
            }
            target->DrawTextW(node.text.c_str(),
                              static_cast<UINT32>(node.text.size()),
                              text_format,
                              bounds, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (animated_transform) target->SetTransform(original_transform);
        if (page_node) target->PopAxisAlignedClip();
    }
    brush->SetOpacity(exit_visibility);
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
        brush->SetOpacity(exit_visibility * 0.65F);
        brush->SetColor(color(theme.border));
        target->FillRoundedRectangle(
            {{track_x - 1.0F, track_top, track_x + 5.0F,
              track_top + track_height},
             3.0F, 3.0F}, brush.Get());
        brush->SetOpacity(exit_visibility);
        brush->SetColor(color(theme.accent));
        target->FillRoundedRectangle(
            {{track_x - 1.0F, track_top + travel * ratio,
              track_x + 5.0F, track_top + travel * ratio + thumb_height},
             3.0F, 3.0F}, brush.Get());
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
