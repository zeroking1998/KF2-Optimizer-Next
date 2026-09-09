#include "kf2/telemetry/present_source.hpp"
#include <Windows.h>
#include <algorithm>
#include <iterator>

namespace kf2::telemetry {
namespace {
constexpr std::uint64_t kFastWindowNs = 750'000'000ULL;
constexpr std::uint64_t kSustainedWindowNs = 3'000'000'000ULL;
constexpr std::uint64_t kTailWindowNs = 5'000'000'000ULL;
}

PresentSource::PresentSource(SampleIdentity identity, std::size_t capacity)
    : identity_{identity},
      capacity_{std::max<std::size_t>(2, capacity)},
      drain_worker_{[this](std::stop_token stop) { drain_worker(stop); }} {}

PresentSource::~PresentSource() {
    drain_worker_.request_stop();
    drain_changed_.notify_all();
    if (drain_worker_.joinable()) drain_worker_.join();
}

Result<bool> PresentSource::start() {
    std::scoped_lock lock{mutex_};
    streams_.clear(); reported_loss_ = 0; schema_failure_ = false;
    ++diagnostic_generation_; last_stream_.reset(); diagnostic_boundary_ns_ = 0;
    invalidate_drain_locked();
    running_ = true;
    return Result<bool>::success(true);
}
Result<bool> PresentSource::stop() {
    std::scoped_lock lock{mutex_};
    running_ = false; streams_.clear(); reported_loss_ = 0;
    ++diagnostic_generation_; last_stream_.reset(); diagnostic_boundary_ns_ = 0;
    invalidate_drain_locked();
    return Result<bool>::success(true);
}
void PresentSource::bind(SampleIdentity identity) {
    std::scoped_lock lock{mutex_};
    identity_ = identity; streams_.clear(); reported_loss_ = 0;
    ++diagnostic_generation_; last_stream_.reset(); diagnostic_boundary_ns_ = 0;
    schema_failure_ = false;
    invalidate_drain_locked();
}
void PresentSource::reset_statistics() {
    std::scoped_lock lock{mutex_};
    streams_.clear();
    reported_loss_ = 0;
    ++diagnostic_generation_; last_stream_.reset(); diagnostic_boundary_ns_ = 0;
    invalidate_drain_locked();
}
bool PresentSource::ingest(const PresentEvent& event) {
    std::scoped_lock lock{mutex_};
    if (!running_ || event.identity != identity_) return false;
    if (event.schema_version != 1) {
        schema_failure_ = true; streams_.clear(); return false;
    }
    if (!event.completed) { ++reported_loss_; return false; }
    if (last_stream_ && *last_stream_ != event.stream_id) {
        ++diagnostic_generation_;
        diagnostic_boundary_ns_ = std::max(diagnostic_boundary_ns_, event.monotonic_ns);
    }
    last_stream_ = event.stream_id;
    reported_loss_ += event.events_lost;
    constexpr std::size_t kMaximumStreams = 16;
    auto stream = streams_.find(event.stream_id);
    if (stream == streams_.end()) {
        if (streams_.size() >= kMaximumStreams) return false;
        stream = streams_.try_emplace(event.stream_id).first;
    }
    auto& presents = stream->second;
    const auto position = std::lower_bound(
        presents.begin(), presents.end(), event.monotonic_ns,
        [](const PresentTimestamp& present, std::uint64_t timestamp) {
            return present.monotonic_ns < timestamp;
        });
    if (position != presents.end() &&
        position->monotonic_ns == event.monotonic_ns) {
        return false;
    }
    presents.insert(position, {event.identity, event.monotonic_ns});
    while (presents.size() > capacity_) presents.pop_front();
    return true;
}
FrameMetrics PresentSource::drain(std::uint64_t now_ns,
                                  std::uint64_t stale_after_ns,
                                  std::uint64_t not_before_ns) const {
    std::vector<PresentTimestamp> long_term;
    SampleIdentity identity;
    std::uint64_t reported_loss = 0;
    {
        std::scoped_lock lock{mutex_};
        if (!running_ || schema_failure_) {
            FrameMetrics unavailable;
            unavailable.reason = UnavailableReason::source_failure;
            return unavailable;
        }
        identity = identity_;
        reported_loss = reported_loss_;
        const std::deque<PresentTimestamp>* selected = nullptr;
        std::size_t selected_fast_count = 0;
        std::uint64_t selected_newest = 0;
        for (const auto& [stream_id, presents] : streams_) {
            static_cast<void>(stream_id);
            if (presents.empty()) continue;
            const auto newest = presents.back().monotonic_ns;
            if (newest < not_before_ns) continue;
            const auto cutoff = std::max(not_before_ns,
                newest > kFastWindowNs ? newest - kFastWindowNs : 0);
            const auto first = std::lower_bound(
                presents.begin(), presents.end(), cutoff,
                [](const PresentTimestamp& present,
                   std::uint64_t timestamp) {
                    return present.monotonic_ns < timestamp;
                });
            const auto fast_count = static_cast<std::size_t>(
                std::distance(first, presents.end()));
            if (!selected || fast_count > selected_fast_count ||
                (fast_count == selected_fast_count &&
                 newest > selected_newest)) {
                selected = &presents;
                selected_fast_count = fast_count;
                selected_newest = newest;
            }
        }
        if (selected) {
            const auto newest = selected->back().monotonic_ns;
            const auto cutoff = std::max(not_before_ns,
                newest > PresentSource::longest_window_ns
                    ? newest - PresentSource::longest_window_ns : 0);
            const auto first = std::lower_bound(
                selected->begin(), selected->end(), cutoff,
                [](const PresentTimestamp& present,
                   std::uint64_t timestamp) {
                    return present.monotonic_ns < timestamp;
                });
            long_term.assign(first, selected->end());
        }
    }

    const auto window = [&](std::uint64_t duration) {
        const std::span<const PresentTimestamp> all{long_term};
        if (all.empty()) return all;
        const auto newest = all.back().monotonic_ns;
        const auto cutoff = std::max(not_before_ns,
            newest > duration ? newest - duration : 0);
        const auto first = std::lower_bound(
            all.begin(), all.end(), cutoff,
            [](const PresentTimestamp& present, std::uint64_t timestamp) {
                return present.monotonic_ns < timestamp;
            });
        return all.subspan(static_cast<std::size_t>(first - all.begin()));
    };
    auto result = aggregate_presents(
        window(kFastWindowNs), identity, now_ns, stale_after_ns);
    const auto sustained_metrics = aggregate_presents(
        window(kSustainedWindowNs), identity, now_ns, stale_after_ns);
    const auto tail_metrics = aggregate_presents(
        window(kTailWindowNs), identity, now_ns, stale_after_ns);
    const auto long_metrics = aggregate_presents(
        long_term, identity, now_ns, stale_after_ns);
    if (sustained_metrics.fps) {
        result.average_fps = sustained_metrics.fps;
    }
    if (sustained_metrics.one_percent_low_fps) {
        result.sustained_one_percent_low_fps =
            sustained_metrics.one_percent_low_fps;
    }
    if (tail_metrics.p95_ms) result.p95_ms = tail_metrics.p95_ms;
    if (tail_metrics.p99_ms) result.p99_ms = tail_metrics.p99_ms;
    if (tail_metrics.fps) result.stutter_count = tail_metrics.stutter_count;
    if (long_metrics.one_percent_low_fps) {
        result.one_percent_low_fps = long_metrics.one_percent_low_fps;
    }
    result.loss_count += reported_loss;
    if (result.fps && result.loss_count > 0) result.quality = SampleQuality::degraded;
    return result;
}

void PresentSource::request_drain(std::uint64_t now_ns,
                                  std::uint64_t stale_after_ns,
                                  std::uint64_t not_before_ns) {
    std::scoped_lock lock{mutex_};
    auto request = DrainRequest{
        drain_generation_, now_ns, stale_after_ns, not_before_ns};
    if (not_before_ns == 0) {
        pending_default_drain_ = request;
    } else {
        pending_bounded_drain_ = request;
    }
    drain_changed_.notify_one();
}

std::optional<FrameMetrics> PresentSource::latest_drain(
    std::uint64_t not_before_ns) const {
    std::scoped_lock lock{mutex_};
    if (not_before_ns == 0) return latest_default_drain_;
    if (latest_bounded_not_before_ns_ != not_before_ns) return std::nullopt;
    return latest_bounded_drain_;
}

bool PresentSource::wait_for_drain(std::chrono::milliseconds timeout) {
    std::unique_lock lock{mutex_};
    return drain_changed_.wait_for(lock, timeout, [this] {
        return !pending_default_drain_ && !pending_bounded_drain_ &&
               !drain_active_;
    });
}

void PresentSource::invalidate_drain_locked() {
    ++drain_generation_;
    pending_default_drain_.reset();
    pending_bounded_drain_.reset();
    latest_default_drain_.reset();
    latest_bounded_drain_.reset();
    latest_bounded_not_before_ns_ = 0;
    drain_changed_.notify_all();
}

void PresentSource::drain_worker(std::stop_token stop) {
    static_cast<void>(SetThreadPriority(
        GetCurrentThread(), THREAD_PRIORITY_NORMAL));
    while (!stop.stop_requested()) {
        DrainRequest request;
        {
            std::unique_lock lock{mutex_};
            drain_changed_.wait(lock, [&] {
                return pending_default_drain_.has_value() ||
                       pending_bounded_drain_.has_value() ||
                       stop.stop_requested();
            });
            if (pending_default_drain_) {
                request = *pending_default_drain_;
                pending_default_drain_.reset();
            } else if (pending_bounded_drain_) {
                request = *pending_bounded_drain_;
                pending_bounded_drain_.reset();
            } else {
                return;
            }
            drain_active_ = true;
        }

        auto metrics = drain(request.now_ns, request.stale_after_ns,
                             request.not_before_ns);
        {
            std::scoped_lock lock{mutex_};
            drain_active_ = false;
            if (!stop.stop_requested() &&
                request.generation == drain_generation_) {
                if (request.not_before_ns == 0) {
                    latest_default_drain_ = std::move(metrics);
                } else {
                    latest_bounded_drain_ = std::move(metrics);
                    latest_bounded_not_before_ns_ = request.not_before_ns;
                }
            }
        }
        drain_changed_.notify_all();
    }
}

PresentSource::Window PresentSource::measure_window(
    std::uint64_t begin_ns, std::uint64_t end_ns) const {
    std::scoped_lock lock{mutex_};
    Window result;
    result.generation = diagnostic_generation_;
    if (!running_ || schema_failure_ || reported_loss_ || end_ns <= begin_ns ||
        (diagnostic_boundary_ns_ >= begin_ns && diagnostic_boundary_ns_ <= end_ns))
        return result;
    std::vector<PresentTimestamp> selected;
    for (const auto& [stream_id, presents] : streams_) {
        std::vector<PresentTimestamp> window;
        std::copy_if(presents.begin(), presents.end(), std::back_inserter(window),
            [=](const auto& p) {
                return p.monotonic_ns >= begin_ns && p.monotonic_ns <= end_ns;
            });
        if (window.size() > selected.size() ||
            (window.size() == selected.size() && stream_id < result.stream_id)) {
            selected = std::move(window);
            result.stream_id = stream_id;
        }
    }
    result.count = selected.size();
    if (result.count < 2) return result;
    result.span_ns = selected.back().monotonic_ns - selected.front().monotonic_ns;
    result.metrics = aggregate_presents(selected, identity_, end_ns, 100'000'000ULL);
    // Allow boundary quantization by one frame, but not a young or stale window.
    result.complete = selected.front().monotonic_ns - begin_ns <= 100'000'000ULL &&
        end_ns - selected.back().monotonic_ns <= 100'000'000ULL &&
        result.span_ns >= (end_ns - begin_ns) * 95 / 100 &&
        result.metrics.quality == SampleQuality::good;
    return result;
}
}  // namespace kf2::telemetry
