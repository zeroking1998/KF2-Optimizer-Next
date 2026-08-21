#pragma once

#include <cstdint>
#include <optional>

#include "kf2/backup/backup_store.hpp"
#include "kf2/config/config_preview.hpp"

namespace kf2::config {

struct ApplyPreconditions {
    bool game_running{false};
    std::optional<std::uintmax_t> available_bytes;
};

struct ApplyResult {
    backup::BackupSet backup;
    std::size_t files_changed{0};
};

[[nodiscard]] Result<ApplyResult> apply_preview(
    const ConfigPreview& preview, backup::BackupStore& store,
    const ApplyPreconditions& preconditions);

}  // namespace kf2::config
