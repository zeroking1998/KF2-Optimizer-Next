#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
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
    [[nodiscard]] Result<bool> start();
    [[nodiscard]] Result<bool> stop();
    void bind(SampleIdentity identity);
    void reset_statistics();
    [[nodiscard]] bool ingest(const PresentEvent& event);
    [[nodiscard]] FrameMetrics drain(std::uint64_t now_ns,
                                     std::uint64_t stale_after_ns,
                                     std::uint64_t not_before_ns = 0) const;
private:
    SampleIdentity identity_;
    std::size_t capacity_;
    bool running_{false};
    bool schema_failure_{false};
    std::uint64_t reported_loss_{0};
    std::uint64_t diagnostic_generation_{0}, diagnostic_boundary_ns_{0};
    std::optional<std::uint64_t> last_stream_;
    std::unordered_map<std::uint64_t, std::deque<PresentTimestamp>> streams_;
    mutable std::mutex mutex_;
};
}  // namespace kf2::telemetry
