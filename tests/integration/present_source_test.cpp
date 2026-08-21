#include <cstdlib>
#include <iostream>
#include "kf2/telemetry/present_source.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    const SampleIdentity game{1234, 5678};
    PresentSource source{game, 256};
    CHECK(!source.drain(1, 500).fps.has_value());
    CHECK(source.start().has_value());
    for (std::uint64_t i = 0; i <= 120; ++i) {
        CHECK(source.ingest({game, 1'000'000'000ULL + i * 16'000'000ULL,
                             1, true, 0}));
    }
    auto fresh = source.drain(2'921'000'000ULL, 500'000'000ULL);
    CHECK(fresh.fps.has_value());
    CHECK(*fresh.fps > 62.0 && *fresh.fps < 63.0);
    CHECK(fresh.quality == SampleQuality::good);

    CHECK(!source.ingest({{99, 88}, 2'922'000'000ULL, 1, true, 0}));
    CHECK(source.ingest({game, 2'936'000'000ULL, 1, true, 4}));
    auto lossy = source.drain(2'936'000'000ULL, 500'000'000ULL);
    CHECK(lossy.fps.has_value());
    CHECK(lossy.quality == SampleQuality::degraded);
    CHECK(lossy.loss_count >= 4);

    CHECK(!source.ingest({game, 2'952'000'000ULL, 99, true, 0}));
    auto schema = source.drain(2'952'000'000ULL, 500'000'000ULL);
    CHECK(!schema.fps.has_value());
    CHECK(schema.reason == UnavailableReason::source_failure);

    CHECK(source.stop().has_value());
    CHECK(!source.ingest({game, 4'000'000'000ULL, 1, true, 0}));
    CHECK(!source.drain(4'000'000'000ULL, 500'000'000ULL).fps.has_value());
    CHECK(source.start().has_value());
    CHECK(!source.drain(4'000'000'000ULL, 500'000'000ULL).fps.has_value());
    CHECK(source.ingest({game, 4'016'000'000ULL, 1, true, 0}));
    CHECK(source.ingest({game, 4'032'000'000ULL, 1, true, 0}));
    CHECK(source.drain(4'032'000'000ULL, 500'000'000ULL).fps.has_value());

    source.bind({game.pid, game.process_start_id + 1});
    CHECK(!source.drain(5'000'000'000ULL, 500'000'000ULL).fps.has_value());
    CHECK(source.ingest({{game.pid, game.process_start_id + 1},
                         5'016'000'000ULL, 1, true, 0}));
    CHECK(source.ingest({{game.pid, game.process_start_id + 1},
                         5'032'000'000ULL, 1, true, 0}));
    CHECK(source.drain(5'032'000'000ULL, 500'000'000ULL).fps.has_value());

    PresentSource responsive{game, 600};
    CHECK(responsive.start().has_value());
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(responsive.ingest({game, 1'000'000'000ULL +
                                          index * 33'333'333ULL,
                                 1, true, 0}));
    }
    const auto transition_ns = 5'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(responsive.ingest({game, transition_ns +
                                          index * 8'333'333ULL,
                                 1, true, 0}));
    }
    const auto responsive_metrics = responsive.drain(
        transition_ns + 120 * 8'333'333ULL, 500'000'000ULL);
    CHECK(responsive_metrics.fps.has_value());
    CHECK(*responsive_metrics.fps > 119.0 && *responsive_metrics.fps < 121.0);
    CHECK(responsive_metrics.average_fps.has_value());
    CHECK(*responsive_metrics.average_fps < *responsive_metrics.fps);
    CHECK(responsive_metrics.p95_ms.has_value());
    CHECK(*responsive_metrics.p95_ms > 30.0);
    CHECK(responsive_metrics.one_percent_low_fps.has_value());
    CHECK(*responsive_metrics.one_percent_low_fps > 29.0 &&
          *responsive_metrics.one_percent_low_fps < 31.0);

    responsive.reset_statistics();
    CHECK(!responsive.drain(
        transition_ns + 120 * 8'333'333ULL,
        500'000'000ULL).fps.has_value());
    const auto reset_ns = 7'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(responsive.ingest({game, reset_ns + index * 8'333'333ULL,
                                 1, true, 0}));
    }
    const auto reset_metrics = responsive.drain(
        reset_ns + 120 * 8'333'333ULL, 500'000'000ULL);
    CHECK(reset_metrics.fps.has_value());
    CHECK(reset_metrics.average_fps.has_value());
    CHECK(reset_metrics.one_percent_low_fps.has_value());
    CHECK(*reset_metrics.fps > 119.0);
    CHECK(*reset_metrics.average_fps > 119.0);
    CHECK(*reset_metrics.one_percent_low_fps > 119.0);
    return EXIT_SUCCESS;
}
