#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <system_error>

#include "kf2/core/result.hpp"

namespace kf2::config {

struct SessionConfigSnapshot {
    std::filesystem::path config_root;
    std::filesystem::path snapshot_root;
    std::size_t file_count{};
    std::uint64_t root_volume{0};
    std::uint64_t root_file{0};
};

#if defined(KF2_SESSION_GUARD_TESTING)
using SessionReadHook = void (*)(const std::filesystem::path&);
using SessionStatusHook = bool (*)(const std::filesystem::path&,
                                   std::error_code&);
using SessionManifestWriteHook = void (*)(std::ostream&);
void set_session_read_hook_for_testing(SessionReadHook hook) noexcept;
void set_session_status_hook_for_testing(SessionStatusHook hook) noexcept;
void set_session_manifest_write_hook_for_testing(SessionManifestWriteHook hook) noexcept;
#endif

[[nodiscard]] Result<SessionConfigSnapshot> capture_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root);
[[nodiscard]] Result<std::size_t> restore_session_config(
    const SessionConfigSnapshot& snapshot, bool retain_snapshot = false);
// Only finalize after the caller's confirmed graphics replay passed readback.
[[nodiscard]] Result<bool> complete_session_config(
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
