#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

#ifndef KF2_OFFLINE_TELEMETRY_SHA256
#define KF2_OFFLINE_TELEMETRY_SHA256 \
    "e8979fc9b0daf6104ebb7e4027aabf5e048aa1c49d386f29373253eabca27933"
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
// running; otherwise it performs the exact cleanup.
[[nodiscard]] Result<OfflineTelemetryRecovery>
recover_offline_telemetry_lab(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running);

}  // namespace kf2::game
