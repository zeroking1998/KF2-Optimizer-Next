#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::telemetry_pipeline {

Result<TelemetryFrame> build_telemetry_frame(
    const TelemetryFrameInput& input) {
    if (input.window.process.pid != input.identity.pid ||
        input.window.process.process_start_id !=
            input.identity.process_start_id) {
        return Result<TelemetryFrame>::failure(
            {ErrorCode::stale_data,
             L"Telemetry window identity does not match the bound process",
             0});
    }

    TelemetryFrame frame;
    frame.identity = input.identity;
    frame.observed_at_ns = input.observed_at_ns;
    frame.window = input.window;
    frame.frames = input.frames;
    frame.process = input.process;
    frame.adapter_gpu = input.adapter_gpu;
    frame.driver_gpu_percent = input.driver_gpu_percent;
    frame.gpu_utilization = input.gpu_utilization;
    frame.system_memory = input.system_memory;
    frame.gameplay = input.gameplay;
    frame.flex = input.flex;
    frame.adapter_luid = input.adapter_luid;
    frame.adapter_vram_budget_bytes = input.adapter_vram_budget_bytes;

    frame.active_gameplay = frame.gameplay &&
        game::game_log_is_active_gameplay(*frame.gameplay);
    frame.offline_gameplay = frame.gameplay &&
        game::game_log_is_offline_gameplay(*frame.gameplay);

    auto& evidence = frame.evidence;
    evidence.fresh = frame.active_gameplay && frame.frames.fps.has_value() &&
        frame.frames.quality != ::kf2::telemetry::SampleQuality::unavailable;
    evidence.fps = frame.frames.fps;
    evidence.p95_frame_time_ms = frame.frames.p95_ms;
    if (frame.process) {
        evidence.cpu_percent = frame.process->cpu_percent;
        evidence.system_cpu_percent =
            frame.process->system_cpu_percent;
        evidence.critical_core_percent =
            frame.process->critical_core_percent;
        evidence.effective_core_usage =
            frame.process->effective_core_usage;
        evidence.dominant_thread_share_percent =
            frame.process->dominant_thread_share_percent;
        evidence.active_cpu_threads = frame.process->active_cpu_threads;
        evidence.affinity_logical_processors =
            frame.process->affinity_logical_processors;
        evidence.affinity_physical_cores =
            frame.process->affinity_physical_cores;
        evidence.system_logical_processors =
            frame.process->system_logical_processors;
        evidence.process_private_bytes = frame.process->private_bytes;
    }
    if (frame.gpu_utilization && frame.gpu_utilization->decision_ready) {
        evidence.process_gpu_percent =
            frame.gpu_utilization->process_percent;
        evidence.gpu_percent = frame.gpu_utilization->adapter_percent;
    }
    if (frame.adapter_gpu) {
        evidence.dedicated_vram_bytes =
            frame.adapter_gpu->dedicated_bytes;
        evidence.dedicated_vram_budget_bytes =
            frame.adapter_vram_budget_bytes;
        evidence.adapter_vram_used_bytes =
            frame.adapter_gpu->adapter_local_usage_bytes;
        evidence.adapter_vram_budget_bytes =
            frame.adapter_gpu->adapter_local_budget_bytes;
    }
    if (frame.system_memory) {
        evidence.system_ram_budget_bytes =
            frame.system_memory->total_physical_bytes;
        evidence.system_ram_used_bytes =
            frame.system_memory->total_physical_bytes -
            frame.system_memory->available_physical_bytes;
        evidence.system_commit_budget_bytes =
            frame.system_memory->commit_limit_bytes;
        if (frame.system_memory->available_commit_bytes <=
            frame.system_memory->commit_limit_bytes) {
            evidence.system_commit_used_bytes =
                frame.system_memory->commit_limit_bytes -
                frame.system_memory->available_commit_bytes;
        }
    }
    return Result<TelemetryFrame>::success(std::move(frame));
}

}  // namespace kf2::telemetry_pipeline
