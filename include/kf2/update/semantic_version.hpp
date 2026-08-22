#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::update {

struct SemanticVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};
    std::vector<std::string> prerelease;
    std::string canonical;
};

[[nodiscard]] Result<SemanticVersion> parse_semantic_version(
    std::string_view text);
[[nodiscard]] int compare_semantic_versions(
    const SemanticVersion& left, const SemanticVersion& right) noexcept;

}  // namespace kf2::update
