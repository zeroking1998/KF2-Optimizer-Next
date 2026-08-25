#pragma once

#include <algorithm>
#include <optional>

namespace kf2::telemetry_pipeline {

// Mirrors the protected KF2 provider's monotonic enemy-pressure score. Visible
// enemies dominate the result, while total living enemies and active attacks
// add bounded pressure. Adding enemies or attacks can never reduce the score.
[[nodiscard]] inline std::optional<double> calculate_enemy_scene_pressure(
    const std::optional<int>& visible_living,
    const std::optional<int>& total_living,
    const std::optional<int>& living_attack_moves,
    bool gameplay_context_fresh) noexcept {
    if (!gameplay_context_fresh || !visible_living || !total_living ||
        *visible_living < 0 || *total_living <= 0 ||
        (living_attack_moves && *living_attack_moves < 0)) {
        return std::nullopt;
    }
    const int score = std::clamp(
        std::min(60, *visible_living * 8) +
            std::min(25, *total_living * 2) +
            std::min(15, living_attack_moves.value_or(0) * 5),
        0, 100);
    return static_cast<double>(score) / 100.0;
}

}  // namespace kf2::telemetry_pipeline
