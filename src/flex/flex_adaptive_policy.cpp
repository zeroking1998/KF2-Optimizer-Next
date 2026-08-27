#include "kf2/flex/flex_adaptive_policy.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::flex {

bool AdaptivePolicy::synchronize_observed(int substeps) noexcept {
    if (substeps < 1 || substeps > 5) return false;
    if (active_substeps_ == 0) {
        active_substeps_ = substeps;
        candidate_substeps_ = substeps;
        constrained_ = true;
    }
    return true;
}

AdaptiveDecision AdaptivePolicy::evaluate(bool enabled, int target_fps,
    std::optional<double> fps, std::uint64_t now_ms,
    int quality_change_budget,
    std::optional<double> enemy_pressure) noexcept {
    constexpr int maximum_substeps = 5;
    if (!enabled || !optimizer::valid_target_fps(target_fps) || !fps ||
        !std::isfinite(*fps) || *fps <= 0.0 ||
        quality_change_budget < 1 || quality_change_budget > 5) {
        reset();
        return {};
    }
    int enemy_pressure_target = maximum_substeps;
    if (enemy_pressure && std::isfinite(*enemy_pressure)) {
        const double pressure = std::clamp(*enemy_pressure, 0.0, 1.0);
        enemy_pressure_target = std::clamp(
            maximum_substeps - static_cast<int>(pressure * 4.999),
            1, maximum_substeps);
    }

    if (target_fps_ != 0 && target_fps_ != target_fps) {
        // Target-dependent history and pending work must never cross a target
        // generation. Preserve the observed effective level while discarding
        // only target-dependent pending work.
        candidate_substeps_ = active_substeps_;
        candidate_since_ = now_ms;
    }
    if (last_evaluation_ms_ != 0 && now_ms < last_evaluation_ms_) {
        reset();
    }
    target_fps_ = target_fps;
    last_evaluation_ms_ = now_ms;

    // Adaptive FleX is quality-first: a valid gameplay session starts at the
    // full quality level and gives performance telemetry the opportunity to
    // prove that less work is necessary.  The old ratio table required up to
    // 140% of the target FPS before selecting level five.  That can never be
    // reached by a frame-capped game (KF2 commonly presents around 62 FPS), so
    // Auto remained stuck at the original level or levels one and two.
    if (active_substeps_ == 0) {
        active_substeps_ = maximum_substeps;
        candidate_substeps_ = enemy_pressure_target;
        candidate_since_ = now_ms;
        constrained_ = true;
        return {active_substeps_, constrained_};
    }

    const auto bands = optimizer::adaptive_stability_bands(target_fps);
    const double frame_time_ms = 1000.0 / *fps;
    int candidate = active_substeps_;
    std::uint64_t dwell = 0;
    if (frame_time_ms >= bands.corrective_frame_time_ms &&
        active_substeps_ > 1) {
        const bool critical =
            frame_time_ms >= bands.critical_frame_time_ms;
        // The user-visible 1..5 quality-step budget now controls real FleX
        // attack strength. Critical pressure adds one bounded step from level
        // two upward; recovery below remains one level at a time.
        const int attack_steps = std::clamp(
            quality_change_budget +
                (critical && quality_change_budget > 1 ? 1 : 0),
            1, maximum_substeps);
        candidate = active_substeps_ - attack_steps;
        dwell = critical ? 400 : 600;
    } else if (frame_time_ms <= bands.warning_frame_time_ms &&
               active_substeps_ < maximum_substeps) {
        // Slow release prevents target-band noise from causing ping-pong.
        candidate = active_substeps_ + 1;
        dwell = 5'000;
    }
    if (enemy_pressure_target < candidate) {
        // Visible-enemy load is proactive: it may lower solver work before
        // the following frame-time samples fall, but uses the same dwell
        // and never bypasses the verified user-enabled FleX actuator.
        candidate = enemy_pressure_target;
        dwell = dwell == 0 ? 600 : std::min<std::uint64_t>(dwell, 600);
    }
    candidate = std::clamp(candidate, 1, maximum_substeps);

    if (candidate != candidate_substeps_) {
        candidate_substeps_ = candidate;
        candidate_since_ = now_ms;
    } else if (candidate != active_substeps_ && now_ms >= candidate_since_) {
        if (now_ms - candidate_since_ >= dwell) {
            active_substeps_ = candidate;
            // HOLD begins at each real integer transition. A continuing
            // correction must establish a new candidate on a later sample.
            candidate_substeps_ = active_substeps_;
            candidate_since_ = now_ms;
        }
    }
    constrained_ = true;
    return {active_substeps_, constrained_};
}

void AdaptivePolicy::reset() noexcept {
    constrained_ = false;
    active_substeps_ = 0;
    candidate_substeps_ = 0;
    candidate_since_ = 0;
    last_evaluation_ms_ = 0;
    target_fps_ = 0;
}

}  // namespace kf2::flex
