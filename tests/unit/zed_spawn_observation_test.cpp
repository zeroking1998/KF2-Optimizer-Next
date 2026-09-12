#include <cstdlib>
#include <iostream>

#include "features/telemetry/zed_spawn_observation.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

kf2::telemetry_pipeline::TelemetryFrame frame(
    std::uint64_t process_start_id, std::string map, int sample, int living,
    std::uint64_t observed_at_ns = 20'000'000'000ULL) {
    kf2::telemetry_pipeline::TelemetryFrame value;
    value.identity = {42, process_start_id};
    value.observed_at_ns = observed_at_ns;
    value.active_gameplay = true;
    value.offline_gameplay = true;
    kf2::game::GameLogSession gameplay;
    gameplay.map = std::move(map);
    gameplay.net_mode = "NM_Standalone";
    gameplay.telemetry_sample = sample;
    gameplay.telemetry_living_zeds = living;
    gameplay.telemetry_observed_ns = observed_at_ns;
    value.gameplay = std::move(gameplay);
    return value;
}

}  // namespace

int main() {
    using kf2::telemetry_pipeline::ZedSpawnObservationTracker;

    ZedSpawnObservationTracker tracker;
    CHECK(!tracker.observe(frame(100, "KF-BioticsLab", 1, 0)));
    CHECK(!tracker.observe(frame(100, "KF-BioticsLab", 1, 9)));

    const auto first = tracker.observe(frame(100, "KF-BioticsLab", 2, 9));
    CHECK(first.has_value());
    CHECK(first->previous_living == 0);
    CHECK(first->current_living == 9);
    CHECK(first->delta == 9);
    CHECK(first->first_group);

    const auto reinforcement =
        tracker.observe(frame(100, "KF-BioticsLab", 3, 12));
    CHECK(reinforcement.has_value());
    CHECK(reinforcement->delta == 3);
    CHECK(!reinforcement->first_group);

    CHECK(!tracker.observe(frame(100, "KF-BioticsLab", 4, 4)));
    CHECK(!tracker.observe(frame(100, "KF-BioticsLab", 5, 0)));
    const auto next_group =
        tracker.observe(frame(100, "KF-BioticsLab", 6, 6));
    CHECK(next_group.has_value());
    CHECK(next_group->first_group);

    // Attaching mid-match establishes a baseline rather than reporting a
    // false spawn burst.
    CHECK(!tracker.observe(frame(100, "KF-Nightmare", 20, 35)));
    const auto later_increase =
        tracker.observe(frame(100, "KF-Nightmare", 21, 37));
    CHECK(later_increase.has_value());
    CHECK(!later_increase->first_group);

    // A provider sample reset and a new process both establish new baselines.
    CHECK(!tracker.observe(frame(100, "KF-Nightmare", 1, 8)));
    CHECK(!tracker.observe(frame(200, "KF-Nightmare", 2, 10)));

    auto inactive = frame(200, "KF-Nightmare", 3, 14);
    inactive.active_gameplay = false;
    CHECK(!tracker.observe(inactive));

    auto stale = frame(200, "KF-Nightmare", 3, 14);
    stale.gameplay->telemetry_observed_ns =
        stale.observed_at_ns - kf2::game::kGameLogObservationFreshnessNs - 1;
    CHECK(!tracker.observe(stale));

    return EXIT_SUCCESS;
}
