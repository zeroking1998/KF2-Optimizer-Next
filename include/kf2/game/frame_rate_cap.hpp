#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"
#include "kf2/game/game_discovery.hpp"

namespace kf2::game {

struct FrameRateCapResult final {
    int target_fps{60};
    bool changed{false};
};

// Persists KF2's vendor-independent startup cap. The engine console variable
// is the hard ceiling; frame smoothing keeps the game's own pacing settings in
// agreement with the same user-selected target.
[[nodiscard]] Result<FrameRateCapResult> persist_frame_rate_cap(
    const GameInstallation& installation, int target_fps);

}  // namespace kf2::game
