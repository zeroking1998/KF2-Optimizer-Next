#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include "kf2/core/result.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry {
struct PresentEvent {
    SampleIdentity identity;
    std::uint64_t monotonic_ns{0};
    std::uint16_t schema_version{0};
    bool completed{false};
    std::uint64_t events_lost{0};
    std::uint64_t stream_id{0};
};
class PresentSource final {
public:
    static constexpr std::uint64_t longest_window_ns =
        10'000'000'000ULL;

    struct Window {
        FrameMetrics metrics;
        std::uint64_t stream_id{0};
        // Lifetime token for this stream, including shared source invalidation.
        std::uint64_t generation{0};
        std::uint64_t span_ns{0};
        std::size_t count{0};
        bool complete{false};
    };
    // Read-only, single-stream statistics for a fixed diagnostic interval.
    [[nodiscard]] Window measure_window(std::uint64_t begin_ns,
                                        std::uint64_t end_ns) const;
    PresentSource(SampleIdentity identity, std::size_t capacity);
    ~PresentSource();
    [[nodiscard]] Result<bool> start();
    [[nodiscard]] Result<bool> stop();
    void bind(SampleIdentity identity);
    void reset_statistics();
    // Commit known loss without fallible stream/sample allocation.
    [[nodiscard]] bool record_loss(SampleIdentity identity,
                                   std::uint64_t timestamp_ns,
                                   std::uint64_t count,
                                   bool uncounted = false);
    // nullopt hides gameplay history; raw live timing remains measurable.
    void set_statistics_boundary(std::optional<std::uint64_t> not_before_ns);
    [[nodiscard]] bool ingest(const PresentEvent& event);
    [[nodiscard]] FrameMetrics drain(std::uint64_t now_ns,
                                     std::uint64_t stale_after_ns,
                                     std::uint64_t not_before_ns = 0) const;
    void request_drain(std::uint64_t now_ns,
                       std::uint64_t stale_after_ns,
                       std::uint64_t not_before_ns = 0);
    [[nodiscard]] std::optional<FrameMetrics> latest_drain(
        std::uint64_t not_before_ns = 0) const;
    [[nodiscard]] bool wait_for_drain(std::chrono::milliseconds timeout);
private:
    struct Stream {
        std::deque<PresentTimestamp> presents;
        std::uint64_t diagnostic_generation{0};
        std::uint64_t boundary_ns{0};
    };
    struct DrainRequest {
        std::uint64_t generation{0};
        std::uint64_t now_ns{0};
        std::uint64_t stale_after_ns{0};
        std::uint64_t not_before_ns{0};
    };

    void invalidate_drain_locked();
    void record_loss_locked(std::uint64_t timestamp_ns, std::uint64_t count,
                            bool incomplete);
    void drain_worker(std::stop_token stop) noexcept;

    SampleIdentity identity_;
    std::size_t capacity_;
    bool running_{false};
    bool schema_failure_{false};
    bool uncounted_loss_{false};
    std::uint64_t reported_loss_{0};
    std::uint64_t loss_boundary_ns_{0};
    std::uint64_t diagnostic_generation_{0};
    std::optional<std::uint64_t> last_stream_;
    std::unordered_map<std::uint64_t, Stream> streams_;
    mutable std::mutex mutex_;
    mutable std::condition_variable drain_changed_;
    std::uint64_t drain_generation_{0};
    std::optional<std::uint64_t> statistics_not_before_ns_{0};
    std::optional<DrainRequest> pending_default_drain_;
    std::optional<DrainRequest> pending_bounded_drain_;
    std::optional<FrameMetrics> latest_default_drain_;
    std::optional<FrameMetrics> latest_bounded_drain_;
    std::uint64_t latest_bounded_not_before_ns_{0};
    bool drain_active_{false};
    bool drain_worker_failed_{false};
    std::jthread drain_worker_;
};

#ifdef KF2_PRESENT_SOURCE_TESTING
namespace detail {
void fail_next_present_drain_publication() noexcept;
using PresentDrainWaitHook = void (*)(std::stop_token) noexcept;
void set_present_drain_wait_hook(PresentDrainWaitHook hook) noexcept;
void set_present_drain_publication_hook(PresentDrainWaitHook hook) noexcept;
}
#endif
}  // namespace kf2::telemetry
