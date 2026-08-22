#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::update {

enum class UpdateFaultInjection {
    none,
    after_first_replacement,
};

struct UpdateTransactionRequest {
    std::filesystem::path target_root;
    std::filesystem::path staged_root;
    std::filesystem::path backup_root;
    std::string expected_new_version;
    UpdateFaultInjection fault{UpdateFaultInjection::none};
};

struct UpdateTransactionResult {
    std::size_t replaced_files{};
    bool rolled_back{false};
    std::string previous_version;
    std::string installed_version;
};

[[nodiscard]] Result<std::string> package_version(
    const std::filesystem::path& package_root);

[[nodiscard]] Result<UpdateTransactionResult> apply_update_transaction(
    const UpdateTransactionRequest& request);

[[nodiscard]] Result<bool> rollback_update_transaction(
    const std::filesystem::path& target_root,
    const std::filesystem::path& backup_root);

}  // namespace kf2::update
