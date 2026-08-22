#include "kf2/update/release_notes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <vector>

namespace kf2::update {
namespace {

constexpr std::size_t kMaximumInputBytes = 128U * 1024U;
constexpr std::size_t kMaximumOutputBytes = 16U * 1024U;

enum class Section { none, new_features, bug_fixes, important_notes };

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(
               static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(
               static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return std::string{value};
}

std::string normalized_heading(std::string_view line) {
    while (!line.empty() && (line.front() == '#' ||
                             std::isspace(static_cast<unsigned char>(line.front())))) {
        line.remove_prefix(1);
    }
    std::string result;
    for (const unsigned char character : line) {
        if (std::isalnum(character)) {
            result.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return result;
}

Section section_from_heading(std::string_view line) {
    if (!line.starts_with('#')) return Section::none;
    const auto heading = normalized_heading(line);
    if (heading == "whatsnew" || heading == "new" ||
        heading == "newfeatures") return Section::new_features;
    if (heading == "bugfixes" || heading == "fixes" ||
        heading == "fixed") return Section::bug_fixes;
    if (heading == "importantnotes" || heading == "important") {
        return Section::important_notes;
    }
    return Section::none;
}

std::string title(Section section) {
    switch (section) {
        case Section::new_features: return "## What's new";
        case Section::bug_fixes: return "## Bug fixes";
        case Section::important_notes: return "## Important notes";
        default: return {};
    }
}

}  // namespace

std::string concise_release_notes(std::string_view markdown) {
    if (markdown.size() > kMaximumInputBytes) {
        markdown = markdown.substr(0, kMaximumInputBytes);
    }
    std::array<std::vector<std::string>, 3> lines;
    Section active = Section::none;
    std::istringstream input{std::string{markdown}};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with('#')) {
            active = section_from_heading(line);
            continue;
        }
        if (active == Section::none) continue;
        auto value = trim(line);
        if (value.empty()) continue;
        if (!value.starts_with("- ") && !value.starts_with("* ")) continue;
        value.replace(0, 2, "- ");
        const auto index = static_cast<std::size_t>(active) - 1U;
        if (lines[index].size() < 32U && value.size() <= 512U) {
            lines[index].push_back(std::move(value));
        }
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].empty()) continue;
        if (!result.empty()) result += "\n\n";
        const auto section = static_cast<Section>(index + 1U);
        result += title(section) + "\n";
        for (const auto& item : lines[index]) {
            if (result.size() + item.size() + 1U > kMaximumOutputBytes) {
                return result;
            }
            result += item + "\n";
        }
        if (!result.empty()) result.pop_back();
    }
    return result.empty()
        ? "## Important notes\n- No concise release notes were provided."
        : result;
}

}  // namespace kf2::update
