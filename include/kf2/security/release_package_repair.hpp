#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/security/package_integrity.hpp"

namespace kf2::security {

struct ReleaseRepairPlan {
    std::wstring tag;
    std::wstring asset_name;
    std::wstring url;
};

[[nodiscard]] Result<ReleaseRepairPlan> exact_release_repair_plan(
    std::string_view installed_version);

[[nodiscard]] Result<PackageRepairResult>
download_and_repair_release_package(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity);

}  // namespace kf2::security
