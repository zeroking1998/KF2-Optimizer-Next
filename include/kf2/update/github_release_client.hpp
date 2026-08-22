#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/update/update_controller.hpp"

namespace kf2::update {

[[nodiscard]] std::string_view official_release_repository() noexcept;

[[nodiscard]] Result<std::optional<ReleaseInfo>> parse_github_releases(
    std::string_view json, std::string_view repository,
    std::string_view installed_version);

[[nodiscard]] Result<std::optional<ReleaseInfo>> query_official_github_releases(
    std::string_view installed_version);

}  // namespace kf2::update
