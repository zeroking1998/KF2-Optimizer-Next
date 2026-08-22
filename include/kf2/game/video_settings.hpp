#pragma once

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

#include "kf2/config/config_preview.hpp"
#include "kf2/core/result.hpp"

namespace kf2::game {

enum class VideoOption : std::size_t {
    display,
    resolution,
    overall_quality,
    vsync,
    variable_frame_rate,
    environment_detail,
    character_detail,
    fx_quality,
    texture_resolution,
    texture_filtering,
    shadow_quality,
    realtime_reflections,
    anti_aliasing,
    bloom,
    motion_blur,
    ambient_occlusion,
    depth_of_field,
    volumetric_lighting,
    lens_flares,
    light_shafts,
    nvidia_flex,
    count,
};

inline constexpr std::size_t kVideoOptionCount =
    static_cast<std::size_t>(VideoOption::count);

struct Resolution {
    int width{};
    int height{};
};

struct VideoSettings {
    std::array<int, kVideoOptionCount> choices{};
    std::vector<Resolution> resolutions;
    int film_grain_percent{50};
    int flex_level{0};
};

[[nodiscard]] std::wstring_view video_option_label(VideoOption option) noexcept;
[[nodiscard]] std::wstring video_choice_label(
    VideoOption option, const VideoSettings& settings);
[[nodiscard]] std::wstring aspect_ratio_label(const VideoSettings& settings);
[[nodiscard]] std::wstring flex_state_label(int level);
[[nodiscard]] int video_choice_count(
    VideoOption option, const VideoSettings& settings) noexcept;
[[nodiscard]] VideoSettings recommended_video_defaults(
    const VideoSettings& current);

[[nodiscard]] Result<VideoSettings> read_video_settings(
    const std::filesystem::path& config_root);
[[nodiscard]] Result<config::ConfigPreview> build_video_preview(
    const std::filesystem::path& config_root,
    const VideoSettings& settings);

}  // namespace kf2::game
