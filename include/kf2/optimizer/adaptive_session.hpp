#pragma once

#include <optional>

#include "kf2/optimizer/adaptive_stability.hpp"

namespace kf2::optimizer {

// The process-bound target describes the native cap KF2 actually started
// with. A newly selected target is persisted for the next launch, but must not
// make Adaptive grade the current process against a cap that is not active.
[[nodiscard]] constexpr int effective_adaptive_target_fps(
    int configured_value, std::optional<int> session_value) noexcept {
    if (session_value && valid_target_fps(*session_value)) {
        return *session_value;
    }
    return configured_value;
}

[[nodiscard]] constexpr int effective_adaptive_corpse_limit(
    int configured_value, std::optional<int> session_value) noexcept {
    return session_value && *session_value >= 4 && *session_value <= 2000
        ? *session_value : configured_value;
}

[[nodiscard]] constexpr int effective_adaptive_quality_change_budget(
    int configured_value, std::optional<int> session_value) noexcept {
    return session_value && *session_value >= 1 && *session_value <= 5
        ? *session_value : configured_value;
}

}  // namespace kf2::optimizer
