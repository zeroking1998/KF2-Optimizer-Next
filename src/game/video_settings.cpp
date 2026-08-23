#include "kf2/game/video_settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>

#include "kf2/config/ini_document.hpp"

namespace kf2::game {
namespace {

constexpr std::wstring_view kSystem = L"SystemSettings";
constexpr std::wstring_view kEngine = L"Engine.Engine";
constexpr std::wstring_view kGameEngine = L"KFGame.KFGameEngine";
const std::filesystem::path kSystemFile{L"KFSystemSettings.ini"};
const std::filesystem::path kEngineFile{L"KFEngine.ini"};
const std::filesystem::path kGameFile{L"KFGame.ini"};
constexpr std::array<std::wstring_view, 30> kTextureGroups{{
    L"TEXTUREGROUP_World", L"TEXTUREGROUP_WorldNormalMap",
    L"TEXTUREGROUP_WorldSpecular", L"TEXTUREGROUP_Character",
    L"TEXTUREGROUP_CharacterNormalMap", L"TEXTUREGROUP_CharacterSpecular",
    L"TEXTUREGROUP_Weapon", L"TEXTUREGROUP_WeaponNormalMap",
    L"TEXTUREGROUP_WeaponSpecular", L"TEXTUREGROUP_Vehicle",
    L"TEXTUREGROUP_VehicleNormalMap", L"TEXTUREGROUP_VehicleSpecular",
    L"TEXTUREGROUP_Cinematic", L"TEXTUREGROUP_Effects",
    L"TEXTUREGROUP_EffectsNotFiltered", L"TEXTUREGROUP_Skybox",
    L"TEXTUREGROUP_UI", L"TEXTUREGROUP_Lightmap",
    L"TEXTUREGROUP_Shadowmap", L"TEXTUREGROUP_RenderTarget",
    L"TEXTUREGROUP_MobileFlattened", L"TEXTUREGROUP_ProcBuilding_Face",
    L"TEXTUREGROUP_ProcBuilding_LightMap", L"TEXTUREGROUP_Terrain_Heightmap",
    L"TEXTUREGROUP_Terrain_Weightmap", L"TEXTUREGROUP_ImageBasedReflection",
    L"TEXTUREGROUP_Bokeh", L"TEXTUREGROUP_UIWithMips",
    L"TEXTUREGROUP_UIStreamable", L"TEXTUREGROUP_Creature"}};

std::size_t index(VideoOption option) noexcept {
    return static_cast<std::size_t>(option);
}

bool safe_regular_file(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

Result<std::string> read_file(const std::filesystem::path& path) {
    if (!safe_regular_file(path)) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"KF2 video configuration file is unsafe or missing", 0});
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 4U * 1024U * 1024U) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"KF2 video configuration is too large to read safely", 0});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"KF2 video configuration cannot be opened", 0});
    }
    return Result<std::string>::success({
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}});
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool same(std::optional<std::wstring> value, std::wstring_view expected) {
    return value && lower(*value) == lower(std::wstring{expected});
}

int integer(const config::IniDocument& document, std::wstring_view key,
            int fallback) {
    const auto value = document.find(kSystem, key);
    if (!value) return fallback;
    wchar_t* end{};
    const long result = std::wcstol(value->c_str(), &end, 10);
    return end != value->c_str() ? static_cast<int>(result) : fallback;
}

double number(const config::IniDocument& document, std::wstring_view key,
              double fallback) {
    const auto value = document.find(kSystem, key);
    if (!value) return fallback;
    wchar_t* end{};
    const double result = std::wcstod(value->c_str(), &end);
    return end != value->c_str() ? result : fallback;
}

int bool_choice(const config::IniDocument& document, std::wstring_view key,
                bool inverted = false) {
    const bool enabled = same(document.find(kSystem, key), L"true");
    return static_cast<int>(inverted ? !enabled : enabled);
}

int choice_from_key(const config::IniDocument& document, std::wstring_view key,
                    std::initializer_list<std::wstring_view> values,
                    int fallback = 0) {
    const auto current = document.find(kSystem, key);
    if (!current) return fallback;
    int result = 0;
    for (const auto value : values) {
        if (same(current, value)) return result;
        ++result;
    }
    return fallback;
}

Result<bool> put(config::IniDocument& document, std::wstring_view key,
                 std::wstring_view value) {
    const auto changed = document.upsert(kSystem, key, value);
    if (changed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::stale_data, L"Duplicate KF2 video setting was rejected", 0});
    }
    return Result<bool>::success(changed.changed);
}

