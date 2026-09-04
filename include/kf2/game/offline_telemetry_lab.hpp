#pragma once

#include <cstdint>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

#ifndef KF2_OFFLINE_TELEMETRY_SHA256
#define KF2_OFFLINE_TELEMETRY_SHA256 \
    "df0bd4c5703562c13dabca4d252983ad751ed00a706e91c18f883c01227385c0"
#endif
inline constexpr char kOfflineTelemetryModuleSha256[] =
    KF2_OFFLINE_TELEMETRY_SHA256;

struct OfflineTelemetryLabOptions {
    std::filesystem::path config_root;
    std::filesystem::path state_root;
    std::filesystem::path module_asset;
    bool game_running{false};
};

struct OfflineTelemetryRecovery {
    bool active{false};
    bool cleaned{false};
};

struct OfflineTelemetryCleanupHelperOptions {
    std::uint32_t wait_process_id{0};
    std::filesystem::path config_root;
    std::filesystem::path state_root;
    std::uint32_t wait_timeout_ms{60'000};
};

// Installs the pinned UnrealScript package into KF2's normal per-user
// Published/BrewedPC directory for one protected session, regardless of
// whether KF2 is then started from the optimizer, Steam or a shortcut.
// Telemetry is read-only; its distance/density and frame-pressure corpse
// actuator is separately policy-gated.
[[nodiscard]] Result<bool> install_offline_telemetry_lab(
    const OfflineTelemetryLabOptions& options);

// Removes only a package that is bound by the local marker and still matches
// the pinned hash. A running game always blocks removal.
[[nodiscard]] Result<bool> restore_offline_telemetry_lab(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running);

// On startup, retains a verified package while its bound KF2 process is still
// running; otherwise it performs exact cleanup. When KF2 is stopped it also
// removes a markerless package whose UE3 identity and optimizer class set prove
// that it came from an older KF2 Optimizer version. Foreign occupants remain.
[[nodiscard]] Result<OfflineTelemetryRecovery>
recover_offline_telemetry_lab(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running);

// Starts a hidden, short-lived copy of the portable executable. It waits for
// the process that still owns the package to exit and then performs the same
// marker- and hash-bound cleanup as normal recovery.
[[nodiscard]] Result<bool> launch_offline_telemetry_cleanup_helper(
    const OfflineTelemetryCleanupHelperOptions& options);

// Helper entry point used before the normal application and single-instance
// startup paths. Exposed for deterministic recovery tests.
[[nodiscard]] Result<bool> run_offline_telemetry_cleanup_helper(
    const OfflineTelemetryCleanupHelperOptions& options);

}  // namespace kf2::game
