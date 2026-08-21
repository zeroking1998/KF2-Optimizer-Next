#pragma once

#include <optional>
#include <utility>

#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

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