Result<bool> put_bool(config::IniDocument& document, std::wstring_view key,
                      bool value) {
    return put(document, key, value ? L"True" : L"False");
}

Result<bool> put_int(config::IniDocument& document, std::wstring_view key,
                     int value) {
    return put(document, key, std::to_wstring(value));
}

Result<bool> put_double(config::IniDocument& document, std::wstring_view key,
                        std::wstring_view value) {
    return put(document, key, value);
}

bool update_tuple_field(std::wstring& tuple, std::wstring_view field,
                        std::wstring_view value) {
    const std::wstring needle = std::wstring{field} + L"=";
    auto start = lower(tuple).find(lower(needle));
    if (start == std::wstring::npos) {
        const auto close = tuple.rfind(L')');
        if (close == std::wstring::npos) return false;
        tuple.insert(close, L"," + needle + std::wstring{value});
        return true;
    }
    start += needle.size();
    auto end = tuple.find_first_of(L",)", start);
    if (end == std::wstring::npos) return false;
    tuple.replace(start, end - start, value);
    return true;
}

std::optional<std::wstring> tuple_field(
    std::wstring_view tuple, std::wstring_view field) {
    const auto normalized = lower(std::wstring{tuple});
    const auto needle = lower(std::wstring{field}) + L"=";
    auto start = normalized.find(needle);
    if (start == std::wstring::npos) return std::nullopt;
    start += needle.size();
    const auto end = normalized.find_first_of(L",)", start);
    auto value = std::wstring{tuple.substr(start, end - start)};
    const auto first = value.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return std::nullopt;
    const auto last = value.find_last_not_of(L" \t");
    return value.substr(first, last - first + 1);
}

std::optional<int> tuple_integer(
    std::wstring_view tuple, std::wstring_view field) {
    const auto value = tuple_field(tuple, field);
    if (!value) return std::nullopt;
    wchar_t* end{};
    const long parsed = std::wcstol(value->c_str(), &end, 10);
    if (end == value->c_str()) return std::nullopt;
    return static_cast<int>(parsed);
}

std::optional<std::array<int, 4>> texture_bias_profile(
    std::wstring_view group) {
    const auto name = lower(std::wstring{group});
    if (name.rfind(L"texturegroup_ui", 0) == 0) return std::nullopt;
    if (name.find(L"character") != std::wstring::npos ||
        name.find(L"creature") != std::wstring::npos) {
        return std::array<int, 4>{3, 2, 1, 0};
    }
    if (name.find(L"world") != std::wstring::npos ||
        name.find(L"terrain") != std::wstring::npos) {
        return std::array<int, 4>{2, 2, 1, 0};
    }
    if (name.find(L"shadowmap") != std::wstring::npos) {
        return std::array<int, 4>{1, 0, 0, 0};
    }
    return std::array<int, 4>{1, 1, 0, 0};
}

int texture_resolution_choice(const config::IniDocument& document) {
    std::array<int, 4> scores{};
    int samples = 0;
    for (const auto group : kTextureGroups) {
        const auto profile = texture_bias_profile(group);
        const auto tuple = document.find(kSystem, group);
        if (!profile || !tuple) continue;
        const auto bias = tuple_integer(*tuple, L"LODBias");
        if (!bias) continue;
        ++samples;
        const int bounded_bias = std::clamp(*bias, -1000, 1000);
        for (std::size_t level = 0; level < scores.size(); ++level) {
            scores[level] += std::abs(bounded_bias - (*profile)[level]);
        }
    }
    if (samples == 0) return 3;
    return static_cast<int>(std::distance(
        scores.begin(), std::min_element(scores.begin(), scores.end())));
}

int texture_filtering_choice(const config::IniDocument& document) {
    const int anisotropy = std::clamp(
        integer(document, L"MaxAnisotropy", 16), 1, 16);
    for (const auto group : kTextureGroups) {
        const auto tuple = document.find(kSystem, group);
        if (!tuple) continue;
        const auto minmag = tuple_field(*tuple, L"MinMagFilter");
        const auto mip = tuple_field(*tuple, L"MipFilter");
        if (same(minmag, L"linear")) {
            if (same(mip, L"point")) return 0;
            if (same(mip, L"linear")) return 1;
        }
        if (same(minmag, L"aniso")) return anisotropy >= 16 ? 3 : 2;
    }
    if (anisotropy >= 16) return 3;
    if (anisotropy >= 4) return 2;
    return 1;
}

