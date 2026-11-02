#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include "kf2/telemetry/present_source.hpp"

namespace {
thread_local std::size_t* allocation_counter = nullptr;
}

void* operator new(std::size_t size) {
    if (allocation_counter) ++*allocation_counter;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
std::atomic_bool waiting_to_sleep{false};
std::atomic_bool publication_paused{false}, release_publication{false};

void pause_before_publication(std::stop_token stop) noexcept {
    publication_paused.store(true, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!release_publication.load(std::memory_order_acquire) &&
           !stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
}

using namespace kf2::telemetry;

FrameMetrics reference_windows(std::span<const PresentTimestamp> presents,
                               SampleIdentity identity, std::uint64_t now,
                               std::uint64_t stale, std::uint64_t not_before,
                               std::optional<std::uint64_t> history = 0) {
    const auto window = [&](std::uint64_t duration, std::uint64_t boundary) {
        if (presents.empty()) return presents;
        const auto newest = presents.back().monotonic_ns;
        const auto cutoff = std::max(boundary,
            newest > duration ? newest - duration : 0);
        const auto first = std::lower_bound(presents.begin(), presents.end(),
            cutoff, [](const auto& item, auto at) { return item.monotonic_ns < at; });
        return presents.subspan(static_cast<std::size_t>(first - presents.begin()));
    };
    auto result = aggregate_presents(window(1'000'000'000ULL, not_before), identity, now, stale);
    result.average_fps.reset();
    result.one_percent_low_fps.reset();
    result.p95_ms.reset();
    result.p99_ms.reset();
    result.stutter_count = 0;
    const auto boundary = std::max(not_before,
        history.value_or(std::numeric_limits<std::uint64_t>::max()));
    const auto sustained = aggregate_presents(window(3'000'000'000ULL, boundary), identity, now, stale);
    const auto tail = aggregate_presents(window(5'000'000'000ULL, boundary), identity, now, stale);
    const auto longest = aggregate_presents(window(10'000'000'000ULL, boundary), identity, now, stale);
    if (sustained.fps) result.average_fps = sustained.fps;
    if (sustained.one_percent_low_fps)
        result.sustained_one_percent_low_fps = sustained.one_percent_low_fps;
    if (tail.p95_ms) result.p95_ms = tail.p95_ms;
    if (tail.p99_ms) result.p99_ms = tail.p99_ms;
    if (tail.fps) result.stutter_count = tail.stutter_count;
    if (longest.one_percent_low_fps) result.one_percent_low_fps = longest.one_percent_low_fps;
    return result;
}

bool equal_metrics(const FrameMetrics& actual, const FrameMetrics& expected) {
    return actual.fps == expected.fps && actual.average_fps == expected.average_fps &&
        actual.frame_time_ms == expected.frame_time_ms &&
        actual.sustained_one_percent_low_fps == expected.sustained_one_percent_low_fps &&
        actual.one_percent_low_fps == expected.one_percent_low_fps &&
        actual.p95_ms == expected.p95_ms && actual.p99_ms == expected.p99_ms &&
        actual.stutter_count == expected.stutter_count && actual.age_ns == expected.age_ns &&
        actual.newest_present_ns == expected.newest_present_ns &&
        actual.loss_count == expected.loss_count && actual.quality == expected.quality &&
        actual.reason == expected.reason;
}

int test_shared_window_statistics() {
    const SampleIdentity identity{315, 1};
    constexpr std::uint64_t stale = 500'000'000ULL;
    for (const std::size_t capacity : {2U, 128U, 4096U}) {
        for (const std::size_t count : {0U, 1U, 2U, 99U, 100U, 101U, 301U, 1201U, 2401U}) {
            PresentSource source{identity, capacity};
            CHECK(source.start().has_value());
            std::vector<PresentTimestamp> presents;
            std::uint64_t at = 0;
            for (std::size_t index = 0; index < count; ++index) {
                // Deterministic changing cadence and spikes, including ties in
                // the sorted intervals and 1% nearest-rank boundaries.
                at += index % 113 == 0 ? 150'000'000ULL :
                    1'000'000ULL + (index * 7919 % 31'000'000ULL);
                presents.push_back({identity, at});
            }
            // Late ETW events are admitted in timestamp order; duplicates do
            // not add intervals. Retention must still match the reference.
            for (auto iterator = presents.rbegin(); iterator != presents.rend(); ++iterator)
                CHECK(source.ingest({identity, iterator->monotonic_ns, 1, true, 0, 41}));
            if (!presents.empty())
                CHECK(!source.ingest({identity, presents.back().monotonic_ns, 1, true, 0, 41}));
            if (presents.size() > capacity)
                presents.erase(presents.begin(), presents.end() - static_cast<std::ptrdiff_t>(capacity));
            const auto generation = source.drain(at, stale).source_generation;
            for (const std::uint64_t boundary : {0ULL, at / 2, at, at + 1}) {
                for (const std::uint64_t now : {at, at + stale, at + stale + 1, at > 0 ? at - 1 : 0}) {
                    const auto actual = source.drain(now, stale, boundary);
                    CHECK(equal_metrics(actual, reference_windows(presents, identity, now, stale, boundary)));
                    CHECK(actual.source_generation == generation);
                    CHECK(actual.stream_id == (presents.empty() || boundary > at ? 0 : 41));
                }
            }
        }
    }

    // Exact window edges, a one-sample live window with a usable longer tail,
    // and a second slower swapchain. Source selection and loss recovery stay
    // outside aggregation and must keep their existing semantics.
    PresentSource source{identity, 4096};
    CHECK(source.start().has_value());
    std::vector<PresentTimestamp> presents;
    for (std::uint64_t at : {0ULL, 5'000'000'000ULL, 7'000'000'000ULL,
                             9'000'000'000ULL, 10'000'000'000ULL}) {
        presents.push_back({identity, at});
        CHECK(source.ingest({identity, at, 1, true, 0, 41}));
    }
    CHECK(equal_metrics(source.drain(10'000'000'000ULL, stale),
        reference_windows(presents, identity, 10'000'000'000ULL, stale, 0)));
    presents.push_back({identity, 11'500'000'000ULL});
    CHECK(source.ingest({identity, presents.back().monotonic_ns, 1, true, 4, 41}));
    auto expected = reference_windows(presents, identity, 11'500'000'000ULL, stale, 0);
    expected.loss_count += 4;
    CHECK(equal_metrics(source.drain(11'500'000'000ULL, stale), expected));
    for (std::uint64_t index = 1; index <= 120; ++index) {
        const auto at = 11'500'000'000ULL + index * 16'666'667ULL;
        presents.push_back({identity, at});
        CHECK(source.ingest({identity, at, 1, true, 0, 41}));
        if (index % 4 == 0)
            CHECK(source.ingest({identity, at + 1, 1, true, 0, 42}));
    }
    const auto now = presents.back().monotonic_ns + 1;
    for (std::uint64_t boundary : {0ULL, 11'500'000'001ULL}) {
        expected = reference_windows(presents, identity, now, stale, boundary);
        if (boundary == 0) {
            expected.loss_count += 4;
            if (expected.fps) expected.quality = SampleQuality::degraded;
        }
        const auto actual = source.drain(now, stale, boundary);
        CHECK(equal_metrics(actual, expected));
        CHECK(actual.stream_id == 41);
    }

    // Count allocations on this drain's thread only, not on the independent
    // worker. Reference buffers/ingestion are deliberately outside the budget.
    std::size_t allocations = 0;
    allocation_counter = &allocations;
    const auto measured = source.drain(now, stale);
    allocation_counter = nullptr;
    CHECK(measured.fps.has_value());
    // MSVC's Debug iterator tracking also allocates one proxy per vector.
    constexpr std::size_t budget = _ITERATOR_DEBUG_LEVEL == 0 ? 2 : 4;
    if (allocations > budget) std::cerr << "Drain allocations: " << allocations << '\n';
    CHECK(allocations <= budget);

    PresentSource mixed{identity, 4};
    CHECK(mixed.start().has_value());
    constexpr std::uint64_t mixed_origin = 20'000'000'000ULL;
    for (const auto offset : {0ULL, 32ULL, 16ULL, 64ULL, 48ULL, 80ULL, 112ULL, 96ULL})
        CHECK(mixed.ingest({identity, mixed_origin + offset * 1'000'000ULL, 1, true, 0, 41}));
    CHECK(!mixed.ingest({identity, mixed_origin + 80'000'000ULL, 1, true, 0, 41}));
    CHECK(!mixed.ingest({identity, mixed_origin + 112'000'000ULL, 1, true, 0, 41}));
    CHECK(mixed.ingest({identity, mixed_origin + 128'000'000ULL, 1, true, 0, 41}));
    std::vector<PresentTimestamp> retained;
    for (const auto offset : {80ULL, 96ULL, 112ULL, 128ULL})
        retained.push_back({identity, mixed_origin + offset * 1'000'000ULL});
    const auto mixed_now = mixed_origin + 128'000'001ULL;
    const auto mixed_metrics = mixed.drain(mixed_now, stale);
    CHECK(equal_metrics(mixed_metrics,
        reference_windows(retained, identity, mixed_now, stale, 0)));
    CHECK(mixed_metrics.stream_id == 41);
    return EXIT_SUCCESS;
}

int test_gameplay_history_boundary() {
    const SampleIdentity identity{315, 1};
    constexpr std::uint64_t start = 20'000'000'000ULL;
    constexpr std::uint64_t step = 16'000'000ULL;
    constexpr std::uint64_t stale = 2'000'000'000ULL;
    PresentSource source{identity, 2400};
    CHECK(source.start().has_value());
    std::vector<PresentTimestamp> presents;
    auto at = start;
    for (std::size_t index = 0; index < 720; ++index) {
        at += index == 600 ? 500'000'000ULL : step;
        presents.push_back({identity, at});
    }
    for (auto it = presents.rbegin(); it != presents.rend(); ++it)
        CHECK(source.ingest({identity, it->monotonic_ns, 1, true, 0, 41}));
    const auto raw = source.drain(at, stale);
    CHECK(raw.one_percent_low_fps && *raw.one_percent_low_fps < 20.0);
    const auto diagnostic = source.measure_window(at - 500'000'000ULL, at);
    const std::optional<std::uint64_t> history_boundaries[]{
        0, presents[600].monotonic_ns, at, at + 1, std::nullopt};
    for (const auto history : history_boundaries) {
        source.set_statistics_boundary(history);
        const auto generation = source.drain(at, stale).source_generation;
        source.set_statistics_boundary(history); // No-op must not churn epochs.
        for (const auto boundary : {0ULL, presents[650].monotonic_ns, at + 1}) {
            const auto actual = source.drain(at, stale, boundary);
            CHECK(equal_metrics(actual,
                reference_windows(presents, identity, at, stale, boundary, history)));
            CHECK(actual.source_generation == generation);
        }
        const auto unchanged = source.measure_window(at - 500'000'000ULL, at);
        CHECK(unchanged.generation == diagnostic.generation);
        CHECK(unchanged.count == diagnostic.count);
        CHECK(equal_metrics(unchanged.metrics, diagnostic.metrics));
    }
    // Menu drains allocate only the live copy, no historical sort buffer.
    std::size_t allocations = 0;
    allocation_counter = &allocations;
    const auto menu = source.drain(at, stale);
    allocation_counter = nullptr;
    CHECK(menu.fps == raw.fps && menu.frame_time_ms == raw.frame_time_ms);
    CHECK(!menu.average_fps && !menu.one_percent_low_fps);
    CHECK(allocations <= (_ITERATOR_DEBUG_LEVEL == 0 ? 1U : 3U));

    source.set_statistics_boundary(0);
    source.request_drain(at, stale);
    source.request_drain(at, stale, presents[650].monotonic_ns);
    CHECK(source.wait_for_drain(std::chrono::seconds{2}));
    const auto cached = source.latest_drain();
    CHECK(cached && cached->one_percent_low_fps);
    struct PublicationGuard {
        ~PublicationGuard() {
            detail::set_present_drain_publication_hook(nullptr);
            release_publication.store(true, std::memory_order_release);
        }
    } publication_guard;
    detail::set_present_drain_publication_hook(&pause_before_publication);
    source.request_drain(at, stale);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!publication_paused.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    CHECK(publication_paused.load(std::memory_order_acquire));
    source.set_statistics_boundary(std::nullopt);
    const auto hidden = source.latest_drain();
    CHECK(hidden && hidden->fps == cached->fps);
    CHECK(hidden->source_generation == cached->source_generation);
    CHECK(hidden->quality == cached->quality && hidden->stream_id == 41);
    CHECK(!hidden->average_fps && !hidden->one_percent_low_fps);
    CHECK(!source.latest_drain(presents[650].monotonic_ns));
    detail::set_present_drain_publication_hook(nullptr);
    release_publication.store(true, std::memory_order_release);
    CHECK(source.wait_for_drain(std::chrono::seconds{2}));
    CHECK(source.latest_drain()->source_generation == cached->source_generation);
    CHECK(!source.latest_drain()->average_fps); // Old worker result was rejected.

    // Late pre-resume events cannot reintroduce a menu-gap interval.
    const auto resumed = at + 1'000'000'000ULL;
    source.set_statistics_boundary(resumed);
    CHECK(source.ingest({identity, resumed - step, 1, true, 0, 41}));
    CHECK(source.ingest({identity, resumed, 1, true, 0, 41}));
    CHECK(!source.drain(resumed, stale).average_fps);
    CHECK(source.ingest({identity, resumed + step, 1, true, 0, 41}));
    auto clean = source.drain(resumed + step, stale);
    CHECK(clean.average_fps == 62.5 && clean.one_percent_low_fps == 62.5);
    CHECK(source.ingest({identity, resumed + step + 500'000'000ULL, 1, true, 0, 41}));
    auto stalled = source.drain(resumed + step + 500'000'000ULL, stale);
    CHECK(stalled.one_percent_low_fps == 2.0); // Real gameplay stalls still count.
    CHECK(source.ingest({identity, resumed + step + 500'000'001ULL, 1, false, 7, 41}) == false);
    source.set_statistics_boundary(std::nullopt);
    const auto lost = source.drain(resumed + step + 500'000'001ULL, stale);
    CHECK(lost.loss_count == 8 && lost.quality == SampleQuality::degraded);
    CHECK(!lost.one_percent_low_fps);
    return EXIT_SUCCESS;
}

void pause_before_wait(std::stop_token stop) noexcept {
    waiting_to_sleep.store(true, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{200};
    // Force destruction after the false predicate but before wait unlocks.
    // With the fixed protocol, destruction waits for this mutex to release.
    while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
}

int test_uncounted_capture_loss() {
    constexpr SampleIdentity identity{42, 123};
    PresentSource source{identity, 128};
    CHECK(!source.record_loss(identity, 1'017'000'000ULL, 0, true));
    for (unsigned int reset = 0; reset < 4; ++reset) {
        CHECK(source.start().has_value());
        for (const auto at : {1'000'000'000ULL, 1'016'000'000ULL})
            CHECK(source.ingest({identity, at, 1, true, 0, 7}));
        CHECK(!source.record_loss(identity, 1'017'000'000ULL, 0));
        CHECK(!source.record_loss({99, 1}, 1'017'000'000ULL, 0, true));
        source.request_drain(1'017'000'000ULL, 1'000'000'000ULL);
        CHECK(source.wait_for_drain(std::chrono::seconds{2}));
        CHECK(source.latest_drain()->quality == SampleQuality::good);
        const auto before = source.measure_window(1'000'000'000ULL, 1'016'000'000ULL);
        CHECK(before.complete);
        std::size_t allocations = 0;
        allocation_counter = &allocations;
        const auto committed = source.record_loss(identity, 1'017'000'000ULL, 0, true);
        allocation_counter = nullptr;
        CHECK(committed && allocations == 0);
        CHECK(!source.latest_drain());
        for (const auto at : {1'032'000'000ULL, 1'048'000'000ULL})
            CHECK(source.ingest({identity, at, 1, true, 0, 7}));
        const auto marked = source.drain(1'049'000'000ULL, 1'000'000'000ULL);
        CHECK(marked.fps && *marked.fps == 62.5);
        CHECK(marked.quality == SampleQuality::degraded && marked.loss_count == 0);
        const auto crossed = source.measure_window(1'000'000'000ULL, 1'048'000'000ULL);
        CHECK(!crossed.complete && crossed.count == 0);
        CHECK(crossed.generation != before.generation);
        CHECK(source.measure_window(1'032'000'000ULL, 1'048'000'000ULL).complete);
        CHECK(source.drain(1'049'000'000ULL, 1'000'000'000ULL,
                            1'032'000'000ULL).quality == SampleQuality::good);
        for (const auto at : {12'000'000'000ULL, 12'016'000'000ULL})
            CHECK(source.ingest({identity, at, 1, true, 0, 7}));
        const auto recovered = source.drain(12'017'000'000ULL, 1'000'000'000ULL);
        CHECK(recovered.fps && recovered.quality == SampleQuality::good);
        CHECK(recovered.loss_count == 0);
        // Each lifecycle boundary must also discard a still-active marker.
        CHECK(source.record_loss(identity, 12'017'000'000ULL, 0, true));
        if (reset == 0) source.reset_statistics();
        if (reset == 1) source.bind(identity);
        if (reset == 2) CHECK(source.start().has_value());
        if (reset == 3) {
            CHECK(source.stop().has_value());
            CHECK(!source.record_loss(identity, 13'000'000'000ULL, 0, true));
            CHECK(source.start().has_value());
        }
        for (const auto at : {13'000'000'000ULL, 13'016'000'000ULL})
            CHECK(source.ingest({identity, at, 1, true, 0, 7}));
        CHECK(source.drain(13'017'000'000ULL, 1'000'000'000ULL).quality == SampleQuality::good);
    }
    CHECK(!source.ingest({identity, 14'000'000'000ULL, 99, true, 0, 7}));
    CHECK(!source.record_loss(identity, 14'017'000'000ULL, 0, true));
    return EXIT_SUCCESS;
}

int test_shutdown_before_wait() {
    kf2::telemetry::detail::set_present_drain_wait_hook(&pause_before_wait);
    auto source = std::make_unique<kf2::telemetry::PresentSource>(
        kf2::telemetry::SampleIdentity{42, 123}, 120);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!waiting_to_sleep.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    CHECK(waiting_to_sleep.load(std::memory_order_acquire));
    source.reset();
    kf2::telemetry::detail::set_present_drain_wait_hook(nullptr);
    return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--shutdown-before-wait")
        return test_shutdown_before_wait();
    CHECK(test_shared_window_statistics() == EXIT_SUCCESS);
    CHECK(test_uncounted_capture_loss() == EXIT_SUCCESS);
    CHECK(test_gameplay_history_boundary() == EXIT_SUCCESS);
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
    CHECK(fresh.newest_present_ns == 2'920'000'000ULL);
    CHECK(fresh.source_generation != 0);
    CHECK(fresh.stream_id == 0);

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

    // A retired high-rate swapchain must not hide a slower stream that is
    // still presenting. Candidate freshness is relative to the requested
    // observation time, not to each stream's own newest sample.
    PresentSource retired_fast_stream{game, 256};
    CHECK(retired_fast_stream.start().has_value());
    constexpr std::uint64_t retired_start_ns = 10'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(retired_fast_stream.ingest(
            {game, retired_start_ns + index * 8'333'333ULL,
             1, true, 0, 301}));
    }
    constexpr std::uint64_t current_start_ns = 12'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 30; ++index) {
        CHECK(retired_fast_stream.ingest(
            {game, current_start_ns + index * 33'333'333ULL,
             1, true, 0, 302}));
    }
    constexpr std::uint64_t current_now_ns =
        current_start_ns + 30 * 33'333'333ULL;
    const auto current_slow_stream = retired_fast_stream.drain(
        current_now_ns, 500'000'000ULL);
    CHECK(current_slow_stream.fps.has_value());
    CHECK(*current_slow_stream.fps > 29.0 &&
          *current_slow_stream.fps < 31.0);

    PresentSource stale_stream_only{game, 256};
    CHECK(stale_stream_only.start().has_value());
    for (std::uint64_t index = 0; index <= 120; ++index) {
        CHECK(stale_stream_only.ingest(
            {game, retired_start_ns + index * 8'333'333ULL,
             1, true, 0, 301}));
    }
    const auto stale_only = stale_stream_only.drain(
        current_now_ns, 500'000'000ULL);
    CHECK(!stale_only.fps.has_value());
    CHECK(stale_only.reason == UnavailableReason::stale);

    // Equal fresh candidates use the same stable stream-ID tie break as the
    // diagnostic window path, independent of unordered-map iteration order.
    PresentSource deterministic_fresh_streams{game, 64};
    CHECK(deterministic_fresh_streams.start().has_value());
    constexpr std::uint64_t deterministic_end_ns = 15'000'000'000ULL;
    for (std::uint64_t index = 0; index <= 30; ++index) {
        CHECK(deterministic_fresh_streams.ingest(
            {game, deterministic_end_ns - (30 - index) * 16'000'000ULL,
             1, true, 0, 401}));
        CHECK(deterministic_fresh_streams.ingest(
            {game, deterministic_end_ns - (30 - index) * 33'000'000ULL,
             1, true, 0, 402}));
    }
    const auto deterministic_fresh = deterministic_fresh_streams.drain(
        deterministic_end_ns, 500'000'000ULL);
    CHECK(deterministic_fresh.fps.has_value());
    CHECK(*deterministic_fresh.fps > 62.0 &&
          *deterministic_fresh.fps < 63.0);

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
    CHECK(lossy.source_generation != fresh.source_generation);

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
    CHECK(reset_metrics.source_generation != responsive_metrics.source_generation);

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
    CHECK(asynchronous_metrics->newest_present_ns == 9'920'000'000ULL);
    const auto cached = asynchronous.latest_drain();
    CHECK(cached->newest_present_ns == asynchronous_metrics->newest_present_ns);
    CHECK(cached->source_generation == asynchronous_metrics->source_generation);
    asynchronous.request_drain(10'041'000'000ULL, 500'000'000ULL);
    CHECK(asynchronous.wait_for_drain(std::chrono::seconds{2}));
    const auto repeated = asynchronous.latest_drain();
    CHECK(repeated->age_ns == 121'000'000ULL);
    CHECK(repeated->newest_present_ns == asynchronous_metrics->newest_present_ns);
    CHECK(repeated->source_generation == asynchronous_metrics->source_generation);
    CHECK(repeated->stream_id == asynchronous_metrics->stream_id);
    CHECK(repeated->fps == asynchronous_metrics->fps);

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
    CHECK(asynchronous_bounded->newest_present_ns ==
          asynchronous_metrics->newest_present_ns);
    CHECK(asynchronous_bounded->source_generation ==
          asynchronous_metrics->source_generation);

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
