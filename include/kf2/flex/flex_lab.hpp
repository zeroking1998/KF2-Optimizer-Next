#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "kf2/core/result.hpp"

namespace kf2::flex {

#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
enum class RuntimeInstallTestCheckpoint {
    sources_hashed,
    marker_written,
};

using RuntimeInstallTestHook = void (*)(RuntimeInstallTestCheckpoint checkpoint);
using RuntimeStatusHook = bool (*)(const std::filesystem::path&,
                                   std::error_code&);
void set_runtime_install_test_hook(RuntimeInstallTestHook hook) noexcept;
void set_runtime_status_hook_for_testing(RuntimeStatusHook hook) noexcept;
#endif

struct OptimizedRuntimeTransactionOptions {
    std::filesystem::path game_directory;
    std::filesystem::path state_directory;
    std::filesystem::path forwarder_dll;
    std::filesystem::path optimized_core_dll;
    std::filesystem::path optimized_extension_dll;
    std::filesystem::path optimized_helper_dll;
    bool game_running{true};
    bool exact_runtime_verified{false};
    bool simulate_failure_after_install{false};
};

struct OptimizedRuntimeTransactionResult {
    std::string original_core_sha256;
    std::string original_extension_sha256;
    std::string forwarder_sha256;
    std::string optimized_core_sha256;
    std::string optimized_extension_sha256;
    std::string optimized_helper_sha256;
    bool installed{false};
};

[[nodiscard]] Result<OptimizedRuntimeTransactionResult>
install_optimized_runtime_transaction(
    const OptimizedRuntimeTransactionOptions& options);
[[nodiscard]] Result<bool> restore_optimized_runtime_transaction(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running,
    std::wstring* recovery_details = nullptr);
[[nodiscard]] Result<bool> recover_optimized_runtime_transaction(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running,
    std::wstring* recovery_details = nullptr);

}  // namespace kf2::flex
