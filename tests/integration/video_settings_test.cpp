#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "kf2/config/ini_document.hpp"
#include "kf2/game/video_settings.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    CHECK(!error);

    const std::string system =
        "[SystemSettings]\r\n"
        "Fullscreen=False\r\nBorderless=True\r\nResX=2560\r\nResY=1440\r\n"
        "UseVsync=False\r\nImageGrainScaler=0.5\r\n"
        "DetailMode=2\r\nSkeletalMeshLODBias=0\r\nAllowSubsurfaceScattering=True\r\n"
        "MaxDeadBodies=550\r\nMaxShadowResolution=1536\r\nShadowTexelsPerPixel=2.0\r\n"
        "Bloom=True\r\nBloomQuality=2\r\nMotionBlur=True\r\nAmbientOcclusion=True\r\n"
        "HBAO=True\r\nDepthOfField=True\r\nLightCones=True\r\n"
        "bAllowLensFlares=True\r\nbAllowLightShafts=True\r\n"
        "TEXTUREGROUP_World=(LODBias=0,MinMagFilter=Aniso,MipFilter=Linear)\r\n";
    const std::string engine = "[Engine.Engine]\r\nPhysXLevel=0\r\n";
    const std::string game =
        "[KFGame.KFGameEngine]\r\nbSmoothFrameRate=False\r\n";
    write_file(root / L"KFSystemSettings.ini", system);
    write_file(root / L"KFEngine.ini", engine);
    write_file(root / L"KFGame.ini", game);

    auto loaded = kf2::game::read_video_settings(root);
    CHECK(loaded.has_value());
    CHECK(loaded.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::nvidia_flex)] == 0);
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::display, loaded.value()) == L"Borderless");
    CHECK(kf2::game::aspect_ratio_label(loaded.value()) == L"16:9");
    CHECK(loaded.value().film_grain_percent == 50);

    const auto defaults = kf2::game::recommended_video_defaults(loaded.value());
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::display, defaults) == L"Borderless");
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::resolution, defaults) ==
          kf2::game::video_choice_label(
              kf2::game::VideoOption::resolution, loaded.value()));
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::overall_quality)] == 2);
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::depth_of_field)] == 1);
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::nvidia_flex)] == 0);
    CHECK(defaults.film_grain_percent == 0);

    auto near_sixteen_nine = loaded.value();
    near_sixteen_nine.resolutions = {{1366, 768}};
    near_sixteen_nine.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::resolution)] = 0;
    CHECK(kf2::game::aspect_ratio_label(near_sixteen_nine) == L"16:9");

    auto ultrawide = loaded.value();
    ultrawide.resolutions = {{2560, 1080}};
    ultrawide.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::resolution)] = 0;
    CHECK(kf2::game::aspect_ratio_label(ultrawide) == L"21:9");

    auto off_preview = kf2::game::build_video_preview(root, loaded.value());
    CHECK(off_preview.has_value());
    CHECK(off_preview.value().files.size() == 3);
    auto off_engine = kf2::config::IniDocument::parse(
        off_preview.value().files[1].proposed_bytes);
    CHECK(off_engine.has_value());
    CHECK(off_engine.value().find(L"Engine.Engine", L"PhysXLevel") == L"0");

    auto explicit_flex = loaded.value();
    explicit_flex.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::nvidia_flex)] = 2;
    auto flex_preview = kf2::game::build_video_preview(root, explicit_flex);
    CHECK(flex_preview.has_value());
    auto flex_engine = kf2::config::IniDocument::parse(
        flex_preview.value().files[1].proposed_bytes);
    CHECK(flex_engine.has_value());
    CHECK(flex_engine.value().find(L"Engine.Engine", L"PhysXLevel") == L"2");

    auto proposed_system = kf2::config::IniDocument::parse(
        flex_preview.value().files[0].proposed_bytes);
    CHECK(proposed_system.has_value());
    CHECK(proposed_system.value().find(L"SystemSettings", L"MaxDeadBodies") ==
          L"550");

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
