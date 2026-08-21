#pragma once

#include <cstdint>
#include <optional>

#include "kf2/core/result.hpp"
#include "kf2/flex/flex_observation.hpp"
#include "kf2/game/game_log_session.hpp"
#include "kf2/game/game_session.hpp"
#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry_pipeline {

struct TelemetryFrameInput final {
    ::kf2::telemetry::SampleIdentity identity;
    std::uint64_t observed_at_ns{0};
    game::GameWindowState window;
    ::kf2::telemetry::FrameMetrics frames;
    std::optional<::kf2::telemetry::ProcessMetrics> process;
    std::optional<::kf2::telemetry::GpuMetrics> adapter_gpu;
    std::optional<double> driver_gpu_percent;
    std::optional<::kf2::telemetry::SystemMemoryMetrics> system_memory;
    std::optional<game::GameLogSession> gameplay;
    std::optional<flex::ObservationSnapshot> flex;
    std::optional<std::uint64_t> adapter_luid;
    std::optional<std::uint64_t> adapter_vram_budget_bytes;
};

struct TelemetryFrame final {
    ::kf2::telemetry::SampleIdentity identity;
    std::uint64_t observed_at_ns{0};
    game::GameWindowState window;
    ::kf2::telemetry::FrameMetrics frames;
    std::optional<::kf2::telemetry::ProcessMetrics> process;
    std::optional<::kf2::telemetry::GpuMetrics> adapter_gpu;
    std::optional<double> driver_gpu_percent;
    std::optional<::kf2::telemetry::SystemMemoryMetrics> system_memory;
    std::optional<game::GameLogSession> gameplay;
    std::optional<flex::ObservationSnapshot> flex;
    std::optional<std::uint64_t> adapter_luid;
    std::optional<std::uint64_t> adapter_vram_budget_bytes;
    optimizer::PerformanceEvidence evidence;
    bool active_gameplay{false};
    bool offline_gameplay{false};
};

[[nodiscard]] Result<TelemetryFrame> build_telemetry_frame(
    const TelemetryFrameInput& input);

}  // namespace kf2::telemetry_pipeline
