#pragma once

#include <cstdint>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

extern const char kOfflineTelemetryModuleSha256[];

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

// Test-only fault injection at the bounded directory-cleanup boundary.
using OfflineTelemetryCleanupTestHook = void (*)();
void set_offline_telemetry_cleanup_test_hook(
    OfflineTelemetryCleanupTestHook hook) noexcept;

#if defined(KF2_OFFLINE_TELEMETRY_LAB_TESTING)
enum class OfflineTelemetryRemovalTestStage {
    before_delete,
    retryable_failure,
};
using OfflineTelemetryRemovalTestHook = void (*)(
    OfflineTelemetryRemovalTestStage,
    const std::filesystem::path&,
    std::uint32_t);
void set_offline_telemetry_removal_test_hook(
    OfflineTelemetryRemovalTestHook hook) noexcept;
#endif

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

}  // namespace kf2::game
