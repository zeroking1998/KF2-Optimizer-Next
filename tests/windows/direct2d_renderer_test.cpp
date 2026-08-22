#include <Windows.h>
#include <wincodec.h>

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
    status.telemetry = L"143.2 FPS, 7.0 ms";
    status.live_fps = 143.2;
    status.live_frame_time_ms = 7.0;
    status.live_cpu_percent = 31.0;
    status.live_gpu_percent = 72.0;
    status.live_active_corpses = 18;
    status.live_sleeping_corpses = 42;
    status.corpse_limit = 2000;
    model.set_status(status);
    static_cast<void>(model.focus_destination(kf2::ui::Destination::settings));
    static_cast<void>(model.activate_focused());
    const auto settings_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
    const auto settings_path = output / L"settings-96.png";
    CHECK(renderer.value().capture_wic_png(
        settings_path, settings_layout, theme, {1440, 900}, 96.0F)
              .has_value());
    const auto settings_png = decode_png(settings_path);
    CHECK(settings_png.width == 1440);
    CHECK(settings_png.height == 900);
    CHECK(settings_png.distinct_colors > 16);

    status.update_available = true;
    status.update_installable = true;
    status.update_available_version = L"0.0.4-alpha";
    status.update_published_at = L"2026-08-23";
    status.update_download_size = L"5.0 MiB";
    status.update_changelog = L"WHAT'S NEW\n- Simpler interface.";
    model.set_status(status);
    const auto update_layout =
        kf2::ui::layout_shell(model, 1440.0F, 900.0F);
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
