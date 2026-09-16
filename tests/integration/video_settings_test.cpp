#include <array>
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
        "UseVsync=False\r\nImageGrainScaler=0.5\r\nMaxAnisotropy=16\r\n"
        "DetailMode=2\r\nSkeletalMeshLODBias=0\r\nAllowSubsurfaceScattering=True\r\n"
        "MaxDeadBodies=550\r\nMaxShadowResolution=1536\r\nShadowTexelsPerPixel=2.0\r\n"
        "Bloom=True\r\nBloomQuality=2\r\nMotionBlur=True\r\nAmbientOcclusion=True\r\n"
        "HBAO=True\r\nDepthOfField=True\r\nLightCones=True\r\n"
        "bAllowTemporalAA=True\r\n"
        "bAllowLensFlares=True\r\nbAllowLightShafts=True\r\n"
        "TEXTUREGROUP_World=(LODBias=0,MinMagFilter=Aniso,MipFilter=Linear)\r\n"
        "TEXTUREGROUP_Character=(LODBias=0,MinMagFilter=Aniso,MipFilter=Linear)\r\n";
    const std::string engine = "[Engine.Engine]\r\nPhysXLevel=0\r\n";
    const std::string game =
        "[KFGame.KFGameEngine]\r\nbSmoothFrameRate=False\r\n";
    write_file(root / L"KFSystemSettings.ini", system);
    write_file(root / L"KFEngine.ini", engine);
    write_file(root / L"KFGame.ini", game);

    auto loaded = kf2::game::read_video_settings(root);
    CHECK(loaded.has_value());
    const std::string menu_line =
        "[12.3] ScriptLog: KF2OPT_GFX_MENU schema=1 state=applied "
        "resx=2560 resy=1440 display_full=0 display_borderless=1 "
        "vsync=0 variable_fps=0 environment=-1 character=-1 fx=1 "
        "texture_resolution=1 texture_filtering=-1 shadows=1 reflections=0 "
        "aa=1 bloom=1 motion_blur=0 ao=0 dof=0 volumetric=0 "
        "lens_flares=0 light_shafts=0 flex=0";
    const auto menu_readback =
        kf2::game::parse_game_menu_graphics_readback(menu_line);
    CHECK(menu_readback.has_value());
    CHECK(menu_readback->resolution.width == 2560);
    CHECK(menu_readback->choices[static_cast<std::size_t>(
        kf2::game::VideoOption::environment_detail)] == -1);
    const auto menu_presented = kf2::game::present_game_menu_graphics_readback(
        loaded.value(), *menu_readback);
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::environment_detail, menu_presented) ==
        L"Custom");
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::motion_blur, menu_presented) == L"Off");
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::nvidia_flex, menu_presented) == L"Off");

    // Exact readback captured from KF2's native Medium preset. Overall
    // quality must match KF2 even though FleX remains an independent control.
    const std::string medium_menu_line =
        "[19.37] ScriptLog: KF2OPT_GFX_MENU schema=1 state=applied "
        "resx=2560 resy=1440 display_full=0 display_borderless=1 "
        "vsync=0 variable_fps=0 environment=1 character=0 fx=1 "
        "texture_resolution=1 texture_filtering=1 shadows=1 reflections=0 "
        "aa=1 bloom=1 motion_blur=0 ao=0 dof=0 volumetric=0 "
        "lens_flares=0 light_shafts=0 flex=0";
    const auto medium_readback =
        kf2::game::parse_game_menu_graphics_readback(medium_menu_line);
    CHECK(medium_readback.has_value());
    const auto medium_presented =
        kf2::game::present_game_menu_graphics_readback(
            loaded.value(), *medium_readback);
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::overall_quality, medium_presented) ==
        L"Medium");
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        menu_line + " shadows=3").has_value());
    auto malformed_menu = menu_line;
    malformed_menu.replace(malformed_menu.find("flex=0"), 6, "flex=9");
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        malformed_menu).has_value());
    const auto variable_enabled =
        kf2::game::read_variable_frame_rate_enabled(root);
    CHECK(variable_enabled.has_value());
    CHECK(variable_enabled.value());
    CHECK(loaded.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::nvidia_flex)] == 0);
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::display, loaded.value()) ==
          L"Borderless fullscreen");
    CHECK(kf2::game::aspect_ratio_label(loaded.value()) == L"16:9");
    CHECK(loaded.value().film_grain_percent == 0);

    const auto defaults = kf2::game::recommended_video_defaults(loaded.value());
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::display, defaults) ==
          L"Borderless fullscreen");
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

    // Values below come from KF2's own Graphics-menu defaultproperties.
    // Matching only the label while writing another number is not 1:1.
    for (int level = 0; level < 4; ++level) {
        auto selected = loaded.value();
        selected.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::shadow_quality)] = level;
        const auto shadow_preview = kf2::game::build_video_preview(
            root, selected);
        CHECK(shadow_preview.has_value());
        const auto shadow_ini = kf2::config::IniDocument::parse(
            shadow_preview.value().files[0].proposed_bytes);
        CHECK(shadow_ini.has_value());
        constexpr std::array<std::wstring_view, 4> vanilla{L"1204", L"1204", L"1280", L"2048"};
        CHECK(shadow_ini.value().find(
            L"SystemSettings", L"MaxWholeSceneDominantShadowResolution") ==
            vanilla[level]);
    }
    auto minimum_grain = loaded.value();
    minimum_grain.film_grain_percent = 0;
    const auto grain_preview = kf2::game::build_video_preview(
        root, minimum_grain);
    CHECK(grain_preview.has_value());
    const auto grain_ini = kf2::config::IniDocument::parse(
        grain_preview.value().files[0].proposed_bytes);
    CHECK(grain_ini.has_value());
    CHECK(grain_ini.value().find(L"SystemSettings", L"ImageGrainScaler") ==
          L"0.50");

    // KF2's menu combines native SystemSettings with class-config values.
    // Writing only SystemSettings must not masquerade as an applied preset.
    auto script_preset = loaded.value();
    script_preset.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::environment_detail)] = 1;
    script_preset.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::fx_quality)] = 0;
    const auto script_preview = kf2::game::build_video_preview(
        root, script_preset, &loaded.value());
    CHECK(script_preview.has_value());
    const auto script_game = kf2::config::IniDocument::parse(
        script_preview.value().files[2].proposed_bytes);
    CHECK(script_game.has_value());
    CHECK(script_game.value().find(
        L"Engine.WorldInfo", L"DestructionLifetimeScale") == L"0.5");
    CHECK(script_game.value().find(
        L"Engine.WorldInfo", L"EmitterPoolScale") == L"0.25");
    CHECK(script_game.value().find(
        L"KFGame.KFMuzzleFlash", L"ShellEjectLifetime") == L"2");
    CHECK(script_game.value().find(
        L"KFGame.KFGoreManager", L"MaxBloodEffects") == L"12");

    auto explicit_flex = loaded.value();
    explicit_flex.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::nvidia_flex)] = 2;

    // Temporary protected-session values can already be staged when the user
    // changes FleX. Rebase only the explicit user delta onto the original
    // settings so session-only changes are not accidentally made permanent.
    auto adaptive_staged = loaded.value();
    adaptive_staged.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::shadow_quality)] = 0;
    auto desired_after_staging = adaptive_staged;
    desired_after_staging.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::nvidia_flex)] = 2;
    desired_after_staging.film_grain_percent = 25;
    const auto rebased = kf2::game::rebase_video_changes(
        loaded.value(), adaptive_staged, desired_after_staging);
    CHECK(rebased.has_value());
    CHECK(rebased.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::nvidia_flex)] == 2);
    CHECK(rebased.value().flex_level == 2);
    CHECK(rebased.value().film_grain_percent == 25);
    CHECK(rebased.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::shadow_quality)] ==
          loaded.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::shadow_quality)]);

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
    CHECK(proposed_system.value().find(
              L"SystemSettings", L"MaxAnisotropy") == L"16");
    CHECK(proposed_system.value().find(
              L"SystemSettings", L"bAllowTemporalAA") == L"False");

    auto defaults_preview = kf2::game::build_video_preview(root, defaults);
    CHECK(defaults_preview.has_value());
    const auto default_system = kf2::config::IniDocument::parse(
        defaults_preview.value().files[0].proposed_bytes);
    CHECK(default_system.has_value());
    CHECK(default_system.value().find(
              L"SystemSettings", L"MaxAnisotropy") == L"4");
    CHECK(default_system.value().find(
              L"SystemSettings",
              L"MaxWholeSceneDominantShadowResolution") == L"1280");
    for (const auto& file : defaults_preview.value().files) {
        write_file(root / file.relative_path, file.proposed_bytes);
    }
    const auto variable_after_defaults =
        kf2::game::read_variable_frame_rate_enabled(root);
    CHECK(variable_after_defaults.has_value());
    CHECK(variable_after_defaults.value());
    auto capped_game = defaults_preview.value().files[2].proposed_bytes;
    const auto uncapped_setting = capped_game.find("bSmoothFrameRate=False");
    CHECK(uncapped_setting != std::string::npos);
    capped_game.replace(
        uncapped_setting, std::string_view{"bSmoothFrameRate=False"}.size(),
        "bSmoothFrameRate=True");
    write_file(root / L"KFGame.ini", capped_game);
    const auto capped = kf2::game::read_variable_frame_rate_enabled(root);
    CHECK(capped.has_value());
    CHECK(!capped.value());
    const auto reloaded_defaults = kf2::game::read_video_settings(root);
    CHECK(reloaded_defaults.has_value());
    CHECK(reloaded_defaults.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::texture_resolution)] == 2);
    CHECK(reloaded_defaults.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::texture_filtering)] == 2);
    CHECK(reloaded_defaults.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::overall_quality)] == 2);

    // KF2 can rewrite ResX/ResY while keeping the selected list index at 0.
    // Compare the physical resolution, not that index, when rebasing/saving.
    const auto resolution_index = static_cast<std::size_t>(
        kf2::game::VideoOption::resolution);
    auto native_resolution = reloaded_defaults.value();
    native_resolution.resolutions[static_cast<std::size_t>(
        native_resolution.choices[resolution_index])] = {1920, 1080};
    const auto rebased_resolution = kf2::game::rebase_video_changes(
        reloaded_defaults.value(), reloaded_defaults.value(), native_resolution);
    CHECK(rebased_resolution.has_value());
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::resolution, rebased_resolution.value()) ==
          L"1920 × 1080");
    const auto resolution_preview = kf2::game::build_video_preview(
        root, rebased_resolution.value(), &reloaded_defaults.value());
    CHECK(resolution_preview.has_value());
    const auto resolution_ini = kf2::config::IniDocument::parse(
        resolution_preview.value().files[0].proposed_bytes);
    CHECK(resolution_ini.has_value());
    CHECK(resolution_ini.value().find(L"SystemSettings", L"ResX") == L"1920");
    CHECK(resolution_ini.value().find(L"SystemSettings", L"ResY") == L"1080");

    // Saving one setting must not canonicalize unrelated INI keys or turn
    // off the user's chosen FleX/variable-frame-rate settings.
    auto only_grain = reloaded_defaults.value();
    only_grain.film_grain_percent = 37;
    const auto grain_only_preview = kf2::game::build_video_preview(
        root, only_grain, &reloaded_defaults.value());
    CHECK(grain_only_preview.has_value());
    CHECK(grain_only_preview.value().files[1].proposed_bytes ==
          grain_only_preview.value().files[1].original_bytes);
    CHECK(grain_only_preview.value().files[2].proposed_bytes ==
          grain_only_preview.value().files[2].original_bytes);
    auto grain_only_system = kf2::config::IniDocument::parse(
        grain_only_preview.value().files[0].proposed_bytes);
    CHECK(grain_only_system.has_value());
    CHECK(grain_only_system.value().find(L"SystemSettings", L"ImageGrainScaler") == L"14.19");
    CHECK(grain_only_system.value().find(L"SystemSettings", L"MaxAnisotropy") == L"4");

    auto only_texture_resolution = reloaded_defaults.value();
    only_texture_resolution.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution)] = 0;
    const auto texture_only_preview = kf2::game::build_video_preview(
        root, only_texture_resolution, &reloaded_defaults.value());
    CHECK(texture_only_preview.has_value());
    const auto texture_only_system = kf2::config::IniDocument::parse(
        texture_only_preview.value().files[0].proposed_bytes);
    CHECK(texture_only_system.has_value());
    CHECK(texture_only_system.value().find(L"SystemSettings", L"MaxAnisotropy") == L"4");
    CHECK(texture_only_system.value().find(L"SystemSettings", L"TEXTUREGROUP_World") ==
          L"(LODBias=2,MinMagFilter=Aniso,MipFilter=Linear)");

    // KF2's native Graphics menu controls exactly these texture groups.
    // Other engine groups (for example Vehicle) must remain untouched.
    const fs::path groups_root = root / L"vanilla-texture-groups";
    fs::create_directories(groups_root, error);
    CHECK(!error);
    constexpr std::array<std::pair<std::string_view, int>, 24> groups{{
        {"UI", 0}, {"UIWithMips", 0}, {"UIStreamable", 0},
        {"Shadowmap", 1}, {"Character", 3}, {"CharacterNormalMap", 3},
        {"CharacterSpecular", 3}, {"Creature", 3},
        {"CreatureNormalMap", 3}, {"CreatureSpecular", 3},
        {"Cosmetic", 3}, {"CosmeticNormalMap", 3},
        {"CosmeticSpecular", 3}, {"Weapon", 1},
        {"WeaponNormalMap", 1}, {"WeaponSpecular", 1},
        {"Weapon3rd", 1}, {"Weapon3rdNormalMap", 1},
        {"Weapon3rdSpecular", 1}, {"World", 2},
        {"WorldNormalMap", 2}, {"WorldSpecular", 2},
        {"Effects", 1}, {"EffectsNotFiltered", 1}}};
    std::string group_ini = "[SystemSettings]\r\nImageGrainScaler=0.5\r\n";
    for (const auto& [name, bias] : groups) {
        group_ini += "TEXTUREGROUP_" + std::string{name} +
            "=(LODBias=" + (name == "Creature" ? "9" : "0") +
            ",MinMagFilter=Aniso,MipFilter=Linear)\r\n";
    }
    group_ini += "TEXTUREGROUP_Vehicle=(LODBias=9,MinMagFilter=Aniso,MipFilter=Linear)\r\n";
    write_file(groups_root / L"KFSystemSettings.ini", group_ini);
    write_file(groups_root / L"KFEngine.ini", engine);
    write_file(groups_root / L"KFGame.ini", game);
    const auto group_baseline = kf2::game::read_video_settings(groups_root);
    CHECK(group_baseline.has_value());
    CHECK(group_baseline.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution)] == -1);
    CHECK(group_baseline.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_filtering)] == -1);
    auto group_selected = group_baseline.value();
    group_selected.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution)] = 0;
    group_selected.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_filtering)] = 1;
    const auto group_preview = kf2::game::build_video_preview(
        groups_root, group_selected);
    CHECK(group_preview.has_value());
    const auto group_document = kf2::config::IniDocument::parse(
        group_preview.value().files[0].proposed_bytes);
    CHECK(group_document.has_value());
    for (const auto& [name, bias] : groups) {
        const std::string key = "TEXTUREGROUP_" + std::string{name};
        const bool no_mips = name == "UI" || name == "UIStreamable" ||
                             name == "EffectsNotFiltered";
        const std::string expected = "(LODBias=" + std::to_string(bias) +
            ",MinMagFilter=Linear,MipFilter=" +
            (no_mips ? "Point" : "Linear") + ")";
        const std::wstring wide_key{key.begin(), key.end()};
        const std::wstring wide_expected{expected.begin(), expected.end()};
        CHECK(group_document.value().find(L"SystemSettings",
            wide_key) == wide_expected);
    }
    CHECK(group_document.value().find(L"SystemSettings", L"TEXTUREGROUP_Vehicle") ==
          L"(LODBias=9,MinMagFilter=Aniso,MipFilter=Linear)");

    // Every named preset must survive an INI write/read round-trip. In
    // particular, Ultra and Low must not come back as Custom.
    for (int preset : {0, 3}) {
        auto desired = reloaded_defaults.value();
        desired.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::overall_quality)] = preset;
        constexpr std::array<kf2::game::VideoOption, 15> targets{{
            kf2::game::VideoOption::environment_detail,
            kf2::game::VideoOption::character_detail,
            kf2::game::VideoOption::fx_quality,
            kf2::game::VideoOption::texture_resolution,
            kf2::game::VideoOption::texture_filtering,
            kf2::game::VideoOption::shadow_quality,
            kf2::game::VideoOption::realtime_reflections,
            kf2::game::VideoOption::anti_aliasing,
            kf2::game::VideoOption::bloom,
            kf2::game::VideoOption::motion_blur,
            kf2::game::VideoOption::ambient_occlusion,
            kf2::game::VideoOption::depth_of_field,
            kf2::game::VideoOption::volumetric_lighting,
            kf2::game::VideoOption::lens_flares,
            kf2::game::VideoOption::light_shafts,
        }};
        constexpr std::array<std::array<int, 15>, 2> expected{{
            {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
            {{3,2,3,3,3,3,1,1,2,1,2,1,1,1,1}},
        }};
        for (std::size_t i = 0; i < targets.size(); ++i) {
            desired.choices[static_cast<std::size_t>(targets[i])] =
                expected[preset == 0 ? 0 : 1][i];
        }
        const auto preset_preview = kf2::game::build_video_preview(root, desired);
        CHECK(preset_preview.has_value());
        for (const auto& file : preset_preview.value().files) {
            write_file(root / file.relative_path, file.proposed_bytes);
        }
        const auto reread = kf2::game::read_video_settings(root);
        CHECK(reread.has_value());
        CHECK(reread.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::overall_quality)] == preset);
    }

    for (int level = 0; level < 4; ++level) {
        auto desired = reloaded_defaults.value();
        desired.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::texture_resolution)] = level;
        desired.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::texture_filtering)] = level;
        const auto level_preview = kf2::game::build_video_preview(root, desired);
        CHECK(level_preview.has_value());
        for (const auto& file : level_preview.value().files) {
            write_file(root / file.relative_path, file.proposed_bytes);
        }
        const auto reloaded_level = kf2::game::read_video_settings(root);
        CHECK(reloaded_level.has_value());
        CHECK(reloaded_level.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::texture_resolution)] == level);
        CHECK(reloaded_level.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::texture_filtering)] == level);
    }

    const fs::path class_root = root / L"class-readback";
    fs::create_directories(class_root, error);
    CHECK(!error);
    write_file(class_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nDetailMode=2\r\nDestructionLifetimeScale=1.0\r\n"
        "DistanceFogQuality=1\r\nEmitterPoolScale=2.0\r\n");
    write_file(class_root / L"KFEngine.ini", engine);
    write_file(class_root / L"KFGame.ini",
        "[Engine.WorldInfo]\r\nDestructionLifetimeScale=1.2\r\n"
        "EmitterPoolScale=0.5\r\n");
    const auto class_readback = kf2::game::read_video_settings(class_root);
    CHECK(class_readback.has_value());
    CHECK(class_readback.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::environment_detail)] == 3);
    CHECK(class_readback.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::fx_quality)] == 2);

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
