#include "features/telemetry/telemetry_collection_stage.hpp"

#include "app/application_runtime.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"

namespace kf2::telemetry_pipeline {
namespace {

std::optional<flex::ObservationSnapshot> current_flex_snapshot(
    const app::UiRuntime& runtime) {
    if (!runtime.last_flex_observation ||
        !runtime.last_flex_observation->fresh ||
        runtime.last_flex_observation->last_update_tick == 0) {
        return std::nullopt;
    }
    const auto now_ms = GetTickCount64();
    if (now_ms < runtime.last_flex_observation->last_update_tick ||
        now_ms - runtime.last_flex_observation->last_update_tick > 3000) {
        return std::nullopt;
    }
    return runtime.last_flex_observation;
}

}  // namespace

PresentDrainResult drain_present_stage(app::UiRuntime& runtime,
                                       std::uint64_t now_ns) {
    if (!runtime.game_process || !runtime.present_source) {
        return PresentDrainResult::invalid(
            {ErrorCode::stale_data,
             L"Telemetry sources are not bound to a current KF2 process", 0});
    }

    auto frames = runtime.present_source->drain(now_ns, 2'000'000'000ULL);
    // A stale Launch.log can make the startup gate look ready before KF2's
    // new DX11 swap chain begins presenting. An embedded ETW session that
    // stays completely silent is restarted a bounded number of times; a
    // healthy or merely stale stream is never churned.
    if (should_reconnect_silent_present({
            .scene_ready = runtime.overlay_scene_ready,
            .session_bound = runtime.present_session != nullptr,
            .fps = frames.fps,
            .reason = frames.reason,
            .session_started_ns = runtime.present_session_started_ns,
            .now_ns = now_ns,
            .restart_count = runtime.present_session_restart_count})) {
        runtime.present_session.reset();
        static_cast<void>(runtime.present_source->stop());
        static_cast<void>(runtime.present_source->start());
        const ::kf2::telemetry::SampleIdentity identity{
            runtime.game_process->pid,
            runtime.game_process->process_start_id};
        auto restarted = platform::windows::PresentMonSession::start(
            identity, *runtime.present_source);
        ++runtime.present_session_restart_count;
        runtime.present_session_started_ns = now_ns;
        if (restarted.has_value()) {
            runtime.present_session = std::move(restarted.value());
            runtime.telemetry_failure =
                L"Reconnecting KF2 frame telemetry";
            runtime.events->append(
                {0, diagnostics::Severity::info, "PRESENTMON_RECONNECTED",
                 L"Silent startup telemetry was reconnected after KF2 reached the main menu",
                 L"telemetry"});
        } else {
            runtime.telemetry_failure = L"PresentMon reconnect failed: " +
                restarted.error().message;
            runtime.events->append(
                {0, diagnostics::Severity::warning,
                 "PRESENTMON_RECONNECT_FAILED", restarted.error().message,
                 L"telemetry"});
        }
        return PresentDrainResult::reconnecting();
    }
    return PresentDrainResult::with_frames(std::move(frames));
}

Result<TelemetryFrame> capture_telemetry_frame(
    app::UiRuntime& runtime, const game::GameWindowState& window,
    std::uint64_t now_ns, ::kf2::telemetry::FrameMetrics frames) {
    TelemetryFrameInput input;
    input.identity = {runtime.game_process->pid,
                      runtime.game_process->process_start_id};
    input.observed_at_ns = now_ns;
    input.window = window;
    input.frames = std::move(frames);
    input.gameplay = runtime.game_log_session_parser.current();
    input.flex = current_flex_snapshot(runtime);
    input.adapter_luid = runtime.adaptive_adapter_luid;
    input.adapter_vram_budget_bytes = runtime.adapter_vram_budget;

    if (input.frames.fps && input.frames.frame_time_ms) {
        if (runtime.process_metrics) {
            auto process = runtime.process_metrics->sample();
            if (process.has_value()) {
                input.process = std::move(process.value());
            }
        }
        if (runtime.nvidia_gpu_metrics) {
            const auto driver_gpu = runtime.nvidia_gpu_metrics->sample();
            if (driver_gpu.has_value()) {
                input.driver_gpu_percent = driver_gpu.value();
            }
        }
        if (runtime.gpu_metrics) {
            auto gpu = runtime.gpu_metrics->sample();
            if (gpu.has_value()) {
                bool sample_matches_bound_adapter = true;
                if (gpu.value().process_adapter_luid) {
                    sample_matches_bound_adapter = !input.adapter_luid ||
                        *input.adapter_luid ==
                            *gpu.value().process_adapter_luid;
                    runtime.bind_process_gpu_adapter(
                        *gpu.value().process_adapter_luid);
                    input.adapter_luid = runtime.adaptive_adapter_luid;
                    input.adapter_vram_budget_bytes =
                        runtime.adapter_vram_budget;
                }
                // A sample collected by the old sampler must never seed the
                // newly selected physical adapter's continuity history.
                if (sample_matches_bound_adapter) {
                    input.adapter_gpu = std::move(gpu.value());
                } else {
                    input.driver_gpu_percent.reset();
                }
            }
        }
        const auto raw_process_gpu = input.adapter_gpu
            ? input.adapter_gpu->gpu_percent : std::nullopt;
        const auto raw_adapter_gpu =
            ::kf2::telemetry::choose_total_gpu_percent(
                input.driver_gpu_percent,
                input.adapter_gpu
                    ? input.adapter_gpu->adapter_gpu_percent
                    : std::nullopt);
        input.gpu_utilization = runtime.gpu_utilization_filter.update({
            now_ns, input.adapter_luid.value_or(0), raw_process_gpu,
            raw_adapter_gpu});
        const auto memory = ::kf2::telemetry::query_system_memory_metrics();
        if (memory.has_value()) input.system_memory = memory.value();
    }
    return build_telemetry_frame(input);
}

}  // namespace kf2::telemetry_pipeline
