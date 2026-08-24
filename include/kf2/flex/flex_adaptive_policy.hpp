#pragma once

#include <cstdint>
#include <optional>

namespace kf2::flex {

struct AdaptiveDecision {
    int requested_substeps{0}; // 0 preserves the game's original value
    bool constrained{false};
};

class AdaptivePolicy final {
public:
    [[nodiscard]] bool synchronize_observed(int substeps) noexcept;
    [[nodiscard]] AdaptiveDecision evaluate(bool enabled, int target_fps,
        std::optional<double> fps, std::uint64_t now_ms,
        int quality_change_budget = 1) noexcept;
    void reset() noexcept;

private:
    bool constrained_{false};
    int active_substeps_{0};
    int candidate_substeps_{0};
    std::uint64_t candidate_since_{0};
    std::uint64_t last_evaluation_ms_{0};
    int target_fps_{0};
};

}  // namespace kf2::flex
