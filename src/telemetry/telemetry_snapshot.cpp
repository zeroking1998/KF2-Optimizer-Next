#include "kf2/telemetry/telemetry_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kf2::telemetry {
namespace {

FrameMetrics unavailable(UnavailableReason reason, std::uint64_t age = 0) {
    FrameMetrics result;
    result.reason = reason;
    result.age_ns = age;
    return result;
}

double percentile(const std::vector<double>& sorted, double fraction) {
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(sorted.size()))) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

}  // namespace

FrameMetrics aggregate_presents(
    const std::vector<PresentTimestamp>& presents,
    const SampleIdentity& expected_identity,
    std::uint64_t now_ns,
    std::uint64_t stale_after_ns) {
    if (presents.size() < 2) return unavailable(UnavailableReason::no_samples);
    for (const auto& present : presents) {
        if (present.identity != expected_identity) {
            return unavailable(UnavailableReason::identity_mismatch);
        }
    }
    const auto newest = presents.back().monotonic_ns;
    const auto age = now_ns >= newest ? now_ns - newest : 0;
    if (now_ns < newest || age > stale_after_ns) {
        return unavailable(UnavailableReason::stale, age);
    }

    std::vector<double> intervals;
    intervals.reserve(presents.size() - 1);
    std::uint64_t loss = 0;
    std::uint64_t previous = presents.front().monotonic_ns;
    for (std::size_t index = 1; index < presents.size(); ++index) {
        const auto current = presents[index].monotonic_ns;
        if (current <= previous) {
            ++loss;
            continue;
        }
        intervals.push_back(static_cast<double>(current - previous) / 1'000'000.0);
        previous = current;
    }
    if (intervals.empty()) {
        auto result = unavailable(UnavailableReason::discontinuity, age);
        result.loss_count = loss;
        return result;
    }
    const double total = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    const double average = total / static_cast<double>(intervals.size());
    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t slow_count = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(sorted.size() * 0.01)));
    const double slow_average = std::accumulate(
        sorted.end() - static_cast<std::ptrdiff_t>(slow_count), sorted.end(), 0.0) /
        static_cast<double>(slow_count);
    const double median = percentile(sorted, 0.5);
    const double stutter_limit = std::max(50.0, median * 2.0);

    FrameMetrics result;
    result.fps = 1000.0 / average;
    result.average_fps = result.fps;
    result.frame_time_ms = average;
    result.p95_ms = percentile(sorted, 0.95);
    result.p99_ms = percentile(sorted, 0.99);
    result.one_percent_low_fps = 1000.0 / slow_average;
    result.stutter_count = static_cast<std::size_t>(std::count_if(
        intervals.begin(), intervals.end(),
        [stutter_limit](double interval) { return interval > stutter_limit; }));
    result.age_ns = age;
    result.loss_count = loss;
    result.quality = loss == 0 ? SampleQuality::good : SampleQuality::degraded;
    result.reason = UnavailableReason::none;
    return result;
}

SnapshotStore::SnapshotStore(std::size_t capacity)
    : capacity_{std::max<std::size_t>(1, capacity)} {}

bool SnapshotStore::publish(TelemetrySnapshot snapshot) {
    std::scoped_lock lock{mutex_};
    if (!snapshots_.empty() &&
        snapshot.monotonic_ns <= snapshots_.back().monotonic_ns) return false;
    snapshots_.push_back(std::move(snapshot));
    while (snapshots_.size() > capacity_) snapshots_.pop_front();
    return true;
}

std::optional<TelemetrySnapshot> SnapshotStore::read(
    const SampleIdentity& identity, std::uint64_t now_ns,
    std::uint64_t stale_after_ns) const {
    std::scoped_lock lock{mutex_};
    if (snapshots_.empty()) return std::nullopt;
    const auto& latest = snapshots_.back();
    if (latest.identity != identity || now_ns < latest.monotonic_ns ||
        now_ns - latest.monotonic_ns > stale_after_ns) return std::nullopt;
    return latest;
}

std::size_t SnapshotStore::size() const {
    std::scoped_lock lock{mutex_};
    return snapshots_.size();
}

}  // namespace kf2::telemetry
