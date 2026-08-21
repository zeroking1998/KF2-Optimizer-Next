#pragma once

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "features/telemetry/telemetry_frame.hpp"
#include "kf2/overlay/overlay_policy.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct StatusProjection final {
    std::wstring telemetry;
    std::wstring performance_analysis;
    std::optional<double> live_fps;
    std::optional<double> live_frame_time_ms;
    std::optional<double> live_cpu_percent;
    std::optional<double> live_gpu_percent;
    std::optional<int> live_active_corpses;
    std::optional<int> live_sleeping_corpses;
};

struct TelemetryPresentation final {
    StatusProjection status;
    std::optional<overlay::OverlayPresentation> overlay;
};

[[nodiscard]] inline std::wstring format_gib(std::uint64_t bytes) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(1)
         << static_cast<long double>(bytes) /
                (1024.0L * 1024.0L * 1024.0L)
         << L" GiB";
    return text.str();
}

[[nodiscard]] inline StatusProjection build_status_projection(
    const TelemetryFrame& frame, std::wstring_view telemetry_failure,
    std::wstring_view optimizer_reason, std::wstring_view adaptive_profile,
    std::wstring_view adaptive_reason) {
    StatusProjection result;
    result.telemetry = telemetry_failure.empty()
        ? L"Waiting for KF2 frame data" : std::wstring{telemetry_failure};
    if (frame.frames.fps && frame.frames.frame_time_ms) {
        std::wostringstream text;
        text << std::fixed << std::setprecision(1) << *frame.frames.fps
             << L" FPS, " << *frame.frames.frame_time_ms << L" ms";
        if (frame.process) {
            if (frame.process->cpu_percent) {
                text << L", CPU " << *frame.process->cpu_percent << L"%";
            }
            if (frame.process->critical_core_percent) {
                text << L" (critical thread "
                     << *frame.process->critical_core_percent << L"%)";
            }
            if (frame.process->effective_core_usage) {
                text << L", " << std::setprecision(2)
                     << *frame.process->effective_core_usage << L" cores";
            }
            if (frame.process->active_cpu_threads) {
                text << L" / " << *frame.process->active_cpu_threads
                     << L" active threads";
            }
            if (frame.process->affinity_physical_cores ||
                frame.process->affinity_logical_processors) {
                text << L", affinity ";
                if (frame.process->affinity_physical_cores) {
                    text << *frame.process->affinity_physical_cores << L"C/";
                }
                text << frame.process->affinity_logical_processors.value_or(0)
                     << L"T";
            }
            text << L", RAM " << format_gib(frame.process->working_set_bytes);
        }
        if (frame.adapter_gpu) {
            text << L", VRAM "
                 << format_gib(frame.adapter_gpu->dedicated_bytes);
        }
        if (frame.evidence.gpu_percent) {
            text << L", GPU total " << *frame.evidence.gpu_percent << L"%";
        }
        if (frame.system_memory) {
            text << L", system RAM " << std::setprecision(0)
                 << frame.system_memory->used_percent << L"%"
                 << std::setprecision(1);
        }
        if (frame.flex) {
            text << L", FleX " << frame.flex->last_substeps << L" steps";
            if (!frame.flex->pass_through_healthy) text << L" (relay error)";
        }
        result.telemetry = text.str();
    }

    result.performance_analysis =
        L"Performance analysis unavailable | Adaptive: " +
        std::wstring{adaptive_profile} + L" (" +
        std::wstring{adaptive_reason} + L")";
    if (frame.frames.fps) {
        const wchar_t* quality =
            frame.frames.quality == ::kf2::telemetry::SampleQuality::good
                ? L"good"
                : frame.frames.quality ==
                          ::kf2::telemetry::SampleQuality::degraded
                      ? L"degraded" : L"unavailable";
        std::wostringstream details;
        details << std::fixed << std::setprecision(1)
                << L"Measurement " << quality;
        if (frame.frames.p95_ms) {
            details << L" | p95 " << *frame.frames.p95_ms << L" ms";
        }
        if (frame.frames.p99_ms) {
            details << L" | p99 " << *frame.frames.p99_ms << L" ms";
        }
        details << L" | stutters " << frame.frames.stutter_count
                << L" | lost events " << frame.frames.loss_count
                << L" | analysis: " << optimizer_reason
                << L" | Adaptive: " << adaptive_profile << L" ("
                << adaptive_reason << L")";
        result.performance_analysis = details.str();
    }

    result.live_fps = frame.frames.fps;
    result.live_frame_time_ms = frame.frames.frame_time_ms;
    result.live_cpu_percent = frame.evidence.cpu_percent;
    result.live_gpu_percent = frame.evidence.gpu_percent;
    if (frame.gameplay) {
        result.live_active_corpses =
            frame.gameplay->telemetry_corpse_awake;
        result.live_sleeping_corpses =
            frame.gameplay->telemetry_corpse_sleeping;
    }
    return result;
}

[[nodiscard]] TelemetryPresentation derive_telemetry_presentation(
    const app::UiRuntime& runtime, const TelemetryFrame& frame);
void publish_telemetry_presentation(
    app::UiRuntime& runtime, TelemetryPresentation presentation);

}  // namespace kf2::telemetry_pipeline
