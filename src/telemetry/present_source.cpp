#include "kf2/telemetry/present_source.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>

namespace kf2::telemetry {
namespace {
// Match the one-second live cadence used by common external overlays. The
// samples are already retained for the longer statistics below, so this does
// not add another capture, timer or telemetry pass.
constexpr std::uint64_t kLiveWindowNs = 1'000'000'000ULL;
constexpr std::uint64_t kSustainedWindowNs = 3'000'000'000ULL;
constexpr std::uint64_t kTailWindowNs = 5'000'000'000ULL;
#ifdef KF2_PRESENT_SOURCE_TESTING
std::atomic_bool fail_next_drain_publication{false};
std::atomic<detail::PresentDrainWaitHook> drain_wait_hook{nullptr};
#endif
}

#ifdef KF2_PRESENT_SOURCE_TESTING
void detail::fail_next_present_drain_publication() noexcept {
    fail_next_drain_publication.store(true, std::memory_order_release);
}
void detail::set_present_drain_wait_hook(PresentDrainWaitHook hook) noexcept {
    drain_wait_hook.store(hook, std::memory_order_release);
}
#endif

PresentSource::PresentSource(SampleIdentity identity, std::size_t capacity)
    : identity_{identity},
      capacity_{std::max<std::size_t>(2, capacity)},
      drain_worker_{[this](std::stop_token stop) { drain_worker(stop); }} {}

PresentSource::~PresentSource() {
    {
        // Publish stop under the wait mutex, so its notification cannot fall
        // between the worker's false predicate and the atomic wait/unlock.
        std::scoped_lock lock{mutex_};
        drain_worker_.request_stop();
    }
    drain_changed_.notify_all();
    if (drain_worker_.joinable()) drain_worker_.join();
}

Result<bool> PresentSource::start() {
    std::scoped_lock lock{mutex_};
    streams_.clear(); reported_loss_ = 0; loss_boundary_ns_ = 0;
    schema_failure_ = false;
    ++diagnostic_generation_; last_stream_.reset();
    invalidate_drain_locked();
    running_ = true;
    return Result<bool>::success(true);
}
Result<bool> PresentSource::stop() {
    std::scoped_lock lock{mutex_};
    running_ = false; streams_.clear(); reported_loss_ = 0; loss_boundary_ns_ = 0;
    ++diagnostic_generation_; last_stream_.reset();
    invalidate_drain_locked();
    return Result<bool>::success(true);
}
void PresentSource::bind(SampleIdentity identity) {
    std::scoped_lock lock{mutex_};
    identity_ = identity; streams_.clear(); reported_loss_ = 0; loss_boundary_ns_ = 0;
    ++diagnostic_generation_; last_stream_.reset();
    schema_failure_ = false;
    invalidate_drain_locked();
}
void PresentSource::reset_statistics() {
    std::scoped_lock lock{mutex_};
    streams_.clear();
    reported_loss_ = 0;
    loss_boundary_ns_ = 0;
    ++diagnostic_generation_; last_stream_.reset();
    invalidate_drain_locked();
}
bool PresentSource::ingest(const PresentEvent& event) {
    std::scoped_lock lock{mutex_};
    if (!running_ || event.identity != identity_) return false;
    if (event.schema_version != 1) {
        schema_failure_ = true;
        streams_.clear();
        invalidate_drain_locked();
        return false;
    }
    if (!event.completed || event.events_lost != 0) {
        constexpr auto maximum_loss = std::numeric_limits<std::uint64_t>::max();
        reported_loss_ += std::min(event.events_lost, maximum_loss - reported_loss_);
        if (!event.completed && reported_loss_ < maximum_loss) ++reported_loss_;
        loss_boundary_ns_ = std::max(loss_boundary_ns_, event.monotonic_ns);
        // A late or untimed loss must not certify already admitted data.
        // This bounded stream walk runs only on loss, never on clean presents.
        ++diagnostic_generation_;
        for (auto& [stream_id, stream] : streams_) {
            const auto& presents = stream.presents;
            if (!presents.empty()) loss_boundary_ns_ = std::max(
                loss_boundary_ns_, presents.back().monotonic_ns);
            stream.diagnostic_generation = diagnostic_generation_;
        }
        invalidate_drain_locked();
        if (!event.completed) return false;
    }
    constexpr std::size_t kMaximumStreams = 16;
    auto stream = streams_.find(event.stream_id);
    if (stream == streams_.end()) {
        if (streams_.size() >= kMaximumStreams) {
            auto oldest_inactive = streams_.end();
            for (auto candidate = streams_.begin();
                 candidate != streams_.end(); ++candidate) {
                if (last_stream_ && candidate->first == *last_stream_) continue;
                if (oldest_inactive == streams_.end() ||
                    candidate->second.presents.empty() ||
                    (!oldest_inactive->second.presents.empty() &&
                     candidate->second.presents.back().monotonic_ns <
                         oldest_inactive->second.presents.back().monotonic_ns)) {
                    oldest_inactive = candidate;
                }
            }
            if (oldest_inactive == streams_.end()) return false;
            streams_.erase(oldest_inactive);
        }
        stream = streams_.try_emplace(event.stream_id).first;
        // A swapchain lifetime owns its diagnostic epoch. Interleaved events
        // from another live chain do not interrupt this chain's windows.
        stream->second.diagnostic_generation = ++diagnostic_generation_;
        stream->second.boundary_ns = last_stream_ ? event.monotonic_ns : 0;
    }
    last_stream_ = event.stream_id;
    auto& presents = stream->second.presents;
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
    std::uint64_t source_generation = 0;
    std::uint64_t selected_stream_id = 0;
    {
        std::scoped_lock lock{mutex_};
        if (!running_ || schema_failure_) {
            FrameMetrics unavailable;
            unavailable.reason = UnavailableReason::source_failure;
            return unavailable;
        }
        identity = identity_;
        reported_loss = reported_loss_;
        source_generation = drain_generation_;
        const std::deque<PresentTimestamp>* selected = nullptr;
        bool selected_fresh = false;
        std::size_t selected_fast_count = 0;
        std::uint64_t selected_newest = 0;
        for (const auto& [stream_id, stream] : streams_) {
            const auto& presents = stream.presents;
            if (presents.empty()) continue;
            const auto newest = presents.back().monotonic_ns;
            if (newest < not_before_ns) continue;
            const bool fresh = now_ns >= newest &&
                now_ns - newest <= stale_after_ns;
            const auto cutoff = std::max(not_before_ns,
                newest > kLiveWindowNs ? newest - kLiveWindowNs : 0);
            const auto first = std::lower_bound(
                presents.begin(), presents.end(), cutoff,
                [](const PresentTimestamp& present,
                   std::uint64_t timestamp) {
                    return present.monotonic_ns < timestamp;
                });
            const auto fast_count = static_cast<std::size_t>(
                std::distance(first, presents.end()));
            if (!selected || (fresh && !selected_fresh) ||
                (fresh == selected_fresh &&
                 (fast_count > selected_fast_count ||
                  (fast_count == selected_fast_count &&
                   (newest > selected_newest ||
                    (newest == selected_newest &&
                     stream_id < selected_stream_id)))))) {
                selected = &presents;
                selected_fresh = fresh;
                selected_fast_count = fast_count;
                selected_newest = newest;
                selected_stream_id = stream_id;
            }
        }
        if (selected) {
            const auto newest = selected->back().monotonic_ns;
            const auto cutoff = std::max(not_before_ns,
                newest > PresentSource::longest_window_ns
                    ? newest - PresentSource::longest_window_ns : 0);
            // The shared quality covers every returned statistic, including
            // the longest tail. A fully post-loss window needs no reset.
            if (cutoff > loss_boundary_ns_) reported_loss = 0;
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
        window(kLiveWindowNs), identity, now_ns, stale_after_ns);
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
    result.source_generation = source_generation;
    result.stream_id = selected_stream_id;
    if (result.fps && result.loss_count > 0) result.quality = SampleQuality::degraded;
    return result;
}

void PresentSource::request_drain(std::uint64_t now_ns,
                                  std::uint64_t stale_after_ns,
                                  std::uint64_t not_before_ns) {
    std::scoped_lock lock{mutex_};
    auto request = DrainRequest{
        drain_generation_, now_ns, stale_after_ns, not_before_ns};
    if (drain_worker_failed_) {
        FrameMetrics unavailable;
        unavailable.reason = UnavailableReason::source_failure;
        if (not_before_ns == 0) {
            latest_default_drain_ = unavailable;
        } else {
            latest_bounded_drain_ = unavailable;
            latest_bounded_not_before_ns_ = not_before_ns;
        }
        drain_changed_.notify_all();
        return;
    }
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

void PresentSource::drain_worker(std::stop_token stop) noexcept {
    static_cast<void>(SetThreadPriority(
        GetCurrentThread(), THREAD_PRIORITY_NORMAL));
    try {
        while (!stop.stop_requested()) {
            DrainRequest request;
            {
                std::unique_lock lock{mutex_};
                drain_changed_.wait(lock, [&] {
                    const bool ready = pending_default_drain_.has_value() ||
                           pending_bounded_drain_.has_value() ||
                           stop.stop_requested();
#ifdef KF2_PRESENT_SOURCE_TESTING
                    if (!ready) {
                        if (auto hook = drain_wait_hook.load(std::memory_order_acquire))
                            hook(stop);
                    }
#endif
                    return ready;
                });
                if (pending_bounded_drain_) {
                    request = *pending_bounded_drain_;
                    pending_bounded_drain_.reset();
                } else if (pending_default_drain_) {
                    request = *pending_default_drain_;
                    pending_default_drain_.reset();
                } else {
                    return;
                }
                drain_active_ = true;
            }

            try {
                auto metrics = drain(request.now_ns, request.stale_after_ns,
                                     request.not_before_ns);
#ifdef KF2_PRESENT_SOURCE_TESTING
                if (fail_next_drain_publication.exchange(
                        false, std::memory_order_acq_rel)) {
                    throw std::bad_alloc{};
                }
#endif
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
            } catch (...) {
                try {
                    FrameMetrics unavailable;
                    unavailable.reason = UnavailableReason::source_failure;
                    std::scoped_lock lock{mutex_};
                    drain_active_ = false;
                    if (!stop.stop_requested() &&
                        request.generation == drain_generation_) {
                        if (request.not_before_ns == 0) {
                            latest_default_drain_ = unavailable;
                        } else {
                            latest_bounded_drain_ = unavailable;
                            latest_bounded_not_before_ns_ =
                                request.not_before_ns;
                        }
                    }
                } catch (...) {
                    // Preserve the worker exception boundary even if state
                    // publication itself cannot acquire its lock.
                }
            }
            drain_changed_.notify_all();
        }
    } catch (...) {
        try {
            std::scoped_lock lock{mutex_};
            drain_worker_failed_ = true;
            drain_active_ = false;
            pending_default_drain_.reset();
            pending_bounded_drain_.reset();
            FrameMetrics unavailable;
            unavailable.reason = UnavailableReason::source_failure;
            latest_default_drain_ = unavailable;
            latest_bounded_drain_ = unavailable;
            latest_bounded_not_before_ns_ = 0;
        } catch (...) {
            // No exception may cross the jthread entry point.
        }
        drain_changed_.notify_all();
    }
}

PresentSource::Window PresentSource::measure_window(
    std::uint64_t begin_ns, std::uint64_t end_ns) const {
    std::scoped_lock lock{mutex_};
    Window result;
    result.generation = diagnostic_generation_;
    if (!running_ || schema_failure_ || end_ns <= begin_ns ||
        (reported_loss_ != 0 && begin_ns <= loss_boundary_ns_))
        return result;
    const Stream* selected_stream = nullptr;
    std::deque<PresentTimestamp>::const_iterator selected_first, selected_end;
    for (const auto& [stream_id, stream] : streams_) {
        const auto& presents = stream.presents;
        const auto first = std::lower_bound(presents.begin(), presents.end(),
            begin_ns, [](const auto& present, std::uint64_t at) {
                return present.monotonic_ns < at;
            });
        const auto end = std::upper_bound(first, presents.end(), end_ns,
            [](std::uint64_t at, const auto& present) {
                return at < present.monotonic_ns;
            });
        const auto count = static_cast<std::size_t>(std::distance(first, end));
        if (count > result.count ||
            (count == result.count && stream_id < result.stream_id)) {
            selected_stream = &stream;
            selected_first = first;
            selected_end = end;
            result.count = count;
            result.stream_id = stream_id;
            result.generation = stream.diagnostic_generation;
        }
    }
    if (!selected_stream || result.count < 2 ||
        (selected_stream->boundary_ns >= begin_ns &&
         selected_stream->boundary_ns <= end_ns)) return result;
    // Copy only the selected interval, after binary-searching the existing
    // sorted deques; unrelated chains no longer allocate/copy full windows.
    const std::vector<PresentTimestamp> selected{selected_first, selected_end};
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
