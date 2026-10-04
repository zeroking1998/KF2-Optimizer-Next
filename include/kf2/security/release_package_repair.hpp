#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/security/package_integrity.hpp"
#include "kf2/update/update_package.hpp"

namespace kf2::security {

struct ReleaseRepairPlan {
    std::wstring tag;
    std::wstring asset_name;
    std::wstring url;
};

struct ReleaseRepairOperations {
    std::function<Result<update::ReleaseInfo>(std::string_view)> query_release;
    std::function<Result<update::PreparedUpdatePackage>(
        const update::ReleaseInfo&, const std::filesystem::path&)> prepare_package;
};

[[nodiscard]] Result<ReleaseRepairPlan> exact_release_repair_plan(
    std::string_view installed_version);

[[nodiscard]] Result<PackageRepairResult>
download_and_repair_release_package(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity);

[[nodiscard]] Result<PackageRepairResult>
download_and_repair_release_package_with_operations(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity,
    const ReleaseRepairOperations& operations);

}  // namespace kf2::security
