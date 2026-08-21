#include <cstdlib>
#include <iostream>
#include <limits>

#include "kf2/benchmark/benchmark.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::benchmark;
    const Run baseline{"map-bioticslab-wave10", "config-a", 100.0, 70.0, 18.0, 4};
    CHECK(validate(baseline).has_value());
    CHECK(!validate({"", "config-a", 100.0, 70.0, 18.0, 4}).has_value());
    CHECK(!validate({"map", "config-a", 0.0, 70.0, 18.0, 4}).has_value());

    const auto improved = compare(baseline, {"map-bioticslab-wave10", "config-b",
                                             106.0, 74.0, 16.0, 3});
    CHECK(improved.outcome == Comparison::improved);
    CHECK(improved.average_fps_change_percent > 5.0);

    const auto regressed = compare(baseline, {"map-bioticslab-wave10", "config-c",
                                              95.0, 65.0, 20.0, 6});
    CHECK(regressed.outcome == Comparison::regressed);
    CHECK(compare(baseline, {"another-map", "config-c", 110.0, 80.0, 15.0, 2}).outcome ==
          Comparison::incompatible);
    CHECK(compare(baseline, {"map-bioticslab-wave10", "config-d", 101.0, 70.0, 18.1, 4}).outcome ==
          Comparison::inconclusive);

    const auto round_trip = parse(serialize(baseline));
    CHECK(round_trip.has_value());
    CHECK(round_trip.value().scenario_id == baseline.scenario_id);
    CHECK(round_trip.value().stutter_count == baseline.stutter_count);
    CHECK(!parse("scenario=x\n").has_value());

    std::vector<HistoryEntry> history;
    history = append_history(std::move(history), baseline, 2);
    history = append_history(std::move(history),
        {"map-bioticslab-wave10", "config-b", 106.0, 74.0, 16.0, 3}, 2);
    history = append_history(std::move(history),
        {"map-bioticslab-wave10", "config-c", 107.0, 75.0, 15.9, 2}, 2);
    CHECK(history.size() == 2);
    CHECK(history.front().sequence == 2);
    CHECK(history.back().sequence == 3);
    const auto history_round_trip = parse_history(serialize_history(history));
    CHECK(history_round_trip.has_value());
    CHECK(history_round_trip.value().size() == 2);
    CHECK(history_round_trip.value().back().run.configuration_id == "config-c");
    CHECK(!parse_history("version=2\n").has_value());
    CHECK(!parse_history("version=1\nentry=2|00\n").has_value());
    std::vector<HistoryEntry> overflow{{
        std::numeric_limits<std::uint64_t>::max(), baseline}};
    overflow = append_history(std::move(overflow),
        {"map-bioticslab-wave10", "config-b", 106.0, 74.0, 16.0, 3}, 2);
    CHECK(overflow[0].sequence == 1);
    CHECK(overflow[1].sequence == 2);
    CHECK(parse_history(serialize_history(overflow)).has_value());
    return EXIT_SUCCESS;
}
