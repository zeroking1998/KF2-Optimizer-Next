#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace kf2::telemetry {

struct SampleIdentity {
    std::uint32_t pid{0};
    std::uint64_t process_start_id{0};
    friend bool operator==(const SampleIdentity&, const SampleIdentity&) = default;
};

enum class SampleQuality { unavailable, degraded, good };
enum class UnavailableReason {
    none, no_samples, stale, identity_mismatch, discontinuity, source_failure
};

struct PresentTimestamp {
    SampleIdentity identity;
    std::uint64_t monotonic_ns{0};
};

struct FrameMetrics {
    std::optional<double> fps;
    std::optional<double> average_fps;
    std::optional<double> sustained_one_percent_low_fps;
    std::optional<double> frame_time_ms;
    std::optional<double> p95_ms;
    std::optional<double> p99_ms;
    std::optional<double> one_percent_low_fps;
    std::size_t stutter_count{0};
    std::uint64_t age_ns{0};
    std::uint64_t loss_count{0};
    SampleQuality quality{SampleQuality::unavailable};
    UnavailableReason reason{UnavailableReason::no_samples};
};

struct TelemetrySnapshot {
    SampleIdentity identity;
    std::uint64_t monotonic_ns{0};
    FrameMetrics frames;
};

[[nodiscard]] FrameMetrics aggregate_presents(
    const std::vector<PresentTimestamp>& presents,
    const SampleIdentity& expected_identity,
    std::uint64_t now_ns,
    std::uint64_t stale_after_ns);

class SnapshotStore final {
public:
    explicit SnapshotStore(std::size_t capacity);
    [[nodiscard]] bool publish(TelemetrySnapshot snapshot);
    [[nodiscard]] std::optional<TelemetrySnapshot> read(
        const SampleIdentity& identity, std::uint64_t now_ns,
        std::uint64_t stale_after_ns) const;
    [[nodiscard]] std::size_t size() const;

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<TelemetrySnapshot> snapshots_;
};

}  // namespace kf2::telemetry
