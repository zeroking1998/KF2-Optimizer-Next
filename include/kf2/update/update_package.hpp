#pragma once

#include <filesystem>
#include <functional>

#include "kf2/core/result.hpp"
#include "kf2/update/update_controller.hpp"

namespace kf2::update {

struct PreparedUpdatePackage {
    std::filesystem::path work_root;
    std::filesystem::path archive_path;
    std::filesystem::path staged_root;
};

struct UpdatePackageOperations {
    std::function<Result<bool>(const ReleaseAsset&,
                               const std::filesystem::path&)> download;
    std::function<Result<bool>(const std::filesystem::path&,
                               const std::filesystem::path&)> extract;
};

[[nodiscard]] Result<bool> verify_update_archive(
    const std::filesystem::path& archive,
    const ReleaseAsset& asset);

[[nodiscard]] Result<bool> validate_staged_update_package(
    const std::filesystem::path& staged_root,
    const ReleaseInfo& release);

[[nodiscard]] Result<PreparedUpdatePackage> prepare_update_package(
    const ReleaseInfo& release,
    const std::filesystem::path& new_work_root);

[[nodiscard]] Result<PreparedUpdatePackage> prepare_update_package_with_operations(
    const ReleaseInfo& release,
    const std::filesystem::path& new_work_root,
    const UpdatePackageOperations& operations);

}  // namespace kf2::update
