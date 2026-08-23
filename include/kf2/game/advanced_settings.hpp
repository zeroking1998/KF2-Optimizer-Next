#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/config/config_preview.hpp"
#include "kf2/core/result.hpp"

namespace kf2::game {

enum class AdvancedOption : std::size_t {
    one_frame_thread_lag,
    per_frame_sleep,
    per_frame_yield,
    background_level_streaming,
    texture_streaming,
    priority_streaming,
    dynamic_streaming,
    hardware_shadow_filtering,
    downsampled_translucency,
    floating_point_render_targets,
    max_multisamples,
    gore_level,
    screen_percentage,
    particle_percentage,
    decal_lifetime,
    count,
};

inline constexpr std::size_t kAdvancedOptionCount =
    static_cast<std::size_t>(AdvancedOption::count);

struct AdvancedGameSettings {
    std::array<config::SettingValue, kAdvancedOptionCount> values{};

    friend bool operator==(const AdvancedGameSettings&,
                           const AdvancedGameSettings&) = default;
};

[[nodiscard]] config::SettingId advanced_setting_id(
    AdvancedOption option) noexcept;
[[nodiscard]] std::wstring_view advanced_option_label(
    AdvancedOption option) noexcept;
[[nodiscard]] std::wstring advanced_value_label(
    AdvancedOption option, const AdvancedGameSettings& settings);
[[nodiscard]] bool advanced_option_is_slider(AdvancedOption option) noexcept;
[[nodiscard]] int advanced_slider_value(
    AdvancedOption option, const AdvancedGameSettings& settings) noexcept;
[[nodiscard]] bool set_advanced_slider_value(
    AdvancedGameSettings& settings, AdvancedOption option, int value) noexcept;
[[nodiscard]] bool cycle_advanced_option(
    AdvancedGameSettings& settings, AdvancedOption option) noexcept;
[[nodiscard]] AdvancedGameSettings recommended_advanced_defaults();

[[nodiscard]] Result<AdvancedGameSettings> read_advanced_game_settings(
    const std::filesystem::path& config_root);
[[nodiscard]] std::vector<config::RequestedChange> advanced_setting_changes(
    const AdvancedGameSettings& saved,
    const AdvancedGameSettings& pending);

}  // namespace kf2::game
