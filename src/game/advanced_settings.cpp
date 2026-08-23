#include "kf2/game/advanced_settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

#include "kf2/config/ini_document.hpp"

namespace kf2::game {
namespace {

constexpr std::array<config::SettingId, kAdvancedOptionCount> kSettingIds{{
    config::SettingId::one_frame_thread_lag,
    config::SettingId::per_frame_sleep,
    config::SettingId::per_frame_yield,
    config::SettingId::background_level_streaming,
    config::SettingId::texture_streaming,
    config::SettingId::priority_streaming,
    config::SettingId::dynamic_streaming,
    config::SettingId::hardware_shadow_filtering,
    config::SettingId::downsampled_translucency,
    config::SettingId::floating_point_render_targets,
    config::SettingId::max_multi_samples,
    config::SettingId::gore_level,
    config::SettingId::screen_percentage,
    config::SettingId::particle_percentage,
    config::SettingId::decal_lifetime,
}};

constexpr std::array<std::wstring_view, kAdvancedOptionCount> kLabels{{
    L"One-frame thread lag",
    L"Per-frame sleep",
    L"Per-frame yield",
    L"Background level streaming",
    L"Texture streaming",
    L"Priority texture streaming",
    L"Dynamic texture streaming",
    L"Hardware shadow filtering",
    L"Downsampled translucency",
    L"Floating-point render targets",
    L"Multisampling",
    L"Gore level",
    L"Render scale",
    L"Particle amount",
    L"Decal lifetime",
}};

std::size_t index(AdvancedOption option) noexcept {
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
            {ErrorCode::access_denied,
             L"A required KF2 advanced-settings file is unsafe or missing", 0});
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 4U * 1024U * 1024U) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"A KF2 advanced-settings file is too large to read safely", 0});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"A KF2 advanced-settings file cannot be opened", 0});
    }
    return Result<std::string>::success({
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}});
}

}  // namespace

config::SettingId advanced_setting_id(AdvancedOption option) noexcept {
    const auto selected = index(option);
    return selected < kSettingIds.size()
        ? kSettingIds[selected] : kSettingIds.front();
}

std::wstring_view advanced_option_label(AdvancedOption option) noexcept {
    const auto selected = index(option);
    return selected < kLabels.size() ? kLabels[selected] : L"Advanced setting";
}

std::wstring advanced_value_label(
    AdvancedOption option, const AdvancedGameSettings& settings) {
    const auto selected = index(option);
    if (selected >= settings.values.size()) return L"Unavailable";
    const auto& value = settings.values[selected];
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? L"On" : L"Off";
    }
    if (option == AdvancedOption::max_multisamples) {
        return std::to_wstring(std::get<int>(value)) + L"×";
    }
    if (option == AdvancedOption::gore_level) {
        constexpr std::array<std::wstring_view, 3> levels{
            L"Off", L"Reduced", L"Full"};
        const int level = std::get<int>(value);
        return level >= 0 && level < static_cast<int>(levels.size())
            ? std::wstring{levels[static_cast<std::size_t>(level)]}
            : L"Custom";
    }
    return std::visit([](const auto current) {
        return std::to_wstring(static_cast<int>(current));
    }, value);
}

bool advanced_option_is_slider(AdvancedOption option) noexcept {
    return option == AdvancedOption::screen_percentage ||
           option == AdvancedOption::particle_percentage ||
           option == AdvancedOption::decal_lifetime;
}

int advanced_slider_value(
    AdvancedOption option, const AdvancedGameSettings& settings) noexcept {
    const auto selected = index(option);
    if (selected >= settings.values.size()) return 0;
    const auto& value = settings.values[selected];
    if (const auto* integer = std::get_if<int>(&value)) return *integer;
    if (const auto* real = std::get_if<double>(&value)) {
        return static_cast<int>(*real);
    }
    return 0;
}

