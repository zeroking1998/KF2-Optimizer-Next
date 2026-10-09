#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace {
thread_local bool count_allocations{};
thread_local std::size_t allocations{};
thread_local std::size_t interval_buffer_bytes{};
}

void* operator new(std::size_t size) {
    // Exclude the Debug STL's separate iterator bookkeeping allocations.
    if (count_allocations && size == interval_buffer_bytes) ++allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::telemetry;
    const SampleIdentity game{4242, 9001};
    std::vector<PresentTimestamp> presents;
    for (std::uint64_t index = 0; index <= 120; ++index) {
        presents.push_back({game, 1'000'000'000ULL + index * 16'666'667ULL});
    }
    interval_buffer_bytes = (presents.size() - 1) * sizeof(double);
    count_allocations = true;
    const auto sixty = aggregate_presents(
        presents, game, presents.back().monotonic_ns + 1'000'000ULL,
        500'000'000ULL);
    count_allocations = false;
    std::cout << "Interval buffers allocated: " << allocations << '\n';
    CHECK(allocations == 1);
    CHECK(sixty.fps.has_value());
    CHECK(std::abs(*sixty.fps - 60.0) < 0.01);
    CHECK(sixty.frame_time_ms.has_value());
    CHECK(std::abs(*sixty.frame_time_ms - 16.666667) < 0.001);
    CHECK(sixty.p95_ms.has_value());
    CHECK(sixty.p99_ms.has_value());
    CHECK(sixty.one_percent_low_fps.has_value());
    CHECK(sixty.quality == SampleQuality::good);
    CHECK(sixty.loss_count == 0);

    const std::vector<PresentTimestamp> spike{
        {game, 1'000'000'000ULL}, {game, 1'995'000'000ULL},
        {game, 1'996'000'000ULL}, {game, 1'997'000'000ULL},
        {game, 1'998'000'000ULL}, {game, 1'999'000'000ULL},
        {game, 2'000'000'000ULL}};
    const auto original_spike = spike;
    const auto slow = aggregate_presents(spike, game, 2'000'000'000ULL,
                                         500'000'000ULL);
    CHECK(slow.fps == 6.0);
    CHECK(slow.frame_time_ms == 1000.0 / 6.0);
    CHECK(slow.p95_ms == 995.0);
    CHECK(slow.p99_ms == 995.0);
    CHECK(slow.one_percent_low_fps == 1000.0 / 995.0);
    CHECK(slow.stutter_count == 1);
    CHECK(slow.quality == SampleQuality::good);
    for (std::size_t index = 0; index < spike.size(); ++index) {
        CHECK(spike[index].identity == original_spike[index].identity);
        CHECK(spike[index].monotonic_ns == original_spike[index].monotonic_ns);
    }

    auto imperfect = presents;
    imperfect.insert(imperfect.begin() + 10, imperfect[9]);
    imperfect.insert(imperfect.begin() + 20,
                      {game, imperfect[18].monotonic_ns - 1});
    const auto degraded = aggregate_presents(
        imperfect, game, imperfect.back().monotonic_ns, 500'000'000ULL);
    CHECK(degraded.fps.has_value());
    CHECK(degraded.quality == SampleQuality::degraded);
    CHECK(degraded.loss_count == 2);

    const auto empty = aggregate_presents({}, game, 2'000'000'000ULL,
                                          500'000'000ULL);
    CHECK(!empty.fps.has_value());
    CHECK(empty.reason == UnavailableReason::no_samples);

    auto foreign = presents;
    foreign.back().identity = {77, 88};
    const auto mismatch = aggregate_presents(
        foreign, game, foreign.back().monotonic_ns, 500'000'000ULL);
    CHECK(!mismatch.fps.has_value());
    CHECK(mismatch.reason == UnavailableReason::identity_mismatch);

    const auto stale = aggregate_presents(
        presents, game, presents.back().monotonic_ns + 500'000'001ULL,
        500'000'000ULL);
    CHECK(!stale.fps.has_value());
    CHECK(stale.reason == UnavailableReason::stale);

    SnapshotStore store{2};
    TelemetrySnapshot first{game, 100, sixty};
    TelemetrySnapshot second{game, 200, degraded};
    TelemetrySnapshot third{game, 300, sixty};
    CHECK(store.publish(first));
    CHECK(store.publish(second));
    CHECK(store.publish(third));
    CHECK(store.size() == 2);
    const auto latest = store.read(game, 350, 100);
    CHECK(latest.has_value());
    CHECK(latest->monotonic_ns == 300);
    CHECK(!store.read({game.pid, game.process_start_id + 1}, 350, 100).has_value());
    CHECK(!store.read(game, 401, 100).has_value());
    CHECK(!store.publish({game, 299, sixty}));
    return EXIT_SUCCESS;
}