Result<bool> update_texture_groups(
    config::IniDocument& document, int resolution, int filtering) {
    bool changed = false;
    for (const auto group : kTextureGroups) {
        auto tuple = document.find(kSystem, group);
        if (!tuple) continue;
        int bias = 0;
        const auto name = lower(std::wstring{group});
        if (name.find(L"character") != std::wstring::npos ||
            name.find(L"creature") != std::wstring::npos) {
            constexpr std::array<int, 4> values{3, 2, 1, 0};
            bias = values[resolution];
        } else if (name.find(L"world") != std::wstring::npos ||
                   name.find(L"terrain") != std::wstring::npos) {
            constexpr std::array<int, 4> values{2, 2, 1, 0};
            bias = values[resolution];
        } else if (name.find(L"shadowmap") != std::wstring::npos) {
            constexpr std::array<int, 4> values{1, 0, 0, 0};
            bias = values[resolution];
        } else if (name.find(L"ui") == std::wstring::npos) {
            constexpr std::array<int, 4> values{1, 1, 0, 0};
            bias = values[resolution];
        }
        update_tuple_field(*tuple, L"LODBias", std::to_wstring(bias));
        constexpr std::array<std::wstring_view, 4> minmag{
            L"Linear", L"Linear", L"Aniso", L"Aniso"};
        constexpr std::array<std::wstring_view, 4> mip{
            L"Point", L"Linear", L"Linear", L"Linear"};
        update_tuple_field(*tuple, L"MinMagFilter", minmag[filtering]);
        update_tuple_field(*tuple, L"MipFilter", mip[filtering]);
        const auto result = put(document, group, *tuple);
        if (!result.has_value()) return result;
        changed = changed || result.value();
    }
    return Result<bool>::success(changed);
}

void add_resolutions(VideoSettings& settings, Resolution current) {
    constexpr std::array<Resolution, 9> common{{
        {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {1920, 1200},
        {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}}};
    settings.resolutions.assign(common.begin(), common.end());
    if (std::none_of(settings.resolutions.begin(), settings.resolutions.end(),
                     [&](const Resolution& value) {
                         return value.width == current.width && value.height == current.height;
                     })) {
        settings.resolutions.push_back(current);
    }
    std::sort(settings.resolutions.begin(), settings.resolutions.end(),
              [](const Resolution& left, const Resolution& right) {
                  return std::tie(left.width, left.height) <
                         std::tie(right.width, right.height);
              });
}

std::wstring choice(std::initializer_list<std::wstring_view> values, int selected) {
    if (selected < 0 || selected >= static_cast<int>(values.size())) return L"Custom";
    return std::wstring{*(values.begin() + selected)};
}

}  // namespace

std::wstring_view video_option_label(VideoOption option) noexcept {
    constexpr std::array<std::wstring_view, kVideoOptionCount> labels{{
        L"Display", L"Resolution", L"Graphics quality", L"Vertical sync",
        L"Variable frame rate", L"Environment detail", L"Character detail",
        L"FX", L"Texture resolution", L"Texture filtering", L"Shadow quality",
        L"Realtime reflections", L"Anti-aliasing", L"Bloom", L"Motion blur",
        L"Ambient occlusion", L"Depth of field", L"Volumetric lighting FX",
        L"Lens flares", L"Light shafts", L"NVIDIA FleX"}};
    return labels[index(option)];
}

std::wstring video_choice_label(VideoOption option, const VideoSettings& settings) {
    const int selected = settings.choices[index(option)];
    switch (option) {
        case VideoOption::display:
            return choice({L"Windowed", L"Borderless", L"Fullscreen"}, selected);
        case VideoOption::resolution: {
            if (selected < 0 || selected >= static_cast<int>(settings.resolutions.size())) return L"Unknown";
            const auto value = settings.resolutions[selected];
            return std::to_wstring(value.width) + L" × " + std::to_wstring(value.height);
        }
        case VideoOption::overall_quality:
        case VideoOption::environment_detail:
        case VideoOption::fx_quality:
        case VideoOption::texture_resolution:
        case VideoOption::shadow_quality:
            return choice({L"Low", L"Medium", L"High", L"Ultra"}, selected);
        case VideoOption::character_detail:
            return choice({L"Low", L"High", L"Ultra"}, selected);
        case VideoOption::texture_filtering:
            return choice({L"Bilinear", L"Trilinear", L"4× Anisotropic", L"16× Anisotropic"}, selected);
        case VideoOption::bloom:
            return choice({L"Off", L"Low", L"High"}, selected);
        case VideoOption::ambient_occlusion:
            return choice({L"Off", L"SSAO", L"HBAO+"}, selected);
        case VideoOption::nvidia_flex:
            return choice({L"Off", L"Gibs", L"Gibs and fluids"}, selected);
        default:
            return choice({L"Off", L"On"}, selected);
    }
}

