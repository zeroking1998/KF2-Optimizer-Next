#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::config {

struct SessionConfigSnapshot {
    std::filesystem::path config_root;
    std::filesystem::path snapshot_root;
    std::size_t file_count{};
    std::uint64_t root_volume{0};
    std::uint64_t root_file{0};
};

[[nodiscard]] Result<SessionConfigSnapshot> capture_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root);
[[nodiscard]] Result<std::size_t> restore_session_config(
    const SessionConfigSnapshot& snapshot);
[[nodiscard]] Result<std::optional<SessionConfigSnapshot>>
resume_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root);
[[nodiscard]] Result<std::size_t> recover_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running);

}  // namespace kf2::config
