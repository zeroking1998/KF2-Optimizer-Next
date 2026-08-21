#pragma once

#include <map>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "kf2/config/kf2_catalog.hpp"
#include "kf2/core/result.hpp"

namespace kf2::config {

enum class SettingCategory {
    frame_pacing_cpu,
    gore_decals,
    rendering_effects,
    shadows_lighting,
    lod_distance,
    physics_flex,
};

[[nodiscard]] std::span<const SettingCategory> all_setting_categories() noexcept;
[[nodiscard]] SettingCategory setting_category(SettingId id) noexcept;
[[nodiscard]] std::wstring_view setting_category_label(SettingCategory category) noexcept;
[[nodiscard]] std::wstring_view setting_category_label(SettingId id) noexcept;

[[nodiscard]] std::string setting_token(SettingId id);
[[nodiscard]] const SettingDefinition* find_setting_by_token(
    std::string_view token) noexcept;
[[nodiscard]] Result<std::map<SettingId, SettingValue>> read_catalog_values(
    const std::filesystem::path& config_root);

}  // namespace kf2::config
