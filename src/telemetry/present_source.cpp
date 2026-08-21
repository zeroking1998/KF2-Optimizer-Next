#include "kf2/telemetry/present_source.hpp"
#include <algorithm>
#include <iterator>

namespace kf2::telemetry {
namespace {
constexpr std::uint64_t kFastWindowNs = 750'000'000ULL;
constexpr std::uint64_t kSustainedWindowNs = 3'000'000'000ULL;
constexpr std::uint64_t kTailWindowNs = 5'000'000'000ULL;
constexpr std::uint64_t kLongWindowNs = 10'000'000'000ULL;
}

PresentSource::PresentSource(SampleIdentity identity, std::size_t capacity)
    : identity_{identity}, capacity_{std::max<std::size_t>(2, capacity)} {}

Result<bool> PresentSource::start() {
    std::scoped_lock lock{mutex_};
    presents_.clear(); reported_loss_ = 0; schema_failure_ = false;
    running_ = true;
    return Result<bool>::success(true);
}
Result<bool> PresentSource::stop() {
    std::scoped_lock lock{mutex_};
    running_ = false; presents_.clear(); reported_loss_ = 0;
    return Result<bool>::success(true);
}
void PresentSource::bind(SampleIdentity identity) {
    std::scoped_lock lock{mutex_};
    identity_ = identity; presents_.clear(); reported_loss_ = 0;
    schema_failure_ = false;
}
void PresentSource::reset_statistics() {
    std::scoped_lock lock{mutex_};
    presents_.clear();
    reported_loss_ = 0;
}
bool PresentSource::ingest(const PresentEvent& event) {
    std::scoped_lock lock{mutex_};
    if (!running_ || event.identity != identity_) return false;
    if (event.schema_version != 1) {
        schema_failure_ = true; presents_.clear(); return false;
    }
    if (!event.completed) { ++reported_loss_; return false; }
    reported_loss_ += event.events_lost;
    presents_.push_back({event.identity, event.monotonic_ns});
    while (presents_.size() > capacity_) presents_.pop_front();
    return true;
}
FrameMetrics PresentSource::drain(std::uint64_t now_ns,
                                  std::uint64_t stale_after_ns) const {
    std::scoped_lock lock{mutex_};
    if (!running_ || schema_failure_) {
        FrameMetrics unavailable;
        unavailable.reason = UnavailableReason::source_failure;
        return unavailable;
    }
    std::vector<PresentTimestamp> fast;
    std::vector<PresentTimestamp> sustained;
    std::vector<PresentTimestamp> tail;
    std::vector<PresentTimestamp> long_term;
    if (!presents_.empty()) {
        const auto newest = presents_.back().monotonic_ns;
        const auto copy_window = [&](std::uint64_t duration,
                                     std::vector<PresentTimestamp>& output) {
            const auto cutoff = newest > duration ? newest - duration : 0;
            output.reserve(presents_.size());
            std::copy_if(presents_.begin(), presents_.end(),
                         std::back_inserter(output),
                         [cutoff](const PresentTimestamp& present) {
                             return present.monotonic_ns >= cutoff;
                         });
        };
        copy_window(kFastWindowNs, fast);
        copy_window(kSustainedWindowNs, sustained);
        copy_window(kTailWindowNs, tail);
        copy_window(kLongWindowNs, long_term);
    }
    auto result = aggregate_presents(fast, identity_, now_ns, stale_after_ns);
    const auto sustained_metrics = aggregate_presents(
        sustained, identity_, now_ns, stale_after_ns);
    const auto tail_metrics = aggregate_presents(
        tail, identity_, now_ns, stale_after_ns);
    const auto long_metrics = aggregate_presents(
        long_term, identity_, now_ns, stale_after_ns);
    if (sustained_metrics.fps) {
        result.average_fps = sustained_metrics.fps;
    }
    if (tail_metrics.p95_ms) result.p95_ms = tail_metrics.p95_ms;
    if (tail_metrics.p99_ms) result.p99_ms = tail_metrics.p99_ms;
    if (tail_metrics.fps) result.stutter_count = tail_metrics.stutter_count;
    if (long_metrics.one_percent_low_fps) {
        result.one_percent_low_fps = long_metrics.one_percent_low_fps;
    }
    result.loss_count += reported_loss_;
    if (result.fps && result.loss_count > 0) result.quality = SampleQuality::degraded;
    return result;
}
}  // namespace kf2::telemetry
