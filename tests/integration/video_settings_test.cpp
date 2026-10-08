#include <Windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

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

enum class MutationKind {
    truncate,
    grow_past_limit,
    same_size_rewrite,
    replace,
};

struct MutationPlan {
    MutationKind kind{MutationKind::truncate};
    std::filesystem::path trigger;
    std::filesystem::path target;
    std::filesystem::path replacement;
    std::string payload;
    bool attempted{};
    bool blocked{};
};

MutationPlan* g_mutation_plan{};

void mutate_during_read(const std::filesystem::path& path) {
    if (!g_mutation_plan || g_mutation_plan->attempted ||
        path != g_mutation_plan->trigger) {
        return;
    }
    auto& plan = *g_mutation_plan;
    plan.attempted = true;

    if (plan.kind == MutationKind::replace) {
        const BOOL replaced = ReplaceFileW(
            plan.target.c_str(), plan.replacement.c_str(), nullptr,
            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
        plan.blocked = replaced == FALSE;
        return;
    }

    const HANDLE output = CreateFileW(
        plan.target.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        plan.blocked = true;
        return;
    }

    LARGE_INTEGER position{};
    if (plan.kind == MutationKind::truncate) {
        position.QuadPart = 1;
        plan.blocked = !SetFilePointerEx(output, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(output);
    } else if (plan.kind == MutationKind::grow_past_limit) {
        position.QuadPart = 4LL * 1024LL * 1024LL + 1LL;
        plan.blocked = !SetFilePointerEx(output, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(output);
    } else {
        DWORD written{};
        plan.blocked = !WriteFile(
            output, plan.payload.data(),
            static_cast<DWORD>(plan.payload.size()), &written, nullptr) ||
            written != static_cast<DWORD>(plan.payload.size());
    }
    FlushFileBuffers(output);
    CloseHandle(output);
}

bool rejects_or_blocks_mutation(MutationPlan& plan,
                                const std::filesystem::path& root) {
    g_mutation_plan = &plan;
    kf2::game::set_video_read_hook_for_testing(&mutate_during_read);
    const auto loaded = kf2::game::read_video_settings(root);
    kf2::game::set_video_read_hook_for_testing(nullptr);
    g_mutation_plan = nullptr;
    return plan.attempted && (plan.blocked ? loaded.has_value()
                                           : !loaded.has_value());
}

int check_numeric_video_values(const std::filesystem::path& root,
                               const std::string& system,
                               const std::string& game,
                               const std::string& engine) {
    std::filesystem::create_directories(root);
    struct InvalidValue {
        const wchar_t* key;
        const wchar_t* value;
        bool script{false};
    };
    const InvalidValue invalid_values[] = {
        {L"ResX", L"1920junk"},
        {L"ResY", L"1080.5"},
        {L"ResX", L"2147483648"},
        {L"ResY", L"-2147483649"},
        {L"ResX", L"639"},
        {L"ResY", L"479"},
        {L"ResX", L"16385"},
        {L"ResY", L"0"},
        {L"ResX", L"+"},
        {L"ResX", L""},
        {L"ImageGrainScaler", L"NaN"},
        {L"ImageGrainScaler", L"inf"},
        {L"ImageGrainScaler", L"-inf"},
        {L"ImageGrainScaler", L"1e309"},
        {L"ImageGrainScaler", L"1e-999"},
        {L"ImageGrainScaler", L"1e100"},
        {L"ImageGrainScaler", L"0.49"},
        {L"ImageGrainScaler", L"37.51"},
        {L"ImageGrainScaler", L"0.5junk"},
        {L"ImageGrainScaler", L"0.5e"},
        {L"ImageGrainScaler", L""},
        {L"DetailMode", L"3"},
        {L"DetailMode", L"-1"},
        {L"SkeletalMeshLODBias", L"2"},
        {L"DistanceFogQuality", L"3"},
        {L"MaxAnisotropy", L"0"},
        {L"MaxAnisotropy", L"17"},
        {L"MaxShadowResolution", L"255"},
        {L"MaxShadowResolution", L"4097"},
        {L"BloomQuality", L"3"},
        {L"ShadowTexelsPerPixel", L"NaN"},
        {L"ShadowTexelsPerPixel", L"4.01"},
        {L"DestructionLifetimeScale", L"1.2partial"},
        {L"DestructionLifetimeScale", L"2.01"},
        {L"EmitterPoolScale", L"0.24"},
        {L"EmitterPoolScale", L"4.01"},
        {L"DestructionLifetimeScale", L"NaN", true},
        {L"DestructionLifetimeScale", L"1.2partial", true},
        {L"DestructionLifetimeScale", L"2.01", true},
        {L"EmitterPoolScale", L"inf", true},
        {L"EmitterPoolScale", L"1e-999", true},
        {L"EmitterPoolScale", L"4.01", true},
    };
    for (const auto& invalid : invalid_values) {
        auto changed = kf2::config::IniDocument::parse(
            invalid.script ? game : system);
        CHECK(changed.has_value());
        CHECK(changed.value().upsert(invalid.script ? L"Engine.WorldInfo"
                                                   : L"SystemSettings",
                                     invalid.key, invalid.value).changed);
        const auto system_bytes = invalid.script ? system
            : changed.value().serialize();
        const auto game_bytes = invalid.script ? changed.value().serialize()
                                               : game;
        write_file(root / L"KFSystemSettings.ini", system_bytes);
        write_file(root / L"KFGame.ini", game_bytes);
        write_file(root / L"KFEngine.ini", engine);
        const auto loaded = kf2::game::read_video_settings(root);
        if (loaded.has_value()) {
            std::wcerr << L"Accepted invalid numeric setting: " << invalid.key
                       << L'=' << invalid.value << L'\n';
        }
        CHECK(!loaded.has_value());
        CHECK(loaded.error().code == kf2::ErrorCode::stale_data);
        CHECK(loaded.error().message.find(invalid.key) != std::wstring::npos);
        for (const auto& expected : {
                 std::pair{L"KFSystemSettings.ini", system_bytes},
                 std::pair{L"KFGame.ini", game_bytes},
                 std::pair{L"KFEngine.ini", engine}}) {
            std::ifstream input(root / expected.first, std::ios::binary);
            CHECK(std::string(std::istreambuf_iterator<char>{input}, {}) ==
                  expected.second);
        }
    }

    // Bounds come from the existing configuration catalog and menu readback.
    // Numeric syntax may include surrounding whitespace and a leading sign.
    auto valid = kf2::config::IniDocument::parse(system);
    CHECK(valid.has_value());
    CHECK(valid.value().upsert(L"SystemSettings", L"ResX", L"\t+640\t").changed);
    CHECK(valid.value().upsert(L"SystemSettings", L"ResY", L" +480 ").changed);
    CHECK(valid.value().upsert(L"SystemSettings", L"ImageGrainScaler", L" +3.75e1 ").changed);
    CHECK(valid.value().upsert(L"SystemSettings", L"DistanceFogQuality", L"2").changed);
    CHECK(valid.value().upsert(L"SystemSettings", L"MaxAnisotropy", L"8").changed);
    write_file(root / L"KFSystemSettings.ini", valid.value().serialize());
    write_file(root / L"KFGame.ini", game);
    write_file(root / L"KFEngine.ini", engine);
    auto loaded = kf2::game::read_video_settings(root);
    CHECK(loaded.has_value());
    CHECK(loaded.value().film_grain_percent == 100);
    CHECK(kf2::game::video_choice_label(
        kf2::game::VideoOption::resolution, loaded.value()) == L"640 × 480");
    CHECK(valid.value().upsert(L"SystemSettings", L"ResX", L"16384").changed);
    CHECK(valid.value().upsert(L"SystemSettings", L"ResY", L"16384").changed);
    write_file(root / L"KFSystemSettings.ini", valid.value().serialize());
    CHECK(kf2::game::read_video_settings(root).has_value());

    struct Bounds {
        const wchar_t* key;
        const wchar_t* minimum;
        const wchar_t* maximum;
        bool script{false};
    };
    const Bounds bounds[] = {
        {L"ResX", L"640", L"16384"},
        {L"ResY", L"480", L"16384"},
        {L"ImageGrainScaler", L"0.5", L"37.5"},
        {L"DetailMode", L"0", L"2"},
        {L"SkeletalMeshLODBias", L"0", L"1"},
        {L"DistanceFogQuality", L"0", L"2"},
        {L"MaxAnisotropy", L"1", L"16"},
        {L"MaxShadowResolution", L"256", L"4096"},
        {L"BloomQuality", L"0", L"2"},
        {L"ShadowTexelsPerPixel", L"0.1", L"4.0"},
        {L"DestructionLifetimeScale", L"0.1", L"2.0"},
        {L"EmitterPoolScale", L"0.25", L"4.0"},
        {L"DestructionLifetimeScale", L"0.1", L"2.0", true},
        {L"EmitterPoolScale", L"0.25", L"4.0", true},
    };
    for (const bool maximum : {false, true}) {
        valid = kf2::config::IniDocument::parse(system);
        auto script = kf2::config::IniDocument::parse(game);
        CHECK(valid.has_value() && script.has_value());
        for (const auto& bound : bounds) {
            auto& target = bound.script ? script.value() : valid.value();
            const auto change = target.upsert(
                bound.script ? L"Engine.WorldInfo" : L"SystemSettings",
                bound.key, maximum ? bound.maximum : bound.minimum);
            CHECK(change.shadowed_occurrences == 0);
        }
        write_file(root / L"KFSystemSettings.ini", valid.value().serialize());
        write_file(root / L"KFGame.ini", script.value().serialize());
        loaded = kf2::game::read_video_settings(root);
        CHECK(loaded.has_value());
        CHECK(loaded.value().film_grain_percent == (maximum ? 100 : 0));
    }
    write_file(root / L"KFGame.ini", game);

    // Invalid tuple integers are safely presented as Custom, never as a
    // matching vanilla preset. Existing custom bias values remain supported.
    const auto texture_index = static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution);
    for (const auto* bias : {L"0junk", L"2147483648", L"-2147483649", L"9"}) {
        valid = kf2::config::IniDocument::parse(system);
        CHECK(valid.has_value());
        CHECK(valid.value().upsert(L"SystemSettings", L"TEXTUREGROUP_World",
            std::wstring{L"(LODBias="} + bias +
                L",MinMagFilter=Aniso,MipFilter=Linear)").changed);
        write_file(root / L"KFSystemSettings.ini", valid.value().serialize());
        loaded = kf2::game::read_video_settings(root);
        CHECK(loaded.has_value());
        CHECK(loaded.value().choices[texture_index] == -1);
        const auto preview = kf2::game::build_video_preview(
            root, loaded.value(), &loaded.value());
        CHECK(preview.has_value());
        CHECK(preview.value().files[0].proposed_bytes ==
              valid.value().serialize());
    }
    return EXIT_SUCCESS;
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
    CHECK(check_numeric_video_values(root / L"numeric-values", system, game,
                                    engine) == EXIT_SUCCESS);

    // A graphics snapshot holds all three INIs against writes and replacement
    // until every exact, bounded read and metadata check has completed.
    for (const auto kind : {MutationKind::truncate,
                            MutationKind::grow_past_limit,
                            MutationKind::same_size_rewrite}) {
        MutationPlan plan;
        plan.kind = kind;
        plan.trigger = root / L"KFSystemSettings.ini";
        plan.target = plan.trigger;
        plan.payload.assign(system.size(), 'X');
        CHECK(rejects_or_blocks_mutation(plan, root));
        write_file(root / L"KFSystemSettings.ini", system);
    }

    const auto replacement = root / L"KFSystemSettings.replacement";
    write_file(replacement, std::string(system.size(), 'X'));
    MutationPlan replacement_plan;
    replacement_plan.kind = MutationKind::replace;
    replacement_plan.trigger = root / L"KFSystemSettings.ini";
    replacement_plan.target = replacement_plan.trigger;
    replacement_plan.replacement = replacement;
    CHECK(rejects_or_blocks_mutation(replacement_plan, root));
    write_file(root / L"KFSystemSettings.ini", system);

    // The first file stays protected while later files are read, so a
    // multi-file read cannot silently combine two KF2 write generations.
    MutationPlan generation_plan;
    generation_plan.kind = MutationKind::same_size_rewrite;
    generation_plan.trigger = root / L"KFGame.ini";
    generation_plan.target = root / L"KFSystemSettings.ini";
    generation_plan.payload.assign(system.size(), 'Y');
    CHECK(rejects_or_blocks_mutation(generation_plan, root));
    write_file(root / L"KFSystemSettings.ini", system);

    std::string oversized(4U * 1024U * 1024U + 1U, 'Z');
    write_file(root / L"KFSystemSettings.ini", oversized);
    CHECK(!kf2::game::read_video_settings(root).has_value());
    write_file(root / L"KFSystemSettings.ini", system);
    CHECK(kf2::game::read_video_settings(root).has_value());

    // FleX is part of the verified graphics baseline. Missing, unreadable,
    // partial, malformed, and out-of-range evidence must never become Off.
    const fs::path flex_root = root / L"flex-readback";
    fs::create_directories(flex_root, error);
    CHECK(!error);
    write_file(flex_root / L"KFSystemSettings.ini", system);
    write_file(flex_root / L"KFGame.ini", game);
    CHECK(!kf2::game::read_video_settings(flex_root).has_value());

    const auto read_flex = [&](std::string_view bytes) {
        write_file(flex_root / L"KFEngine.ini", bytes);
        return kf2::game::read_video_settings(flex_root);
    };
    CHECK(!read_flex("[Engine.Engine]\r\nPhysXLe").has_value());
    CHECK(!read_flex("[Engine.Engine]\r\nPhysXLevel=two\r\n").has_value());
    CHECK(!read_flex("[Engine.Engine]\r\nPhysXLevel=2junk\r\n").has_value());
    CHECK(!read_flex("[Engine.Engine]\r\nPhysXLevel=-1\r\n").has_value());
    CHECK(!read_flex("[Engine.Engine]\r\nPhysXLevel=3\r\n").has_value());
    for (int level = 0; level <= 2; ++level) {
        const auto verified = read_flex(
            "[Engine.Engine]\r\nPhysXLevel=" + std::to_string(level) + "\r\n");
        CHECK(verified.has_value());
        CHECK(verified.value().flex_level == level);
        CHECK(verified.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::nvidia_flex)] == level);
    }

    write_file(flex_root / L"KFEngine.ini", engine);
    const HANDLE locked_engine = CreateFileW(
        (flex_root / L"KFEngine.ini").c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked_engine != INVALID_HANDLE_VALUE);
    const auto updating = kf2::game::read_video_settings(flex_root);
    CHECK(!updating.has_value());
    CHECK(updating.error().code == kf2::ErrorCode::stale_data);
    CHECK(CloseHandle(locked_engine) != FALSE);
    CHECK(kf2::game::read_video_settings(flex_root).has_value());

    // Every externally supplied choice must be rejected before it can index
    // one of the preset tables.
    for (std::size_t option_index = 0;
         option_index < kf2::game::kVideoOptionCount; ++option_index) {
        const auto option =
            static_cast<kf2::game::VideoOption>(option_index);
        if (option == kf2::game::VideoOption::overall_quality) continue;
        auto invalid = loaded.value();
        invalid.choices[option_index] =
            kf2::game::video_choice_count(option, invalid);
        CHECK(!kf2::game::build_video_preview(
            root, invalid, &loaded.value()).has_value());
    }
    auto invalid_grain_setting = loaded.value();
    invalid_grain_setting.film_grain_percent = 101;
    CHECK(!kf2::game::build_video_preview(
        root, invalid_grain_setting, &loaded.value()).has_value());
    auto negative_fx_setting = loaded.value();
    negative_fx_setting.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::fx_quality)] = -2;
    CHECK(!kf2::game::build_video_preview(
        root, negative_fx_setting, &loaded.value()).has_value());

    // Overlay compatibility is a display-only delta. Custom texture values
    // must remain byte-for-byte intact while exclusive fullscreen becomes
    // borderless fullscreen.
    const fs::path overlay_root = root / L"overlay-display-delta";
    fs::create_directories(overlay_root, error);
    CHECK(!error);
    auto custom_system = system;
    const auto replace_once = [](std::string& bytes,
                                 std::string_view before,
                                 std::string_view after) {
        const auto position = bytes.find(before);
        if (position == std::string::npos) return false;
        bytes.replace(position, before.size(), after);
        return true;
    };
    CHECK(replace_once(custom_system, "Fullscreen=False", "Fullscreen=True"));
    CHECK(replace_once(custom_system, "Borderless=True", "Borderless=False"));
    CHECK(replace_once(custom_system, "MaxAnisotropy=16", "MaxAnisotropy=4"));
    CHECK(replace_once(
        custom_system,
        "TEXTUREGROUP_World=(LODBias=0,MinMagFilter=Aniso,MipFilter=Linear)",
        "TEXTUREGROUP_World=(LODBias=9,MinMagFilter=Custom,MipFilter=Point)"));
    write_file(overlay_root / L"KFSystemSettings.ini", custom_system);
    write_file(overlay_root / L"KFEngine.ini", engine);
    write_file(overlay_root / L"KFGame.ini", game);
    const auto overlay_baseline =
        kf2::game::read_video_settings(overlay_root);
    CHECK(overlay_baseline.has_value());
    CHECK(overlay_baseline.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution)] == -1);
    CHECK(overlay_baseline.value().choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_filtering)] == -1);
    auto overlay_desired = overlay_baseline.value();
    overlay_desired.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::display)] = 1;
    const auto overlay_preview = kf2::game::build_video_preview(
        overlay_root, overlay_desired, &overlay_baseline.value());
    CHECK(overlay_preview.has_value());
    auto expected_overlay_system = custom_system;
    CHECK(replace_once(
        expected_overlay_system, "Fullscreen=True", "Fullscreen=False"));
    CHECK(replace_once(
        expected_overlay_system, "Borderless=False", "Borderless=True"));
    CHECK(overlay_preview.value().files[0].proposed_bytes ==
          expected_overlay_system);
    CHECK(overlay_preview.value().files[1].proposed_bytes == engine);
    CHECK(overlay_preview.value().files[2].proposed_bytes == game);

    const std::string menu_line =
        "[12.3] ScriptLog: KF2OPT_GFX_MENU schema=2 state=applied "
        "resx=2560 resy=1440 display_full=0 display_borderless=1 "
        "vsync=0 variable_fps=0 film_grain=25 environment=-1 character=-1 fx=1 "
        "texture_resolution=1 texture_filtering=-1 shadows=1 reflections=0 "
        "aa=1 bloom=1 motion_blur=0 ao=0 dof=0 volumetric=0 "
        "lens_flares=0 light_shafts=0 flex=0";
    const auto menu_readback =
        kf2::game::parse_game_menu_graphics_readback(menu_line);
    CHECK(menu_readback.has_value());
    CHECK(menu_readback->resolution.width == 2560);
    CHECK(menu_readback->film_grain_percent == 25);
    CHECK(menu_readback->choices[static_cast<std::size_t>(
        kf2::game::VideoOption::environment_detail)] == -1);
    auto pending = loaded.value();
    pending.film_grain_percent = 75;
    const auto menu_presented = kf2::game::present_game_menu_graphics_readback(
        pending, *menu_readback);
    CHECK(loaded.value().film_grain_percent == 0);
    CHECK(menu_presented.film_grain_percent == 25);
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
        "[19.37] ScriptLog: KF2OPT_GFX_MENU schema=2 state=applied "
        "resx=2560 resy=1440 display_full=0 display_borderless=1 "
        "vsync=0 variable_fps=0 film_grain=67 environment=1 character=0 fx=1 "
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
    CHECK(medium_presented.film_grain_percent == 67);
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        std::string{menu_line}.replace(
            menu_line.find("schema=2"), 8, "schema=1")).has_value());
    auto invalid_grain = menu_line;
    invalid_grain.replace(invalid_grain.find("film_grain=25"), 13,
                          "film_grain=101");
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        invalid_grain).has_value());
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        menu_line + " shadows=3").has_value());
    auto malformed_menu = menu_line;
    malformed_menu.replace(malformed_menu.find("flex=0"), 6, "flex=9");
    CHECK(!kf2::game::parse_game_menu_graphics_readback(
        malformed_menu).has_value());
    // Menu deltas retain only explicitly edited fields, not the other live
    // Adaptive values carried by the same readback. Multiple edits fold into
    // one bounded record without losing a choice changed back to its start.
    const auto before_payload = menu_line.substr(menu_line.find("resx="));
    auto after_payload = before_payload;
    after_payload.replace(after_payload.find("motion_blur=0"), 13, "motion_blur=1");
    const auto edit_line = "ScriptLog: KF2OPT_GFX_USER schema=2 state=applied before " +
        before_payload + " after " + after_payload;
    const auto edit = kf2::game::parse_game_menu_graphics_changes(edit_line);
    CHECK(edit.has_value());
    CHECK(!kf2::game::parse_game_menu_graphics_changes(menu_line));
    CHECK(!kf2::game::parse_game_menu_graphics_readback(edit_line));
    CHECK(!kf2::game::parse_game_menu_graphics_changes(edit_line + " flex=0"));
    CHECK(!kf2::game::parse_game_menu_graphics_changes(edit_line.substr(0, edit_line.size() - 1)));
    CHECK(!kf2::game::parse_game_menu_graphics_changes(std::string(4096, 'x') + edit_line));
    const auto motion_index = static_cast<std::size_t>(kf2::game::VideoOption::motion_blur);
    const auto fx_index = static_cast<std::size_t>(kf2::game::VideoOption::fx_quality);
    CHECK(edit->changed[motion_index]);
    CHECK(!edit->changed[fx_index] && !edit->film_grain_changed);
    auto personal = pending;
    personal.choices[fx_index] = 3;
    const auto edited = kf2::game::apply_game_menu_graphics_changes(personal, *edit);
    CHECK(edited.choices[motion_index] == 1);
    CHECK(edited.choices[fx_index] == 3);
    CHECK(edited.film_grain_percent == personal.film_grain_percent);
    CHECK(edited.resolutions == personal.resolutions);
    auto next_payload = after_payload;
    next_payload.replace(next_payload.find("film_grain=25"), 13, "film_grain=80");
    next_payload.replace(next_payload.find("motion_blur=1"), 13, "motion_blur=0");
    const auto second = kf2::game::parse_game_menu_graphics_changes(
        "KF2OPT_GFX_USER schema=2 state=applied before " + after_payload +
        " after " + next_payload);
    CHECK(second.has_value());
    std::optional<kf2::game::GameMenuGraphicsChanges> merged;
    kf2::game::merge_game_menu_graphics_changes(merged, *edit);
    kf2::game::merge_game_menu_graphics_changes(merged, *second);
    CHECK(merged->changed[motion_index] && merged->film_grain_changed);
    const auto twice_edited = kf2::game::apply_game_menu_graphics_changes(personal, *merged);
    CHECK(twice_edited.choices[motion_index] == 0);
    CHECK(twice_edited.choices[fx_index] == 3);
    CHECK(twice_edited.film_grain_percent == 80);
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

    auto custom_before_reset = loaded.value();
    custom_before_reset.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::nvidia_flex)] = 2;
    custom_before_reset.flex_level = 2;
    custom_before_reset.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::vsync)] = 1;
    custom_before_reset.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::variable_frame_rate)] = 0;
    custom_before_reset.film_grain_percent = 75;
    const auto defaults =
        kf2::game::recommended_video_defaults(custom_before_reset);
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::display, defaults) ==
          L"Borderless fullscreen");
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::resolution, defaults) ==
          kf2::game::video_choice_label(
              kf2::game::VideoOption::resolution, loaded.value()));
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::overall_quality)] == 1);
    CHECK(kf2::game::video_choice_label(
              kf2::game::VideoOption::overall_quality, defaults) ==
          L"Medium");
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::depth_of_field)] == 0);
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::nvidia_flex)] == 2);
    CHECK(defaults.flex_level == 2);
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::vsync)] == 0);
    CHECK(defaults.choices[static_cast<std::size_t>(
              kf2::game::VideoOption::variable_frame_rate)] == 1);
    CHECK(defaults.film_grain_percent == 0);
    // Reset is KF2's exact shipped Medium preset, not an Optimizer-created
    // "Balanced" startup profile. Adaptive never calls this user action.
    constexpr std::array<std::pair<kf2::game::VideoOption, int>, 15>
        vanilla_medium{{
            {kf2::game::VideoOption::environment_detail, 1},
            {kf2::game::VideoOption::character_detail, 0},
            {kf2::game::VideoOption::fx_quality, 1},
            {kf2::game::VideoOption::texture_resolution, 1},
            {kf2::game::VideoOption::texture_filtering, 1},
            {kf2::game::VideoOption::shadow_quality, 1},
            {kf2::game::VideoOption::realtime_reflections, 0},
            {kf2::game::VideoOption::anti_aliasing, 1},
            {kf2::game::VideoOption::bloom, 1},
            {kf2::game::VideoOption::motion_blur, 0},
            {kf2::game::VideoOption::ambient_occlusion, 0},
            {kf2::game::VideoOption::depth_of_field, 0},
            {kf2::game::VideoOption::volumetric_lighting, 0},
            {kf2::game::VideoOption::lens_flares, 0},
            {kf2::game::VideoOption::light_shafts, 0},
        }};
    for (const auto& [option, expected] : vanilla_medium) {
        CHECK(defaults.choices[static_cast<std::size_t>(option)] == expected);
    }

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
              L"SystemSettings", L"MaxAnisotropy") == L"1");
    CHECK(default_system.value().find(
              L"SystemSettings",
              L"MaxWholeSceneDominantShadowResolution") == L"1204");
    CHECK(default_system.value().find(
              L"SystemSettings", L"MaxDeadBodies") == L"550");
    const auto default_engine = kf2::config::IniDocument::parse(
        defaults_preview.value().files[1].proposed_bytes);
    CHECK(default_engine.has_value());
    CHECK(default_engine.value().find(L"Engine.Engine", L"PhysXLevel") ==
          L"2");
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
              kf2::game::VideoOption::texture_resolution)] == 1);
    CHECK(reloaded_defaults.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::texture_filtering)] == 1);
    CHECK(reloaded_defaults.value().choices[static_cast<std::size_t>(
              kf2::game::VideoOption::overall_quality)] == 1);

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
    CHECK(grain_only_system.value().find(L"SystemSettings", L"MaxAnisotropy") == L"1");

    auto only_texture_resolution = reloaded_defaults.value();
    only_texture_resolution.choices[static_cast<std::size_t>(
        kf2::game::VideoOption::texture_resolution)] = 0;
    const auto texture_only_preview = kf2::game::build_video_preview(
        root, only_texture_resolution, &reloaded_defaults.value());
    CHECK(texture_only_preview.has_value());
    const auto texture_only_system = kf2::config::IniDocument::parse(
        texture_only_preview.value().files[0].proposed_bytes);
    CHECK(texture_only_system.has_value());
    CHECK(texture_only_system.value().find(L"SystemSettings", L"MaxAnisotropy") == L"1");
    CHECK(texture_only_system.value().find(L"SystemSettings", L"TEXTUREGROUP_World") ==
          L"(LODBias=2,MinMagFilter=Linear,MipFilter=Linear)");

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

    const fs::path malformed_groups_root = root / L"malformed-texture-groups";
    fs::create_directories(malformed_groups_root, error);
    CHECK(!error);
    write_file(malformed_groups_root / L"KFEngine.ini", engine);
    write_file(malformed_groups_root / L"KFGame.ini", game);
    const auto request_texture_change = [&]() {
        auto baseline = kf2::game::read_video_settings(malformed_groups_root);
        if (!baseline.has_value()) {
            return kf2::Result<kf2::config::ConfigPreview>::failure(
                baseline.error());
        }
        auto desired = baseline.value();
        desired.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::texture_resolution)] = 0;
        desired.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::texture_filtering)] = 1;
        return kf2::game::build_video_preview(
            malformed_groups_root, desired, &baseline.value());
    };

    write_file(malformed_groups_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nMaxAnisotropy=4\r\n"
        "TEXTUREGROUP_World=(LODBias=1,MinMagFilter=Aniso,MipFilter=Linear\r\n");
    CHECK(!request_texture_change().has_value());

    // Repairing the tuple makes the exact same request succeed on retry.
    write_file(malformed_groups_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nMaxAnisotropy=4\r\n"
        "TEXTUREGROUP_World=(LODBias=1,MinMagFilter=Aniso,MipFilter=Linear)\r\n");
    CHECK(request_texture_change().has_value());

    write_file(malformed_groups_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nMaxAnisotropy=4\r\n"
        "TEXTUREGROUP_World=(LODBias=1 MinMagFilter=Aniso,MipFilter=Linear)\r\n");
    CHECK(!request_texture_change().has_value());

    // Missing controlled fields in an otherwise valid tuple are inserted and
    // verified after serialization.
    write_file(malformed_groups_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nMaxAnisotropy=4\r\n"
        "TEXTUREGROUP_World=(NumStreamedMips=3)\r\n");
    const auto missing_fields_preview = request_texture_change();
    CHECK(missing_fields_preview.has_value());
    const auto missing_fields_document = kf2::config::IniDocument::parse(
        missing_fields_preview.value().files[0].proposed_bytes);
    CHECK(missing_fields_document.has_value());
    CHECK(missing_fields_document.value().find(
        L"SystemSettings", L"TEXTUREGROUP_World") ==
          L"(NumStreamedMips=3,LODBias=2,MinMagFilter=Linear,MipFilter=Linear)");

    write_file(malformed_groups_root / L"KFSystemSettings.ini",
        "[SystemSettings]\r\nMaxAnisotropy=4\r\n"
        "TEXTUREGROUP_World=(LODBias=1,MinMagFilter=Aniso,MipFilter=Linear)\r\n"
        "TEXTUREGROUP_Character=(LODBias=1,MinMagFilter=Aniso,MipFilter=Linear\r\n");
    CHECK(!request_texture_change().has_value());

    // Every named preset must survive an INI write/read round-trip. In
    // particular, Ultra and Low must not come back as Custom.
    for (int preset : {0, 1, 2, 3}) {
        auto desired = reloaded_defaults.value();
        CHECK(kf2::game::apply_overall_quality_preset(desired, preset));
        const auto preset_preview = kf2::game::build_video_preview(root, desired);
        CHECK(preset_preview.has_value());
        for (const auto& file : preset_preview.value().files) {
            write_file(root / file.relative_path, file.proposed_bytes);
        }
        const auto reread = kf2::game::read_video_settings(root);
        CHECK(reread.has_value());
        CHECK(reread.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::overall_quality)] == preset);

        kf2::game::GameMenuGraphicsReadback native{
            .choices = desired.choices,
            .resolution = desired.resolutions[static_cast<std::size_t>(
                desired.choices[static_cast<std::size_t>(
                    kf2::game::VideoOption::resolution)])],
            .film_grain_percent = 71};
        const auto presented = kf2::game::present_game_menu_graphics_readback(
            desired, native);
        CHECK(presented.choices[static_cast<std::size_t>(
            kf2::game::VideoOption::overall_quality)] == preset);
        CHECK(presented.film_grain_percent == 71);

        // A mismatch in any quality component remains Custom on both paths,
        // with their intentionally distinct native (-1) and INI (4) values.
        for (const auto component : {
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
             kf2::game::VideoOption::light_shafts}) {
            const auto slot = static_cast<std::size_t>(component);
            auto custom = desired;
            custom.choices[slot] = (custom.choices[slot] + 1) %
                kf2::game::video_choice_count(component, custom);
            auto custom_native = native;
            custom_native.choices[slot] = custom.choices[slot];
            const auto custom_presented =
                kf2::game::present_game_menu_graphics_readback(
                    desired, custom_native);
            CHECK(custom_presented.choices[static_cast<std::size_t>(
                kf2::game::VideoOption::overall_quality)] == -1);
            CHECK(custom_presented.film_grain_percent == 71);
            CHECK(custom_presented.choices[static_cast<std::size_t>(
                kf2::game::VideoOption::nvidia_flex)] ==
                native.choices[static_cast<std::size_t>(
                    kf2::game::VideoOption::nvidia_flex)]);

            const auto custom_preview = kf2::game::build_video_preview(root, custom);
            CHECK(custom_preview.has_value());
            for (const auto& file : custom_preview.value().files) {
                write_file(root / file.relative_path, file.proposed_bytes);
            }
            const auto custom_reread = kf2::game::read_video_settings(root);
            CHECK(custom_reread.has_value());
            CHECK(custom_reread.value().choices[static_cast<std::size_t>(
                kf2::game::VideoOption::overall_quality)] == 4);
        }
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
