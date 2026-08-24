#include "kf2/flex/flex_adaptive_policy.hpp"
#include "kf2/flex/flex_observation_shared.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#include <array>

int main() {
    kf2::flex::AdaptivePolicy policy;
    if (policy.synchronize_observed(0)) return 900;
    if (policy.synchronize_observed(6)) return 901;
    if (policy.evaluate(false, 60, 20.0, 1000).constrained) return 1;
    auto quality = policy.evaluate(true, 60, 60.0, 1000);
    if (!quality.constrained || quality.requested_substeps != 5) return 2;
    if (policy.evaluate(true, 60, 59.0, 2000).requested_substeps != 5)
        return 3;

    // The canonical 58-FPS-equivalent guard lowers exactly one useful level
    // after 600 ms; the obsolete fixed 0.95 rule is not used.
    constexpr double corrective_fps = 58.0;
    if (policy.evaluate(true, 60, corrective_fps - 0.001, 3000).requested_substeps != 5)
        return 4;
    if (policy.evaluate(true, 60, corrective_fps - 0.001, 3599).requested_substeps != 5)
        return 5;
    if (policy.evaluate(true, 60, corrective_fps - 0.001, 3600).requested_substeps != 4)
        return 6;

    // Critical pressure uses the shorter 400 ms confirmation and still steps.
    if (policy.evaluate(true, 60, 56.0, 4000).requested_substeps != 4)
        return 7;
    if (policy.evaluate(true, 60, 56.0, 4399).requested_substeps != 4)
        return 8;
    if (policy.evaluate(true, 60, 56.0, 4400).requested_substeps != 3)
        return 9;
    if (policy.evaluate(true, 60, 56.0, 4800).requested_substeps != 3)
        return 10;
    if (policy.evaluate(true, 60, 56.0, 5200).requested_substeps != 2)
        return 11;

    // Recovery is intentionally slower and also advances one level at a time.
    if (policy.evaluate(true, 60, 60.0, 6000).requested_substeps != 2)
        return 12;
    if (policy.evaluate(true, 60, 60.0, 10999).requested_substeps != 2)
        return 13;
    if (policy.evaluate(true, 60, 60.0, 11000).requested_substeps != 3)
        return 14;
    if (policy.evaluate(true, 60, 60.0, 16000).requested_substeps != 3)
        return 15;

    // Quality-step budget two makes a confirmed normal correction materially
    // stronger (5 -> 3), while the normal recovery path above remains +1.
    kf2::flex::AdaptivePolicy stronger;
    if (stronger.evaluate(true, 60, 60.0, 17'000, 2)
            .requested_substeps != 5) return 29;
    if (stronger.evaluate(true, 60, corrective_fps - 0.001, 17'001, 2)
            .requested_substeps != 5) return 30;
    if (stronger.evaluate(true, 60, corrective_fps - 0.001, 17'601, 2)
            .requested_substeps != 3) return 31;

    // Entering Adaptive pressure preserves the game's observed solver level;
    // it must never jump from one or two substeps to the maximum first.
    kf2::flex::AdaptivePolicy observed;
    if (!observed.synchronize_observed(2)) return 902;
    if (observed.evaluate(true, 60, 56.0, 18'000, 2)
            .requested_substeps != 2) return 903;
    if (observed.evaluate(true, 60, 56.0, 18'400, 2)
            .requested_substeps != 1) return 904;

    // Every representative target reacts at its exact target-relative
    // corrective band, including non-display-standard and upper-bound values.
    constexpr std::array targets{45, 60, 100, 137, 144, 200, 239, 240};
    std::uint64_t clock = 20'000;
    for (const int target : targets) {
        policy.reset();
        if (policy.evaluate(true, target, static_cast<double>(target), clock)
                .requested_substeps != 5) return 30 + target;
        const auto bands =
            kf2::optimizer::adaptive_stability_bands(target);
        const double guard_fps = 1000.0 /
            bands.corrective_frame_time_ms;
        if (policy.evaluate(true, target, guard_fps - 0.0001, clock + 1)
                .requested_substeps != 5) return 300 + target;
        if (policy.evaluate(true, target, guard_fps - 0.0001, clock + 601)
                .requested_substeps != 4) return 600 + target;
        clock += 2'000;
    }

    // A target change invalidates the old pending correction and rebases.
    policy.reset();
    if (policy.evaluate(true, 60, 58.0, 50'000).requested_substeps != 5)
        return 27;
    if (policy.evaluate(true, 137, 132.0, 50'600).requested_substeps != 5)
        return 28;

    policy.reset();
    auto restarted = policy.evaluate(true, 60, 60.0, 60'000);
    if (!restarted.constrained || restarted.requested_substeps != 5) return 16;
    if (policy.evaluate(true, 60, {}, 61'000).constrained) return 17;
    if (kf2::flex::adaptive_substeps(2, 1, true) != 1) return 18;
    if (kf2::flex::adaptive_substeps(2, 1, false) != 2) return 19;
    if (kf2::flex::adaptive_substeps(1, 2, true) != 2) return 20;
    if (kf2::flex::adaptive_substeps(2, 0, true) != 2) return 21;
    if (kf2::flex::adaptive_substeps(3, 5, true) != 5) return 22;
    if (kf2::flex::adaptive_warmup_complete(180, true, false)) return 23;
    if (!kf2::flex::adaptive_warmup_complete(181, true, false)) return 24;
    if (kf2::flex::adaptive_warmup_complete(181, false, false)) return 25;
    if (kf2::flex::adaptive_warmup_complete(181, true, true)) return 26;
    return 0;
}
