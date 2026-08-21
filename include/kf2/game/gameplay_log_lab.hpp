#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

// Enables KF2's own bounded AI/wave logs and additively registers the pinned
// protected telemetry viewport bridge for the next standalone session.
// Adaptive frame-pressure-gated corpse cleanup
// is an explicit offline-only cosmetic actuator; the actor remains read-only
// when it is off.
// The caller must capture the complete INI session first and restore it after
// KF2 exits. No game DLL, process memory or network state is touched.
[[nodiscard]] Result<bool> enable_offline_gameplay_logging(
    const std::filesystem::path& config_root,
    bool adaptive_corpse_stagger = false,
    int adaptive_corpse_maximum = 0,
    int adaptive_target_fps = 0,
    bool adaptive_corpse_debug_markers = false,
    int adaptive_quality_change_budget = 1);

}  // namespace kf2::game