std::wstring aspect_ratio_label(const VideoSettings& settings) {
    const int selected = settings.choices[index(VideoOption::resolution)];
    if (selected < 0 || selected >= static_cast<int>(settings.resolutions.size())) return L"Unknown";
    const auto value = settings.resolutions[selected];
    if (value.width <= 0 || value.height <= 0) return L"Unknown";
    struct CommonAspectRatio {
        double value;
        std::wstring_view label;
    };
    constexpr std::array common{
        CommonAspectRatio{5.0 / 4.0, L"5:4"},
        CommonAspectRatio{4.0 / 3.0, L"4:3"},
        CommonAspectRatio{3.0 / 2.0, L"3:2"},
        CommonAspectRatio{16.0 / 10.0, L"16:10"},
        CommonAspectRatio{16.0 / 9.0, L"16:9"},
        CommonAspectRatio{21.0 / 9.0, L"21:9"},
        CommonAspectRatio{32.0 / 9.0, L"32:9"},
    };
    const double actual = static_cast<double>(value.width) /
                          static_cast<double>(value.height);
    const auto nearest = std::min_element(
        common.begin(), common.end(), [actual](const auto& left,
                                                const auto& right) {
            return std::abs(actual - left.value) <
                   std::abs(actual - right.value);
        });
    if (nearest != common.end() &&
        std::abs(actual - nearest->value) / nearest->value <= 0.03) {
        return std::wstring{nearest->label};
    }
    const int divisor = std::gcd(value.width, value.height);
    return std::to_wstring(value.width / divisor) + L":" +
           std::to_wstring(value.height / divisor);
}

std::wstring flex_state_label(int level) {
    if (level <= 0) return L"Off (preserved)";
    if (level == 1) return L"Gibs (preserved)";
    return L"Gibs and fluids (preserved)";
}

int video_choice_count(VideoOption option, const VideoSettings& settings) noexcept {
    switch (option) {
        case VideoOption::display: return 3;
        case VideoOption::resolution: return static_cast<int>(settings.resolutions.size());
        case VideoOption::overall_quality:
        case VideoOption::environment_detail:
        case VideoOption::fx_quality:
        case VideoOption::texture_resolution:
        case VideoOption::texture_filtering:
        case VideoOption::shadow_quality: return 4;
        case VideoOption::character_detail:
        case VideoOption::bloom:
        case VideoOption::ambient_occlusion: return 3;
        case VideoOption::nvidia_flex: return 3;
        default: return 2;
    }
}

VideoSettings recommended_video_defaults(const VideoSettings& current) {
    VideoSettings defaults = current;
    const auto set = [&](VideoOption option, int value) {
        defaults.choices[index(option)] = value;
    };

    // Keep the monitor-dependent display mode and resolution. Reset the
    // quality controls to a balanced baseline and leave costly optional FleX
    // effects disabled until the user explicitly enables them again.
    set(VideoOption::overall_quality, 2);
    set(VideoOption::vsync, 0);
    set(VideoOption::variable_frame_rate, 1);
    set(VideoOption::environment_detail, 2);
    set(VideoOption::character_detail, 1);
    set(VideoOption::fx_quality, 2);
    set(VideoOption::texture_resolution, 2);
    set(VideoOption::texture_filtering, 2);
    set(VideoOption::shadow_quality, 2);
    set(VideoOption::realtime_reflections, 0);
    set(VideoOption::anti_aliasing, 1);
    set(VideoOption::bloom, 2);
    set(VideoOption::motion_blur, 0);
    set(VideoOption::ambient_occlusion, 1);
    set(VideoOption::depth_of_field, 1);
    set(VideoOption::volumetric_lighting, 1);
    set(VideoOption::lens_flares, 1);
    set(VideoOption::light_shafts, 1);
    set(VideoOption::nvidia_flex, 0);
    defaults.flex_level = 0;
    defaults.film_grain_percent = 0;
    return defaults;
}

