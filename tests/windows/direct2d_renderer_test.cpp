#include <Windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

#include "kf2/platform/windows/window.hpp"
#include "kf2/ui/direct2d_renderer.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/theme.hpp"
#include "kf2/ui/ui_model.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

struct DecodedPng {
    UINT width{0};
    UINT height{0};
    std::size_t distinct_colors{0};
};

DecodedPng decode_png(const std::filesystem::path& path) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    DecodedPng decoded{};

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(frame->GetSize(&decoded.width, &decoded.height)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
        return {};
    }

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(decoded.width) * decoded.height);
    const auto stride = static_cast<UINT>(decoded.width * sizeof(std::uint32_t));
    const auto byte_count = static_cast<UINT>(
        pixels.size() * sizeof(std::uint32_t));
    if (SUCCEEDED(converter->CopyPixels(
            nullptr, stride, byte_count,
            reinterpret_cast<BYTE*>(pixels.data())))) {
        decoded.distinct_colors = std::set<std::uint32_t>(pixels.begin(), pixels.end()).size();
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return decoded;
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    const auto window = kf2::platform::windows::Window::create(
        {.title = L"KF2 renderer test", .width = 800, .height = 520, .visible = false,
         .renderer_owns_background = true});
    CHECK(window.has_value());

    auto renderer = kf2::ui::Direct2DShellRenderer::create(
        static_cast<HWND>(window.value().native_handle_for_testing()));
    CHECK(renderer.has_value());

    kf2::ui::UiModel model;
    model.set_state_path(L"C:\\KF2Optimizer\\Data");
    const auto layout = kf2::ui::layout_shell(model, 800.0F, 520.0F);
    const auto theme = kf2::ui::resolve_theme({});
    CHECK(renderer.value().resize({800, 520}, 96.0F).has_value());
    CHECK(renderer.value().render(layout, theme).has_value());
    CHECK(renderer.value().resize({0, 0}, 96.0F).has_value());
    renderer.value().discard_device_resources();
    CHECK(renderer.value().resize({800, 520}, 192.0F).has_value());
    CHECK(renderer.value().render(layout, theme).has_value());

    const auto output = argument_count > 1
                            ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{KF2_GUI_TEST_ROOT};
    std::filesystem::create_directories(output);
    for (const auto [dpi, width, height] : {
             std::tuple{96.0F, 800U, 520U},
             std::tuple{144.0F, 1200U, 780U},
             std::tuple{192.0F, 1600U, 1040U}}) {
        const auto path = output / (L"dashboard-" + std::to_wstring(static_cast<int>(dpi)) + L".png");
        CHECK(renderer.value().capture_wic_png(path, layout, theme,
                                               {width, height}, dpi).has_value());
        const auto decoded = decode_png(path);
        CHECK(decoded.width == width);
        CHECK(decoded.height == height);
        CHECK(decoded.distinct_colors > 1);
    }

    auto status = model.status();
    status.mode = L"Adaptive / Automatic";
    status.game = L"Game detected: D:\\Steam\\KillingFloor2";
    status.game_detected = true;
    status.telemetry = L"143.2 FPS, 7.0 ms";
    status.live_fps = 143.2;
    status.live_frame_time_ms = 7.0;
    status.live_cpu_percent = 31.0;
    status.live_gpu_percent = 72.0;
    status.live_active_corpses = 18;
    status.live_sleeping_corpses = 42;
    status.corpse_limit = 2000;
    model.set_status(status);
    const auto home_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto home_path = output / L"home-goals-96.png";
    CHECK(renderer.value().capture_wic_png(
        home_path, home_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto home_png = decode_png(home_path);
    CHECK(home_png.width == 1440);
    CHECK(home_png.height == 900);
    CHECK(home_png.distinct_colors > 16);

    auto startup_layout = home_layout;
    startup_layout.startup_progress = 0.5F;
    startup_layout.page_transition_progress = 0.5F;
    const auto startup_path = output / L"motion-startup-mid-96.png";
    CHECK(renderer.value().capture_wic_png(
        startup_path, startup_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    CHECK(decode_png(startup_path).distinct_colors > 16);

    status.graphics_available = true;
    status.graphics_values = {
        L"Borderless", L"2560 × 1440", L"Custom", L"Off", L"On",
        L"Ultra", L"Ultra", L"High", L"Ultra", L"16× Anisotropic",
        L"Ultra", L"On", L"FXAA", L"High", L"Off", L"HBAO+",
        L"On", L"On", L"On", L"On", L"Off"};
    status.graphics_aspect_ratio = L"16:9";
    status.graphics_film_grain_percent = 50;
    model.set_status(status);
    static_cast<void>(model.focus_destination(kf2::ui::Destination::graphics));
    static_cast<void>(model.activate_focused());
    const auto graphics_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto graphics_path = output / L"game-graphics-96.png";
    CHECK(renderer.value().capture_wic_png(
        graphics_path, graphics_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto graphics_png = decode_png(graphics_path);
    CHECK(graphics_png.width == 1440);
    CHECK(graphics_png.height == 900);
    CHECK(graphics_png.distinct_colors > 16);

    auto page_motion_layout = graphics_layout;
    page_motion_layout.page_transition_progress = 0.5F;
    const auto first_navigation = std::find_if(
        page_motion_layout.nodes.begin(), page_motion_layout.nodes.end(),
        [](const auto& item) {
            return item.role == kf2::ui::SemanticRole::navigation_item;
        });
    const auto selected_navigation = std::find_if(
        page_motion_layout.nodes.begin(), page_motion_layout.nodes.end(),
        [](const auto& item) {
            return item.role == kf2::ui::SemanticRole::navigation_item &&
                   item.selected;
        });
    CHECK(first_navigation != page_motion_layout.nodes.end());
    CHECK(selected_navigation != page_motion_layout.nodes.end());
    auto moving_indicator = selected_navigation->bounds;
    moving_indicator.y = (first_navigation->bounds.y +
                          selected_navigation->bounds.y) / 2.0F;
    page_motion_layout.navigation_indicator = moving_indicator;
    const auto page_motion_path = output / L"motion-page-nav-mid-96.png";
    CHECK(renderer.value().capture_wic_png(
        page_motion_path, page_motion_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    CHECK(decode_png(page_motion_path).distinct_colors > 16);

    model.set_scroll_extent(graphics_layout.scroll_extent);
    static_cast<void>(model.set_scroll(graphics_layout.scroll_extent));
    const auto graphics_flex_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto graphics_flex_path = output / L"game-graphics-flex-96.png";
    CHECK(renderer.value().capture_wic_png(
        graphics_flex_path, graphics_flex_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto graphics_flex_png = decode_png(graphics_flex_path);
    CHECK(graphics_flex_png.width == 1440);
    CHECK(graphics_flex_png.height == 900);
    CHECK(graphics_flex_png.distinct_colors > 16);

    auto hidden_layout = graphics_flex_layout;
    hidden_layout.exit_progress = 1.0F;
    const auto hidden_path = output / L"animation-hidden.png";
    CHECK(renderer.value().capture_wic_png(
        hidden_path, hidden_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto hidden_png = decode_png(hidden_path);
    CHECK(hidden_png.width == 1440);
    CHECK(hidden_png.height == 900);
    CHECK(hidden_png.distinct_colors == 1);
    CHECK(std::filesystem::remove(hidden_path));

    auto graphics_tooltip_layout = graphics_flex_layout;
    const auto flex_action = std::find_if(
        graphics_tooltip_layout.nodes.begin(),
        graphics_tooltip_layout.nodes.end(), [](const auto& item) {
            return item.action_id == "graphics-flex";
        });
    CHECK(flex_action != graphics_tooltip_layout.nodes.end());
    flex_action->interaction = 0.5F;
    flex_action->hover = 0.75F;
    set_hover_tooltip(graphics_tooltip_layout, &*flex_action, 0.5F);
    CHECK(std::any_of(
        graphics_tooltip_layout.nodes.begin(),
        graphics_tooltip_layout.nodes.end(), [](const auto& item) {
            return item.role == kf2::ui::SemanticRole::tooltip &&
                   item.text.find(L"Adaptive never") != std::wstring::npos;
        }));
    const auto fading_tooltip = std::find_if(
        graphics_tooltip_layout.nodes.begin(),
        graphics_tooltip_layout.nodes.end(), [](const auto& item) {
            return item.role == kf2::ui::SemanticRole::tooltip;
        });
    CHECK(fading_tooltip != graphics_tooltip_layout.nodes.end());
    CHECK(fading_tooltip->opacity == 0.5F);
    const auto graphics_tooltip_path =
        output / L"game-graphics-flex-tooltip-96.png";
    CHECK(renderer.value().capture_wic_png(
        graphics_tooltip_path, graphics_tooltip_layout, theme,
        {1440, 900}, 96.0F).has_value());
    const auto graphics_tooltip_png = decode_png(graphics_tooltip_path);
    CHECK(graphics_tooltip_png.width == 1440);
    CHECK(graphics_tooltip_png.height == 900);
    CHECK(graphics_tooltip_png.distinct_colors > 16);

    status.advanced_available = true;
    status.advanced_dirty = true;
    status.advanced_game_running = false;
    status.advanced_values = {
        L"On", L"On", L"On", L"On", L"On", L"On", L"On", L"Off",
        L"On", L"Off", L"On", L"Off", L"Full", L"100%", L"100%", L"10 seconds"};
    status.advanced_screen_percentage = 100;
    status.advanced_particle_percentage = 100;
    status.advanced_decal_lifetime = 10;
    model.set_status(status);
    model.set_notice({
        kf2::ui::NoticeSeverity::info, L"ADVANCED_APPLIED",
        L"Advanced settings were applied and verified. A restore backup is available.",
        L""});
    static_cast<void>(model.focus_destination(kf2::ui::Destination::advanced));
    static_cast<void>(model.activate_focused());
    model.set_scroll_extent(0.0F);
    const auto advanced_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto advanced_path = output / L"advanced-settings-96.png";
    CHECK(renderer.value().capture_wic_png(
        advanced_path, advanced_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto advanced_png = decode_png(advanced_path);
    CHECK(advanced_png.width == 1440);
    CHECK(advanced_png.height == 900);
    CHECK(advanced_png.distinct_colors > 16);

    model.set_scroll_extent(advanced_layout.scroll_extent);
    static_cast<void>(model.set_scroll(advanced_layout.scroll_extent));
    const auto advanced_save_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto advanced_save_path = output / L"advanced-settings-save-96.png";
    CHECK(renderer.value().capture_wic_png(
        advanced_save_path, advanced_save_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto advanced_save_png = decode_png(advanced_save_path);
    CHECK(advanced_save_png.width == 1440);
    CHECK(advanced_save_png.height == 900);
    CHECK(advanced_save_png.distinct_colors > 16);

    model.clear_notice();
    static_cast<void>(model.focus_destination(kf2::ui::Destination::dashboard));
    static_cast<void>(model.activate_focused());
    model.set_scroll_extent(0.0F);

    status.update_available = true;
    status.update_newer_version_known = true;
    status.update_prompt_visible = true;
    status.update_check_completed = true;
    status.update_installed_version = L"0.0.3-alpha";
    status.update_installable = true;
    status.update_available_version = L"0.0.4-alpha";
    status.update_published_at = L"2026-08-23";
    status.update_download_size = L"5.0 MiB";
    status.update_changelog = L"WHAT'S NEW\n- Simpler interface.";
    model.set_status(status);
    auto update_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    update_layout.update_glow_progress = 0.5F;
    const auto update_path = output / L"update-available-96.png";
    CHECK(renderer.value().capture_wic_png(
        update_path, update_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto update_png = decode_png(update_path);
    CHECK(update_png.width == 1440);
    CHECK(update_png.height == 900);
    CHECK(update_png.distinct_colors > 16);

    CoUninitialize();
    return EXIT_SUCCESS;
}
