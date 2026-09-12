#pragma once

#include <optional>
#include <string>

#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::telemetry_pipeline {

struct ZedSpawnObservation final {
    int sample{0};
    int previous_living{0};
    int current_living{0};
    int delta{0};
    bool first_group{false};
};

// Diagnostic-only correlation over the existing once-per-second KF2 telemetry
// sample. It never scans actors and never feeds Adaptive decisions.
class ZedSpawnObservationTracker final {
public:
    void reset() noexcept { *this = {}; }

    [[nodiscard]] std::optional<ZedSpawnObservation> observe(
        const TelemetryFrame& frame) {
        const auto& gameplay = frame.gameplay;
        if (!frame.active_gameplay || !frame.offline_gameplay || !gameplay ||
            !gameplay->telemetry_sample ||
            *gameplay->telemetry_sample <= 0 ||
            !gameplay->telemetry_living_zeds ||
            *gameplay->telemetry_living_zeds < 0 ||
            gameplay->telemetry_observed_ns == 0 ||
            frame.observed_at_ns < gameplay->telemetry_observed_ns ||
            frame.observed_at_ns - gameplay->telemetry_observed_ns >
                game::kGameLogObservationFreshnessNs) {
            return std::nullopt;
        }

        const int sample = *gameplay->telemetry_sample;
        const int living = *gameplay->telemetry_living_zeds;
        const bool new_context = !initialized_ || identity_ != frame.identity ||
            map_ != gameplay->map || sample < sample_;
        if (new_context) {
            identity_ = frame.identity;
            map_ = gameplay->map;
            sample_ = sample;
            living_ = living;
            zero_baseline_seen_ = living == 0;
            initialized_ = true;
            return std::nullopt;
        }

        // The application pipeline runs faster than the UnrealScript sample.
        // Process each telemetry sample exactly once.
        if (sample == sample_) return std::nullopt;

        const int previous = living_;
        sample_ = sample;
        living_ = living;
        if (living <= previous) {
            if (living == 0) zero_baseline_seen_ = true;
            return std::nullopt;
        }

        ZedSpawnObservation result{
            sample, previous, living, living - previous,
            zero_baseline_seen_ && previous == 0};
        zero_baseline_seen_ = false;
        return result;
    }

private:
    telemetry::SampleIdentity identity_{};
    std::string map_;
    int sample_{0};
    int living_{0};
    bool zero_baseline_seen_{false};
    bool initialized_{false};
};

}  // namespace kf2::telemetry_pipeline