Result<VideoSettings> read_video_settings(const std::filesystem::path& config_root) {
    auto bytes = read_file(config_root / kSystemFile);
    if (!bytes.has_value()) return Result<VideoSettings>::failure(bytes.error());
    auto parsed = config::IniDocument::parse(bytes.value());
    if (!parsed.has_value()) return Result<VideoSettings>::failure(parsed.error());
    const auto& document = parsed.value();
    VideoSettings settings;
    const Resolution current{integer(document, L"ResX", 1920),
                             integer(document, L"ResY", 1080)};
    add_resolutions(settings, current);
    settings.choices[index(VideoOption::resolution)] =
        static_cast<int>(std::find_if(settings.resolutions.begin(), settings.resolutions.end(),
            [&](const Resolution& value) { return value.width == current.width && value.height == current.height; }) -
            settings.resolutions.begin());
    const bool fullscreen = same(document.find(kSystem, L"Fullscreen"), L"true");
    const bool borderless = same(document.find(kSystem, L"Borderless"), L"true");
    settings.choices[index(VideoOption::display)] = fullscreen ? 2 : borderless ? 1 : 0;
    settings.choices[index(VideoOption::vsync)] = bool_choice(document, L"UseVsync");
    auto game_bytes = read_file(config_root / kGameFile);
    if (!game_bytes.has_value()) {
        return Result<VideoSettings>::failure(game_bytes.error());
    }
    auto game_document = config::IniDocument::parse(game_bytes.value());
    if (!game_document.has_value()) {
        return Result<VideoSettings>::failure(game_document.error());
    }
    settings.choices[index(VideoOption::variable_frame_rate)] =
        same(game_document.value().find(kGameEngine, L"bSmoothFrameRate"), L"true") ? 0 : 1;
    settings.film_grain_percent = std::clamp(
        static_cast<int>(number(document, L"ImageGrainScaler", 0.5) *
                         100.0 + 0.5),
        0, 200);
    settings.choices[index(VideoOption::environment_detail)] =
        std::clamp(integer(document, L"DetailMode", 2), 0, 2);
    settings.choices[index(VideoOption::character_detail)] =
        integer(document, L"SkeletalMeshLODBias", 0) > 0 ? 0 :
        bool_choice(document, L"AllowSubsurfaceScattering") ? 2 : 1;
    settings.choices[index(VideoOption::fx_quality)] =
        std::clamp(integer(document, L"DistanceFogQuality", 1) +
                   (number(document, L"EmitterPoolScale", 1.0) > 1.0 ? 2 : 1), 0, 3);
    settings.choices[index(VideoOption::texture_resolution)] =
        texture_resolution_choice(document);
    settings.choices[index(VideoOption::texture_filtering)] =
        texture_filtering_choice(document);
    settings.choices[index(VideoOption::shadow_quality)] =
        integer(document, L"MaxShadowResolution", 1024) >= 1536 ? 3 :
        number(document, L"ShadowTexelsPerPixel", 1.0) >= 1.3 ? 2 :
        bool_choice(document, L"bAllowWholeSceneDominantShadows") ? 1 : 0;
    settings.choices[index(VideoOption::realtime_reflections)] =
        bool_choice(document, L"bAllowScreenSpaceReflections");
    settings.choices[index(VideoOption::anti_aliasing)] = bool_choice(document, L"PostProcessAA");
    settings.choices[index(VideoOption::bloom)] =
        bool_choice(document, L"Bloom") ? std::clamp(integer(document, L"BloomQuality", 1), 1, 2) : 0;
    settings.choices[index(VideoOption::motion_blur)] = bool_choice(document, L"MotionBlur");
    settings.choices[index(VideoOption::ambient_occlusion)] =
        bool_choice(document, L"HBAO") ? 2 : bool_choice(document, L"AmbientOcclusion") ? 1 : 0;
    settings.choices[index(VideoOption::depth_of_field)] = bool_choice(document, L"DepthOfField");
    settings.choices[index(VideoOption::volumetric_lighting)] = bool_choice(document, L"LightCones");
    settings.choices[index(VideoOption::lens_flares)] = bool_choice(document, L"bAllowLensFlares");
    settings.choices[index(VideoOption::light_shafts)] = bool_choice(document, L"bAllowLightShafts");
    constexpr std::array<std::array<int, 15>, 4> overall_presets{{
        {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
        {{1,0,1,1,1,1,0,1,1,0,0,0,0,0,0}},
        {{2,1,2,2,2,2,0,1,2,0,1,1,1,1,1}},
        {{3,2,3,3,3,3,1,1,2,1,2,1,1,1,1}},
    }};
    constexpr std::array<VideoOption, 15> overall_targets{{
        VideoOption::environment_detail, VideoOption::character_detail,
        VideoOption::fx_quality, VideoOption::texture_resolution,
        VideoOption::texture_filtering, VideoOption::shadow_quality,
        VideoOption::realtime_reflections, VideoOption::anti_aliasing,
        VideoOption::bloom, VideoOption::motion_blur,
        VideoOption::ambient_occlusion, VideoOption::depth_of_field,
        VideoOption::volumetric_lighting, VideoOption::lens_flares,
        VideoOption::light_shafts,
    }};
    settings.choices[index(VideoOption::overall_quality)] = 4;
    for (int preset = 0; preset < 4; ++preset) {
        bool match = true;
        for (std::size_t component = 0; component < overall_targets.size(); ++component) {
            match = match && settings.choices[index(overall_targets[component])] ==
                               overall_presets[preset][component];
        }
        if (match) {
            settings.choices[index(VideoOption::overall_quality)] = preset;
            break;
        }
    }

    auto engine_bytes = read_file(config_root / kEngineFile);
    if (engine_bytes.has_value()) {
        auto engine = config::IniDocument::parse(engine_bytes.value());
        if (engine.has_value()) {
            const auto value = engine.value().find(kEngine, L"PhysXLevel");
            if (value) {
                wchar_t* end{};
                const long level = std::wcstol(value->c_str(), &end, 10);
                if (end != value->c_str()) {
                    settings.flex_level = std::clamp(static_cast<int>(level), 0, 2);
                }
            }
        }
    }
    settings.choices[index(VideoOption::nvidia_flex)] = settings.flex_level;
    return Result<VideoSettings>::success(std::move(settings));
}

Result<config::ConfigPreview> build_video_preview(
    const std::filesystem::path& config_root, const VideoSettings& settings) {
    auto bytes = read_file(config_root / kSystemFile);
    if (!bytes.has_value()) return Result<config::ConfigPreview>::failure(bytes.error());
    auto parsed = config::IniDocument::parse(bytes.value());
    if (!parsed.has_value()) return Result<config::ConfigPreview>::failure(parsed.error());
    auto document = std::move(parsed.value());
    const auto selected = [&](VideoOption option) { return settings.choices[index(option)]; };
    bool changed = false;
    const auto apply_result = [&](Result<bool> result) -> bool {
        if (!result.has_value()) return false;
        changed = changed || result.value();
        return true;
    };
    const int display = selected(VideoOption::display);
    if (!apply_result(put_bool(document, L"Fullscreen", display == 2)) ||
        !apply_result(put_bool(document, L"Borderless", display == 1))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Display settings contain duplicates", 0});
    }
    const int resolution = selected(VideoOption::resolution);
    if (resolution < 0 || resolution >= static_cast<int>(settings.resolutions.size())) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::invalid_argument, L"Resolution selection is invalid", 0});
    }
    if (!apply_result(put_int(document, L"ResX", settings.resolutions[resolution].width)) ||
        !apply_result(put_int(document, L"ResY", settings.resolutions[resolution].height)) ||
        !apply_result(put_bool(document, L"UseVsync", selected(VideoOption::vsync) != 0))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Basic video settings contain duplicates", 0});
    }
    const double grain = static_cast<double>(settings.film_grain_percent) / 100.0;
    wchar_t grain_text[32]{};
    swprintf_s(grain_text, L"%.2f", grain);
    if (!apply_result(put_double(document, L"ImageGrainScaler", grain_text))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Film grain setting contains duplicates", 0});
    }

    const int environment = selected(VideoOption::environment_detail);
    constexpr std::array<std::wstring_view, 4> lifetime{L"0.25", L"0.5", L"1.0", L"1.2"};
    if (!apply_result(put_int(document, L"DetailMode", environment < 2 ? environment : 2)) ||
        !apply_result(put_double(document, L"DestructionLifetimeScale", lifetime[environment])) ||
        !apply_result(put_bool(document, L"bDisableCanBecomeDynamicWakeup", environment == 0)) ||
        !apply_result(put_int(document, L"MakeDynamicCollisionThreshold", environment < 2 ? 200 : 150)) ||
        !apply_result(put_bool(document, L"AllowLightFunctions", environment >= 2))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Environment settings contain duplicates", 0});
    }

    const int character = selected(VideoOption::character_detail);
    constexpr std::array<int, 3> wounds{2, 5, 5};
    constexpr std::array<std::wstring_view, 3> kinematic{L"3.0", L"1.3", L"1.0"};
    if (!apply_result(put_int(document, L"SkeletalMeshLODBias", character == 0 ? 1 : 0)) ||
        !apply_result(put_bool(document, L"AllowSubsurfaceScattering", character == 2)) ||
        !apply_result(put_int(document, L"MaxBodyWoundDecals", wounds[character])) ||
        !apply_result(put_double(document, L"KinematicUpdateDistFactorScale", kinematic[character])) ||
        !apply_result(put_bool(document, L"ShouldCorpseCollideWithDead", character > 0)) ||
        !apply_result(put_bool(document, L"ShouldCorpseCollideWithLiving", character > 0)) ||
        !apply_result(put_bool(document, L"ShouldCorpseCollideWithDeadAfterSleep", character == 2)) ||
        !apply_result(put_bool(document, L"bAllowPhysics", character > 0))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Character settings contain duplicates", 0});
    }

    const int fx = selected(VideoOption::fx_quality);
    constexpr std::array<int, 4> particle_bias{1, 0, 0, 0};
    constexpr std::array<int, 4> fog{0, 0, 1, 1};
    constexpr std::array<std::wstring_view, 4> pool{L"0.25", L"0.5", L"1.0", L"2.0"};
    constexpr std::array<int, 4> shell{2, 5, 10, 20};
    constexpr std::array<int, 4> impact{8, 15, 20, 40};
    constexpr std::array<int, 4> explosion{8, 12, 15, 20};
    constexpr std::array<std::wstring_view, 4> gore_lifetime{L"0.5", L"0.75", L"1.0", L"1.2"};
    constexpr std::array<int, 4> blood{12, 15, 25, 40};
    constexpr std::array<int, 4> gore{8, 8, 10, 15};
    constexpr std::array<int, 4> splats{25, 50, 75, 100};
    const bool high_fx = fx >= 2;
    if (!apply_result(put_int(document, L"ParticleLODBias", particle_bias[fx])) ||
        !apply_result(put_int(document, L"DistanceFogQuality", fog[fx])) ||
        !apply_result(put_bool(document, L"Distortion", high_fx)) ||
        !apply_result(put_bool(document, L"FilteredDistortion", high_fx)) ||
        !apply_result(put_bool(document, L"DropParticleDistortion", !high_fx)) ||
        !apply_result(put_double(document, L"EmitterPoolScale", pool[fx])) ||
        !apply_result(put_int(document, L"ShellEjectLifetime", shell[fx])) ||
        !apply_result(put_bool(document, L"AllowExplosionLights", fx > 0)) ||
        !apply_result(put_bool(document, L"AllowSprayActorLights", high_fx)) ||
        !apply_result(put_bool(document, L"AllowPilotLights", fx > 0)) ||
        !apply_result(put_bool(document, L"AllowFootstepSounds", fx > 0)) ||
        !apply_result(put_bool(document, L"AllowRagdollAndGoreOnDeadBodies", fx > 0)) ||
        !apply_result(put_int(document, L"MaxImpactEffectDecals", impact[fx])) ||
        !apply_result(put_int(document, L"MaxExplosionDecals", explosion[fx])) ||
        !apply_result(put_double(document, L"GoreFXLifetimeMultiplier", gore_lifetime[fx])) ||
        !apply_result(put_int(document, L"MaxBloodEffects", blood[fx])) ||
        !apply_result(put_int(document, L"MaxGoreEffects", gore[fx])) ||
        !apply_result(put_bool(document, L"AllowSecondaryBloodEffects", high_fx)) ||
        !apply_result(put_bool(document, L"AllowBloodSplatterDecals", high_fx)) ||
        !apply_result(put_int(document, L"MaxPersistentSplatsPerFrame", splats[fx]))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"FX settings contain duplicates", 0});
    }

    const int shadow = selected(VideoOption::shadow_quality);
    constexpr std::array<int, 4> whole_shadow{1204, 1204, 1280, 2048};
    constexpr std::array<int, 4> max_shadow{1024, 1024, 1024, 1536};
    constexpr std::array<int, 4> fade{256, 128, 128, 64};
    constexpr std::array<int, 4> min_shadow{128, 64, 64, 32};
    constexpr std::array<std::wstring_view, 4> texels{L"0.5", L"1.0", L"1.3", L"2.0"};
    constexpr std::array<std::wstring_view, 4> distance{L"0.75", L"0.75", L"1.0", L"1.5"};
    if (!apply_result(put_bool(document, L"bAllowWholeSceneDominantShadows", shadow > 0)) ||
        !apply_result(put_bool(document, L"bOverrideMapWholeSceneDominantShadowSetting", shadow == 3)) ||
        !apply_result(put_bool(document, L"bAllowDynamicShadows", true)) ||
        !apply_result(put_bool(document, L"bAllowPerObjectShadows", shadow > 0)) ||
        !apply_result(put_int(document, L"MaxWholeSceneDominantShadowResolution", whole_shadow[shadow])) ||
        !apply_result(put_int(document, L"MaxShadowResolution", max_shadow[shadow])) ||
        !apply_result(put_int(document, L"ShadowFadeResolution", fade[shadow])) ||
        !apply_result(put_int(document, L"MinShadowResolution", min_shadow[shadow])) ||
        !apply_result(put_double(document, L"ShadowTexelsPerPixel", texels[shadow])) ||
        !apply_result(put_double(document, L"GlobalShadowDistanceScale", distance[shadow])) ||
        !apply_result(put_bool(document, L"AllowForegroundPreshadows", shadow >= 2))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Shadow settings contain duplicates", 0});
    }

    const int bloom = selected(VideoOption::bloom);
    const int ao = selected(VideoOption::ambient_occlusion);
    if (!apply_result(put_bool(document, L"bAllowScreenSpaceReflections", selected(VideoOption::realtime_reflections) != 0)) ||
        !apply_result(put_bool(document, L"PostProcessAA", selected(VideoOption::anti_aliasing) != 0)) ||
        !apply_result(put_bool(document, L"Bloom", bloom != 0)) ||
        !apply_result(put_int(document, L"BloomQuality", bloom)) ||
        !apply_result(put_bool(document, L"MotionBlur", selected(VideoOption::motion_blur) != 0)) ||
        !apply_result(put_int(document, L"MotionBlurQuality", selected(VideoOption::motion_blur))) ||
        !apply_result(put_bool(document, L"AmbientOcclusion", ao != 0)) ||
        !apply_result(put_bool(document, L"HBAO", ao == 2)) ||
        !apply_result(put_bool(document, L"DepthOfField", selected(VideoOption::depth_of_field) != 0)) ||
        !apply_result(put_int(document, L"DepthOfFieldQuality", selected(VideoOption::depth_of_field))) ||
        !apply_result(put_bool(document, L"LightCones", selected(VideoOption::volumetric_lighting) != 0)) ||
        !apply_result(put_bool(document, L"bAllowLensFlares", selected(VideoOption::lens_flares) != 0)) ||
        !apply_result(put_bool(document, L"bAllowLightShafts", selected(VideoOption::light_shafts) != 0))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Effects settings contain duplicates", 0});
    }
    constexpr std::array<int, 4> anisotropy{1, 1, 4, 16};
    if (!apply_result(put_int(
            document, L"MaxAnisotropy",
            anisotropy[selected(VideoOption::texture_filtering)]))) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Texture filtering setting contains duplicates", 0});
    }
    auto textures = update_texture_groups(
        document, selected(VideoOption::texture_resolution),
        selected(VideoOption::texture_filtering));
    if (!textures.has_value()) return Result<config::ConfigPreview>::failure(textures.error());
    changed = changed || textures.value();
    static_cast<void>(changed);

    auto engine_bytes = read_file(config_root / kEngineFile);
    if (!engine_bytes.has_value()) {
        return Result<config::ConfigPreview>::failure(engine_bytes.error());
    }
    auto engine_parsed = config::IniDocument::parse(engine_bytes.value());
    if (!engine_parsed.has_value()) {
        return Result<config::ConfigPreview>::failure(engine_parsed.error());
    }
    auto engine = std::move(engine_parsed.value());
    const auto flex_changed = engine.upsert(
        kEngine, L"PhysXLevel",
        std::to_wstring(selected(VideoOption::nvidia_flex)));
    if (flex_changed.shadowed_occurrences != 0) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Duplicate FleX setting was rejected", 0});
    }
    auto game_bytes = read_file(config_root / kGameFile);
    if (!game_bytes.has_value()) {
        return Result<config::ConfigPreview>::failure(game_bytes.error());
    }
    auto game_parsed = config::IniDocument::parse(game_bytes.value());
    if (!game_parsed.has_value()) {
        return Result<config::ConfigPreview>::failure(game_parsed.error());
    }
    auto game = std::move(game_parsed.value());
    const auto variable_changed = game.upsert(
        kGameEngine, L"bSmoothFrameRate",
        selected(VideoOption::variable_frame_rate) == 0 ? L"True" : L"False");
    if (variable_changed.shadowed_occurrences != 0) {
        return Result<config::ConfigPreview>::failure(
            {ErrorCode::stale_data, L"Duplicate variable frame-rate setting was rejected", 0});
    }

    config::ConfigPreview preview;
    preview.config_root = config_root;
    preview.files.push_back({kSystemFile, bytes.value(), document.serialize()});
    preview.files.push_back({kEngineFile, engine_bytes.value(), engine.serialize()});
    preview.files.push_back({kGameFile, game_bytes.value(), game.serialize()});
    return Result<config::ConfigPreview>::success(std::move(preview));
}

}  // namespace kf2::game
