#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include "kf2/core/result.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry {
struct PresentEvent {
    SampleIdentity identity;
    std::uint64_t monotonic_ns{0};
    std::uint16_t schema_version{0};
    bool completed{false};
    std::uint64_t events_lost{0};
};
class PresentSource final {
public:
    PresentSource(SampleIdentity identity, std::size_t capacity);
    [[nodiscard]] Result<bool> start();
    [[nodiscard]] Result<bool> stop();
    void bind(SampleIdentity identity);
    void reset_statistics();
    [[nodiscard]] bool ingest(const PresentEvent& event);
    [[nodiscard]] FrameMetrics drain(std::uint64_t now_ns,
                                     std::uint64_t stale_after_ns) const;
private:
    SampleIdentity identity_;
    std::size_t capacity_;
    bool running_{false};
    bool schema_failure_{false};
    std::uint64_t reported_loss_{0};
    std::deque<PresentTimestamp> presents_;
    mutable std::mutex mutex_;
};
}  // namespace kf2::telemetry
