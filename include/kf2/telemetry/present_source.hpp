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
    struct DrainRequest {
        std::uint64_t generation{0};
        std::uint64_t now_ns{0};
        std::uint64_t stale_after_ns{0};
        std::uint64_t not_before_ns{0};
    };

    void invalidate_drain_locked();
    void drain_worker(std::stop_token stop);

    SampleIdentity identity_;
    std::size_t capacity_;
    bool running_{false};
    bool schema_failure_{false};
    std::uint64_t reported_loss_{0};
    std::uint64_t diagnostic_generation_{0}, diagnostic_boundary_ns_{0};
    std::optional<std::uint64_t> last_stream_;
    std::unordered_map<std::uint64_t, std::deque<PresentTimestamp>> streams_;
    mutable std::mutex mutex_;
    mutable std::condition_variable drain_changed_;
    std::uint64_t drain_generation_{0};
    std::optional<DrainRequest> pending_default_drain_;
    std::optional<DrainRequest> pending_bounded_drain_;
    std::optional<FrameMetrics> latest_default_drain_;
    std::optional<FrameMetrics> latest_bounded_drain_;
    std::uint64_t latest_bounded_not_before_ns_{0};
    bool drain_active_{false};
    std::jthread drain_worker_;
};
}  // namespace kf2::telemetry
