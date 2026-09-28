#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "kf2/core/result.hpp"

namespace kf2::flex {

#if defined(KF2_FLEX_LAB_TEST_HOOKS)
enum class LabInstallTestCheckpoint {
    sources_hashed,
    marker_written,
};

using LabInstallTestHook = void (*)(LabInstallTestCheckpoint checkpoint);
using LabStatusHook = bool (*)(const std::filesystem::path&,
                               std::error_code&);
void set_lab_install_test_hook(LabInstallTestHook hook) noexcept;
void set_lab_status_hook_for_testing(LabStatusHook hook) noexcept;
#endif

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
