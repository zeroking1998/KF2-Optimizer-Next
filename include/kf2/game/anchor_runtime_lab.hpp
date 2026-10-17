#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

struct AnchorRuntimeLabOptions {
    std::filesystem::path game_directory;
    std::filesystem::path state_directory;
    std::filesystem::path helper_asset;
    bool game_running{false};
    bool simulate_failure_after_install{false};
};

[[nodiscard]] Result<bool> install_anchor_runtime_lab(
    const AnchorRuntimeLabOptions& options);
[[nodiscard]] Result<bool> restore_anchor_runtime_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running);
[[nodiscard]] Result<bool> recover_anchor_runtime_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running);

}  // namespace kf2::game
