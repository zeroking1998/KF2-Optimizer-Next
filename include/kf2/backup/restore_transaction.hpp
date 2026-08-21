#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include "kf2/backup/backup_store.hpp"
#include "kf2/config/apply_transaction.hpp"

namespace kf2::backup {

enum class RecoveryOutcome { clean, rolled_back, rolled_forward };

struct RestoreResult {
    BackupSet pre_restore_backup;
    std::size_t files_restored{0};
};

struct RecoveryResult {
    RecoveryOutcome outcome{RecoveryOutcome::clean};
    std::size_t transactions_recovered{0};
};

[[nodiscard]] Result<RestoreResult> restore_backup(
    BackupStore& store, std::string_view id,
    const std::filesystem::path& expected_config_root,
    const config::ApplyPreconditions& preconditions);
[[nodiscard]] Result<RecoveryResult> recover_transactions(
    BackupStore& store,
    const std::optional<std::filesystem::path>& expected_config_root);
[[nodiscard]] Result<std::string> export_preview_json(
    const config::ConfigPreview& preview);
[[nodiscard]] Result<std::vector<config::RequestedChange>>
import_requested_changes_json(std::string_view json);

}  // namespace kf2::backup