bool set_advanced_slider_value(
    AdvancedGameSettings& settings, AdvancedOption option, int value) noexcept {
    if (!advanced_option_is_slider(option)) return false;
    const auto selected = index(option);
    const auto* definition = config::find_setting(advanced_setting_id(option));
    if (!definition || value < definition->minimum ||
        value > definition->maximum) {
        return false;
    }
    if (definition->type == config::SettingType::real) {
        settings.values[selected] = static_cast<double>(value);
    } else if (definition->type == config::SettingType::integer) {
        settings.values[selected] = value;
    } else {
        return false;
    }
    return config::serialize_setting_value(
        *definition, settings.values[selected]).has_value();
}

bool cycle_advanced_option(
    AdvancedGameSettings& settings, AdvancedOption option) noexcept {
    if (advanced_option_is_slider(option)) return false;
    const auto selected = index(option);
    if (selected >= settings.values.size()) return false;
    const auto* definition = config::find_setting(advanced_setting_id(option));
    if (!definition) return false;
    auto next = config::step_setting_value(
        *definition, settings.values[selected], 1);
    if (!next) return false;
    if (*next == settings.values[selected] &&
        definition->type == config::SettingType::integer) {
        if (!definition->allowed_integers.empty()) {
            next = config::SettingValue{definition->allowed_integers.front()};
        } else {
            next = config::SettingValue{static_cast<int>(definition->minimum)};
        }
    }
    settings.values[selected] = *next;
    return true;
}

AdvancedGameSettings recommended_advanced_defaults() {
    AdvancedGameSettings defaults;
    defaults.values = {{
        true,   // one-frame thread lag
        false,  // per-frame sleep
        false,  // per-frame yield
        true,   // background level streaming
        true,   // texture streaming
        true,   // priority texture streaming
        true,   // dynamic texture streaming
        true,   // hardware shadow filtering
        true,   // downsampled translucency
        false,  // floating-point render targets
        1,      // multisampling
        2,      // full gore
        100.0,  // render scale
        100,    // particle amount
        30.0,   // decal lifetime
    }};
    return defaults;
}

Result<AdvancedGameSettings> read_advanced_game_settings(
    const std::filesystem::path& config_root) {
    std::map<std::filesystem::path, config::IniDocument> documents;
    AdvancedGameSettings settings;
    for (std::size_t selected = 0; selected < kSettingIds.size(); ++selected) {
        const auto* definition = config::find_setting(kSettingIds[selected]);
        if (!definition) {
            return Result<AdvancedGameSettings>::failure(
                {ErrorCode::internal_failure,
                 L"The advanced-settings catalog is incomplete", 0});
        }
        auto document = documents.find(definition->relative_path);
        if (document == documents.end()) {
            auto bytes = read_file(config_root / definition->relative_path);
            if (!bytes.has_value()) {
                return Result<AdvancedGameSettings>::failure(bytes.error());
            }
            auto parsed = config::IniDocument::parse(bytes.value());
            if (!parsed.has_value()) {
                return Result<AdvancedGameSettings>::failure(parsed.error());
            }
            document = documents.emplace(
                definition->relative_path, std::move(parsed.value())).first;
        }
        const auto text = document->second.find(
            definition->section, definition->key);
        const auto value = text
            ? config::parse_setting_value(*definition, *text) : std::nullopt;
        if (!value) {
            return Result<AdvancedGameSettings>::failure(
                {ErrorCode::invalid_argument,
                 L"A verified advanced KF2 setting is missing or invalid: " +
                     definition->key,
                 0});
        }
        settings.values[selected] = *value;
    }
    return Result<AdvancedGameSettings>::success(std::move(settings));
}

std::vector<config::RequestedChange> advanced_setting_changes(
    const AdvancedGameSettings& saved,
    const AdvancedGameSettings& pending) {
    std::vector<config::RequestedChange> changes;
    for (std::size_t selected = 0; selected < kSettingIds.size(); ++selected) {
        if (saved.values[selected] == pending.values[selected]) continue;
        changes.push_back({
            kSettingIds[selected], pending.values[selected],
            config::ChangeSource::explicit_user,
            L"Explicit user-selected advanced KF2 setting"});
    }
    changes.push_back({
        config::SettingId::temporal_aa, false,
        config::ChangeSource::explicit_user,
        L"Disable temporal frame-history anti-aliasing to prevent ghosting"});
    return changes;
}

}  // namespace kf2::game
