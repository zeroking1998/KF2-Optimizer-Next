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

    runtime.present_source->request_drain(now_ns, 2'000'000'000ULL);
    auto frames = runtime.present_source->latest_drain().value_or(
        ::kf2::telemetry::FrameMetrics{});
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
    input.gameplay = runtime.game_log_session;
    input.flex = current_flex_snapshot(runtime);
    input.adapter_luid = runtime.adaptive_adapter_luid;
    input.adapter_vram_budget_bytes = runtime.adapter_vram_budget;

    if (input.frames.fps && input.frames.frame_time_ms) {
        runtime.resource_telemetry_worker.request(now_ns);
        const auto snapshot = runtime.resource_telemetry_worker.latest();
        const bool current_snapshot = snapshot &&
            snapshot->generation == runtime.resource_telemetry_generation &&
            snapshot->identity.pid == input.identity.pid &&
            snapshot->identity.process_start_id ==
                input.identity.process_start_id;
        bool accepted_new_gpu_sample = false;
        if (current_snapshot &&
            snapshot->publication_sequence !=
                runtime.resource_telemetry_publication_sequence) {
            runtime.resource_telemetry_publication_sequence =
                snapshot->publication_sequence;
            if (snapshot->process_sampled_at_ns != 0 &&
                snapshot->process_sampled_at_ns !=
                    runtime.cached_process_memory_sample_ns) {
                runtime.cached_process_memory_sample_ns =
                    snapshot->process_sampled_at_ns;
                runtime.cached_process_metrics = snapshot->process;
                runtime.cached_system_memory_metrics = snapshot->system_memory;
            }
            if (snapshot->gpu_sampled_at_ns != 0 &&
                snapshot->gpu_sampled_at_ns != runtime.cached_gpu_sample_ns) {
                if (snapshot->detected_process_adapter &&
                    (!input.adapter_luid ||
                     *input.adapter_luid !=
                         snapshot->detected_process_adapter->luid)) {
                    runtime.bind_process_gpu_adapter(
                        snapshot->detected_process_adapter->luid);
                    input.adapter_luid = runtime.adaptive_adapter_luid;
                    input.adapter_vram_budget_bytes =
                        runtime.adapter_vram_budget;
                } else {
                    runtime.cached_gpu_sample_ns =
                        snapshot->gpu_sampled_at_ns;
                    runtime.cached_gpu_metrics = snapshot->gpu;
                    runtime.cached_driver_gpu_percent =
                        snapshot->driver_gpu_percent;
                    accepted_new_gpu_sample = true;
                    if (runtime.resource_telemetry_nvidia_expected &&
                        runtime.resource_telemetry_source_announced_generation !=
                            snapshot->generation) {
                        runtime.resource_telemetry_source_announced_generation =
                            snapshot->generation;
                        const bool afterburner_compatible =
                            snapshot->nvidia_source ==
                            ::kf2::telemetry::NvidiaGpuSource::
                                nvapi_dynamic_pstates;
                        runtime.events->append({0,
                            snapshot->nvidia_source
                                ? diagnostics::Severity::info
                                : diagnostics::Severity::warning,
                            snapshot->nvidia_source
                                ? "NVIDIA_TOTAL_GPU_TELEMETRY_ACTIVE"
                                : "NVIDIA_TOTAL_GPU_TELEMETRY_FALLBACK",
                            snapshot->nvidia_source
                                ? afterburner_compatible
                                    ? L"GPU usage uses the installed NVIDIA driver's dynamic P-state utilization domain, matching MSI Afterburner semantics"
                                    : L"GPU usage uses the installed NVIDIA driver's local NVML whole-device fallback"
                                : L"NVIDIA driver utilization is unavailable; adapter-wide Windows GPU telemetry is used",
                            L"telemetry"});
                    }
                }
            }
            const auto raw_process_gpu = runtime.cached_gpu_metrics
                ? runtime.cached_gpu_metrics->gpu_percent : std::nullopt;
            const auto raw_adapter_gpu =
                ::kf2::telemetry::choose_total_gpu_percent(
                    runtime.cached_driver_gpu_percent,
                    runtime.cached_gpu_metrics
                        ? runtime.cached_gpu_metrics->adapter_gpu_percent
                        : std::nullopt);
            if (accepted_new_gpu_sample) {
                runtime.cached_gpu_utilization =
                    runtime.gpu_utilization_filter.update({
                        snapshot->gpu_sampled_at_ns,
                        input.adapter_luid.value_or(0), raw_process_gpu,
                        raw_adapter_gpu});
            }
        }
        if (resource_sample_is_fresh(
                runtime.cached_process_memory_sample_ns, now_ns)) {
            input.process = runtime.cached_process_metrics;
            input.system_memory = runtime.cached_system_memory_metrics;
        }
        if (resource_sample_is_fresh(runtime.cached_gpu_sample_ns, now_ns)) {
            input.adapter_gpu = runtime.cached_gpu_metrics;
            input.driver_gpu_percent = runtime.cached_driver_gpu_percent;
            if (runtime.cached_gpu_utilization) {
                input.gpu_utilization = runtime.cached_gpu_utilization;
                input.gpu_utilization->sample_age_ns +=
                    now_ns - runtime.cached_gpu_sample_ns;
            }
        }
    }
    return build_telemetry_frame(input);
}

}  // namespace kf2::telemetry_pipeline
