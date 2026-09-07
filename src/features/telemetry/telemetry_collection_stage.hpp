#pragma once

#include <optional>
#include <utility>

#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

enum class ResourceSampleGroup {
    process_and_memory,
    gpu,
};

inline constexpr std::uint64_t kResourceSampleFreshnessNs =
    1'000'000'000ULL;

[[nodiscard]] constexpr bool resource_sample_is_fresh(
    std::uint64_t sampled_at_ns, std::uint64_t now_ns) noexcept {
    return sampled_at_ns != 0 && now_ns >= sampled_at_ns &&
        now_ns - sampled_at_ns <= kResourceSampleFreshnessNs;
}

// Resource counters are intentionally split across adjacent telemetry frames.
// PresentMon remains on the normal telemetry cadence, while the more expensive
// process/memory and GPU counter groups never burst on the same UI tick.
[[nodiscard]] constexpr ResourceSampleGroup resource_sample_group(
    std::uint64_t sequence) noexcept {
    return sequence % 2 == 0
        ? ResourceSampleGroup::process_and_memory
        : ResourceSampleGroup::gpu;
}

enum class PresentDrainDisposition {
    frames_ready,
    reconnecting,
    source_invalid,
};

class PresentDrainResult final {
public:
    [[nodiscard]] static PresentDrainResult with_frames(
        ::kf2::telemetry::FrameMetrics frames) {
        return PresentDrainResult{PresentDrainDisposition::frames_ready,
                                  std::move(frames), std::nullopt};
    }

    [[nodiscard]] static PresentDrainResult reconnecting() {
        return PresentDrainResult{PresentDrainDisposition::reconnecting,
                                  std::nullopt, std::nullopt};
    }

    [[nodiscard]] static PresentDrainResult invalid(Error error) {
        return PresentDrainResult{PresentDrainDisposition::source_invalid,
                                  std::nullopt, std::move(error)};
    }

    [[nodiscard]] PresentDrainDisposition disposition() const noexcept {
        return disposition_;
    }
    [[nodiscard]] const std::optional<::kf2::telemetry::FrameMetrics>& frames()
        const noexcept {
        return frames_;
    }
    [[nodiscard]] const std::optional<Error>& error() const noexcept {
        return error_;
    }

private:
    PresentDrainResult(
        PresentDrainDisposition disposition,
        std::optional<::kf2::telemetry::FrameMetrics> frames,
        std::optional<Error> error)
        : disposition_{disposition}, frames_{std::move(frames)},
          error_{std::move(error)} {}

    PresentDrainDisposition disposition_;
    std::optional<::kf2::telemetry::FrameMetrics> frames_;
    std::optional<Error> error_;
};

[[nodiscard]] PresentDrainResult drain_present_stage(
    app::UiRuntime& runtime, std::uint64_t now_ns);

[[nodiscard]] Result<TelemetryFrame> capture_telemetry_frame(
    app::UiRuntime& runtime, const game::GameWindowState& window,
    std::uint64_t now_ns, ::kf2::telemetry::FrameMetrics frames);

}  // namespace kf2::telemetry_pipeline
