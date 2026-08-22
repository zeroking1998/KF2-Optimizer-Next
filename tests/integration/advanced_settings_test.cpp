#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "kf2/game/advanced_settings.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

std::size_t index(kf2::game::AdvancedOption option) {
    return static_cast<std::size_t>(option);
}

void write_selected_catalog(const std::filesystem::path& root) {
    using namespace kf2;
    std::map<std::filesystem::path, std::string> files;
    for (std::size_t selected = 0;
         selected < game::kAdvancedOptionCount; ++selected) {
        const auto option = static_cast<game::AdvancedOption>(selected);
        const auto* definition = config::find_setting(
            game::advanced_setting_id(option));
        auto& bytes = files[definition->relative_path];
        bytes += "[";
        for (const wchar_t character : definition->section) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "]\r\n";
        for (const wchar_t character : definition->key) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "=";
        config::SettingValue value = definition->type == config::SettingType::boolean
            ? config::SettingValue{true}
            : definition->type == config::SettingType::integer
                ? config::SettingValue{static_cast<int>(definition->minimum)}
                : config::SettingValue{definition->minimum};
        const auto serialized = config::serialize_setting_value(
            *definition, value);
        for (const wchar_t character : *serialized) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "\r\n";
    }
    for (const auto& [relative, bytes] : files) {
        std::filesystem::create_directories((root / relative).parent_path());
        std::ofstream output(root / relative, std::ios::binary | std::ios::trunc);
        output << bytes;
    }
}

}  // namespace

int main() {
    using namespace kf2;
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    write_selected_catalog(root);

    const auto loaded = game::read_advanced_game_settings(root);
    CHECK(loaded.has_value());
    auto pending = loaded.value();
    CHECK(game::advanced_value_label(
              game::AdvancedOption::one_frame_thread_lag, pending) == L"On");
    CHECK(game::cycle_advanced_option(
        pending, game::AdvancedOption::one_frame_thread_lag));
    CHECK(game::advanced_value_label(
              game::AdvancedOption::one_frame_thread_lag, pending) == L"Off");

    CHECK(game::cycle_advanced_option(
        pending, game::AdvancedOption::max_multisamples));
    CHECK(std::get<int>(pending.values[index(
              game::AdvancedOption::max_multisamples)]) == 2);
    CHECK(game::set_advanced_slider_value(
        pending, game::AdvancedOption::screen_percentage, 137));
    CHECK(game::advanced_slider_value(
              game::AdvancedOption::screen_percentage, pending) == 137);
    CHECK(!game::set_advanced_slider_value(
        pending, game::AdvancedOption::screen_percentage, 201));
    CHECK(game::set_advanced_slider_value(
        pending, game::AdvancedOption::particle_percentage, 86));
    CHECK(game::set_advanced_slider_value(
        pending, game::AdvancedOption::decal_lifetime, 45));

    const auto defaults = game::recommended_advanced_defaults();
    CHECK(game::advanced_value_label(
              game::AdvancedOption::texture_streaming, defaults) == L"On");
    CHECK(game::advanced_value_label(
              game::AdvancedOption::floating_point_render_targets, defaults) ==
          L"Off");
    CHECK(game::advanced_slider_value(
              game::AdvancedOption::screen_percentage, defaults) == 100);
    CHECK(game::advanced_slider_value(
              game::AdvancedOption::particle_percentage, defaults) == 100);
    CHECK(game::advanced_slider_value(
              game::AdvancedOption::decal_lifetime, defaults) == 30);

    const auto changes = game::advanced_setting_changes(
        loaded.value(), pending);
    CHECK(changes.size() == 5);
    for (const auto& change : changes) {
        CHECK(change.source == config::ChangeSource::explicit_user);
    }

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
