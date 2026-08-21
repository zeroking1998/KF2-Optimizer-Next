#pragma once

#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::flex {

struct LabTransactionOptions {
    std::filesystem::path game_directory;
    std::filesystem::path state_directory;
    std::filesystem::path forwarder_dll;
    bool game_running{true};
    bool exact_runtime_verified{false};
    bool offline_confirmed{false};
    bool simulate_failure_after_install{false};
};

struct LabTransactionResult {
    std::string original_sha256;
    std::string forwarder_sha256;
    bool installed{false};
};

[[nodiscard]] Result<LabTransactionResult> install_offline_lab(
    const LabTransactionOptions& options);
[[nodiscard]] Result<bool> restore_offline_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running);
[[nodiscard]] Result<bool> recover_offline_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running);

}  // namespace kf2::flex
