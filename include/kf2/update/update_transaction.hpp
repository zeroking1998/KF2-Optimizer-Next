#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::update {

enum class UpdateFaultInjection {
    none,
    after_first_replacement,
    interrupt_after_replacement,
    interrupt_after_verification,
    rollback_failure,
};

enum class UpdateRecoveryState {
    not_started,
    owner_active,
    update_verified,
    rollback_verified,
};

struct UpdateTransactionRequest {
    std::filesystem::path target_root;
    std::filesystem::path staged_root;
    std::filesystem::path backup_root;
    std::string expected_new_version;
    UpdateFaultInjection fault{UpdateFaultInjection::none};
    std::size_t fault_after_replacements{};
};

struct UpdateTransactionResult {
    std::size_t replaced_files{};
    bool rolled_back{false};
    std::string previous_version;
    std::string installed_version;
};

struct UpdateRecoveryResult {
    UpdateRecoveryState state{UpdateRecoveryState::owner_active};
    std::size_t replaced_files{};
};

struct UpdateOwnerIdentity {
    std::uint32_t process_id{};
    std::uint64_t process_start_id{};
};

#if defined(KF2_UPDATE_TRANSACTION_TESTING)
using ManagedReadHook = void (*)(const std::filesystem::path& path);
void set_managed_read_hook_for_testing(ManagedReadHook hook) noexcept;
#endif

[[nodiscard]] Result<std::string> package_version(
    const std::filesystem::path& package_root);

[[nodiscard]] Result<UpdateTransactionResult> apply_update_transaction(
    const UpdateTransactionRequest& request);

[[nodiscard]] Result<bool> rollback_update_transaction(
    const std::filesystem::path& target_root,
    const std::filesystem::path& backup_root);

[[nodiscard]] Result<UpdateRecoveryResult> recover_update_transaction(
    const UpdateTransactionRequest& request);

[[nodiscard]] Result<bool> mark_update_transaction_handoff_ready(
    const UpdateTransactionRequest& request);

[[nodiscard]] Result<bool> update_transaction_allows_cleanup(
    const UpdateTransactionRequest& request);

[[nodiscard]] Result<UpdateOwnerIdentity> update_transaction_owner_identity(
    const UpdateTransactionRequest& request);

}  // namespace kf2::update
