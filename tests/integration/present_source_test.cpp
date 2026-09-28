#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
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

    PresentSource reordered{game, 16};
    CHECK(reordered.start().has_value());
    CHECK(reordered.ingest({game, 1'000'000'000ULL, 1, true, 0}));
    CHECK(reordered.ingest({game, 1'032'000'000ULL, 1, true, 0}));
    CHECK(reordered.ingest({game, 1'016'000'000ULL, 1, true, 0}));
    const auto reordered_metrics = reordered.drain(
        1'032'000'000ULL, 500'000'000ULL);
    CHECK(reordered_metrics.fps.has_value());
    CHECK(*reordered_metrics.fps > 62.0 && *reordered_metrics.fps < 63.0);
    CHECK(reordered_metrics.quality == SampleQuality::good);

    PresentSource multiple_streams{game, 64};
    CHECK(multiple_streams.start().has_value());
    for (std::uint64_t index = 0; index <= 32; ++index) {
        CHECK(multiple_streams.ingest(
            {game, 2'000'000'000ULL + index * 16'000'000ULL,
             1, true, 0, 101}));
        CHECK(multiple_streams.ingest(
            {game, 2'001'000'000ULL + index * 16'000'000ULL,
             1, true, 0, 202}));
    }
    const auto isolated_stream = multiple_streams.drain(
        2'513'000'000ULL, 500'000'000ULL);
    CHECK(isolated_stream.fps.has_value());
    CHECK(*isolated_stream.fps > 62.0 && *isolated_stream.fps < 63.0);
    CHECK(isolated_stream.quality == SampleQuality::good);

    // Retention is based on inactivity, not sample count. In particular, the
    // active stream must survive admission of a replacement even when every
    // older stream owns more samples.
    PresentSource active_stream_retention{game, 64};
    CHECK(active_stream_retention.start().has_value());
    for (std::uint64_t stream_id = 1; stream_id <= 15; ++stream_id) {
        for (std::uint64_t sample = 0; sample < 4; ++sample) {
            CHECK(active_stream_retention.ingest(
                {game, stream_id * 100'000'000ULL + sample * 16'000'000ULL,
                 1, true, 0, stream_id}));
        }
    }
    const PresentEvent previous_active{
        game, 1'600'000'000ULL, 1, true, 0, 16};
    CHECK(active_stream_retention.ingest(previous_active));
    CHECK(active_stream_retention.ingest(
        {game, 1'700'000'000ULL, 1, true, 0, 17}));
    CHECK(!active_stream_retention.ingest(previous_active));

    // A long-running process can recreate its swapchain many times without a
    // new process binding. The bounded stream cache must retire an inactive
    // swapchain instead of permanently rejecting the seventeenth identity.
    PresentSource recreated_streams{game, 256};
    CHECK(recreated_streams.start().has_value());
    constexpr std::uint64_t recreation_start_ns = 3'000'000'000ULL;
    constexpr std::uint64_t recreation_step_ns = 100'000'000ULL;
    for (std::uint64_t stream_id = 1; stream_id <= 24; ++stream_id) {
        const auto timestamp = recreation_start_ns +
                               stream_id * recreation_step_ns;
        CHECK(recreated_streams.ingest(
            {game, timestamp, 1, true, 0, stream_id}));
        CHECK(recreated_streams.ingest(
            {game, timestamp + 16'000'000ULL, 1, true, 0, stream_id}));
    }
    constexpr std::uint64_t newest_stream_id = 24;
    const auto newest_start_ns = recreation_start_ns +
                                 newest_stream_id * recreation_step_ns;
    for (std::uint64_t index = 2; index <= 64; ++index) {
        CHECK(recreated_streams.ingest(
            {game, newest_start_ns + index * 16'000'000ULL,
             1, true, 0, newest_stream_id}));
    }
    const auto newest_stream = recreated_streams.drain(
        newest_start_ns + 64 * 16'000'000ULL, 500'000'000ULL);
    CHECK(newest_stream.fps.has_value());
    CHECK(*newest_stream.fps > 62.0 && *newest_stream.fps < 63.0);

    // Exercise many more transitions over synthetic long-session time. The
    // current stream remains measurable, and a diagnostic interval that
    // crosses the latest swapchain boundary remains explicitly incomplete.
    PresentSource long_session{game, 256};
    CHECK(long_session.start().has_value());
    constexpr std::uint64_t long_session_step_ns = 2'000'000'000ULL;
    for (std::uint64_t stream_id = 1; stream_id <= 64; ++stream_id) {
        const auto timestamp = stream_id * long_session_step_ns;
        CHECK(long_session.ingest(
            {game, timestamp, 1, true, 0, stream_id}));
    }
    constexpr std::uint64_t final_stream_id = 64;
    constexpr std::uint64_t final_boundary_ns =
        final_stream_id * long_session_step_ns;
    for (std::uint64_t index = 1; index <= 64; ++index) {
        CHECK(long_session.ingest(
            {game, final_boundary_ns + index * 16'000'000ULL,
             1, true, 0, final_stream_id}));
    }
    const auto crossing_boundary = long_session.measure_window(
        final_boundary_ns - 16'000'000ULL,
        final_boundary_ns + 64 * 16'000'000ULL);
    CHECK(!crossing_boundary.complete);
    const auto final_window = long_session.measure_window(
        final_boundary_ns + 16'000'000ULL,
        final_boundary_ns + 64 * 16'000'000ULL);
    CHECK(final_window.stream_id == final_stream_id);
    CHECK(final_window.metrics.fps.has_value());
    CHECK(final_window.complete);

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
    CHECK(responsive_metrics.sustained_one_percent_low_fps.has_value());
    CHECK(*responsive_metrics.sustained_one_percent_low_fps > 29.0 &&
          *responsive_metrics.sustained_one_percent_low_fps < 31.0);
    CHECK(responsive_metrics.p95_ms.has_value());
    CHECK(*responsive_metrics.p95_ms > 30.0);
    CHECK(responsive_metrics.one_percent_low_fps.has_value());
    CHECK(*responsive_metrics.one_percent_low_fps > 29.0 &&
          *responsive_metrics.one_percent_low_fps < 31.0);

    // Live FPS uses the same one-second observation period as common external
    // overlays. This avoids a systematic display mismatch when cadence changes
    // inside the first quarter of that second.
    PresentSource one_second_live{game, 256};
    CHECK(one_second_live.start().has_value());
    constexpr std::uint64_t live_start_ns = 10'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 15; ++index) {
        CHECK(one_second_live.ingest(
            {game, live_start_ns + index * 16'666'667ULL,
             1, true, 0}));
    }
    constexpr std::uint64_t fast_start_ns =
        live_start_ns + 15 * 16'666'667ULL;
    for (std::uint64_t index = 1; index <= 90; ++index) {
        CHECK(one_second_live.ingest(
            {game, fast_start_ns + index * 8'333'333ULL,
             1, true, 0}));
    }
    const auto one_second_metrics = one_second_live.drain(
        fast_start_ns + 90 * 8'333'333ULL, 500'000'000ULL);
    CHECK(one_second_metrics.fps.has_value());
    CHECK(*one_second_metrics.fps > 104.0 &&
          *one_second_metrics.fps < 106.0);

    // An authenticated quality correction starts an adaptive-only window.
    // Older slow frames stay visible in the UI but cannot trigger more steps.
    const auto corrected = responsive.drain(
        transition_ns + 120 * 8'333'333ULL, 500'000'000ULL,
        transition_ns);
    CHECK(corrected.fps.has_value());
    CHECK(corrected.average_fps.has_value());
    CHECK(*corrected.average_fps > 119.0);
    CHECK(*corrected.sustained_one_percent_low_fps > 119.0);
    CHECK(*corrected.one_percent_low_fps > 119.0);
    CHECK(*corrected.p95_ms < 9.0);
    CHECK(corrected.stutter_count == 0);
    const auto ui_after_correction = responsive.drain(
        transition_ns + 120 * 8'333'333ULL, 500'000'000ULL);
    CHECK(ui_after_correction.one_percent_low_fps ==
          responsive_metrics.one_percent_low_fps);
    // A late ETW event from before the receipt cannot recontaminate the view.
    CHECK(responsive.ingest({game, transition_ns - 1'000'000ULL, 1, true, 0}));
    CHECK(responsive.drain(transition_ns + 120 * 8'333'333ULL,
              500'000'000ULL, transition_ns).one_percent_low_fps ==
          corrected.one_percent_low_fps);
    CHECK(!responsive.drain(7'000'000'000ULL, 500'000'000ULL,
                            7'000'000'000ULL).fps.has_value());

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
    CHECK(reset_metrics.sustained_one_percent_low_fps.has_value());
    CHECK(reset_metrics.one_percent_low_fps.has_value());
    CHECK(*reset_metrics.fps > 119.0);
    CHECK(*reset_metrics.average_fps > 119.0);
    CHECK(*reset_metrics.sustained_one_percent_low_fps > 119.0);
    CHECK(*reset_metrics.one_percent_low_fps > 119.0);

    PresentSource asynchronous{game, 256};
    CHECK(asynchronous.start().has_value());
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(asynchronous.ingest(
            {game, 8'000'000'000ULL + index * 16'000'000ULL,
             1, true, 0}));
    }
    asynchronous.request_drain(9'921'000'000ULL, 500'000'000ULL);
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    const auto asynchronous_metrics = asynchronous.latest_drain();
    CHECK(asynchronous_metrics.has_value());
    CHECK(asynchronous_metrics->fps.has_value());
    CHECK(*asynchronous_metrics->fps > 62.0 &&
          *asynchronous_metrics->fps < 63.0);

    detail::fail_next_present_drain_publication();
    asynchronous.request_drain(9'921'000'000ULL, 500'000'000ULL);
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    const auto failed_drain = asynchronous.latest_drain();
    CHECK(failed_drain.has_value());
    CHECK(failed_drain->reason == UnavailableReason::source_failure);
    asynchronous.request_drain(9'921'000'000ULL, 500'000'000ULL);
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    CHECK(asynchronous.latest_drain()->fps.has_value());

    constexpr std::uint64_t asynchronous_boundary_ns = 8'500'000'000ULL;
    asynchronous.request_drain(
        9'921'000'000ULL, 500'000'000ULL, asynchronous_boundary_ns);
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    const auto asynchronous_bounded =
        asynchronous.latest_drain(asynchronous_boundary_ns);
    CHECK(asynchronous_bounded.has_value());
    CHECK(asynchronous_bounded->fps.has_value());

    // Live collection can continuously replace its coalesced request while a
    // drain is running. A queued Adaptive boundary must still be selected on
    // the next completed drain instead of waiting for live traffic to stop.
    PresentSource fair{game, 50'000};
    CHECK(fair.start().has_value());
    constexpr std::uint64_t fair_start_ns = 20'000'000'000ULL;
    constexpr std::uint64_t fair_interval_ns = 200'000ULL;
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        CHECK(fair.ingest(
            {game, fair_start_ns + index * fair_interval_ns,
             1, true, 0}));
    }
    constexpr std::uint64_t fair_now_ns =
        fair_start_ns + 49'999 * fair_interval_ns;
    constexpr std::uint64_t fair_boundary_ns =
        fair_start_ns + 25'000 * fair_interval_ns;
    std::atomic_bool keep_requesting_live{true};
    std::jthread live_requester{[&](std::stop_token stop) {
        while (!stop.stop_requested() &&
               keep_requesting_live.load(std::memory_order_relaxed)) {
            fair.request_drain(fair_now_ns, 500'000'000ULL);
            std::this_thread::yield();
        }
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    fair.request_drain(
        fair_now_ns, 500'000'000ULL, fair_boundary_ns);
    const auto fair_deadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds{500};
    while (!fair.latest_drain(fair_boundary_ns).has_value() &&
           std::chrono::steady_clock::now() < fair_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto fair_bounded = fair.latest_drain(fair_boundary_ns);
    keep_requesting_live.store(false, std::memory_order_relaxed);
    live_requester.request_stop();
    live_requester.join();
    CHECK(fair_bounded.has_value());
    CHECK(fair_bounded->fps.has_value());
    CHECK(fair.wait_for_drain(std::chrono::seconds{2}));
    CHECK(fair.latest_drain().has_value());

    // A schema failure must revoke both previously published paths and any
    // replacement request that the worker has already accepted. Waiting for
    // the worker afterward proves the old generation cannot publish late.
    asynchronous.request_drain(9'921'000'000ULL, 500'000'000ULL);
    asynchronous.request_drain(
        9'921'000'000ULL, 500'000'000ULL, asynchronous_boundary_ns);
    CHECK(!asynchronous.ingest(
        {game, 9'937'000'000ULL, 99, true, 0}));
    CHECK(!asynchronous.latest_drain().has_value());
    CHECK(!asynchronous.latest_drain(asynchronous_boundary_ns).has_value());
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    CHECK(!asynchronous.latest_drain().has_value());
    CHECK(!asynchronous.latest_drain(asynchronous_boundary_ns).has_value());
    const auto asynchronous_schema_failure = asynchronous.drain(
        9'937'000'000ULL, 500'000'000ULL);
    CHECK(!asynchronous_schema_failure.fps.has_value());
    CHECK(asynchronous_schema_failure.reason ==
          UnavailableReason::source_failure);
    return EXIT_SUCCESS;
}
