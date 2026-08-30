#include "features/telemetry/telemetry_adaptive_stage.hpp"

#include "app/application_runtime.hpp"
#include "features/telemetry/telemetry_effect_stage.hpp"

namespace kf2::telemetry_pipeline {

void run_adaptive_stage(app::UiRuntime& runtime,
                        const TelemetryFrame& frame) {
    runtime.update_adaptive_controller(frame);
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {

void UiRuntime::update_adaptive_controller(
    const telemetry_pipeline::TelemetryFrame& frame) {
    const auto& frames = frame.frames;
    const auto now_ns = frame.observed_at_ns;
    const bool active_gameplay = frame.active_gameplay;
    auto status = model.status();
    if (auto outcome = adaptive_control_dispatcher.poll()) {
        if (adaptive_control_pending) {
            const auto pending = *adaptive_control_pending;
            const auto completed_ns = monotonic_ns();
            if (outcome->has_value()) {
                static_cast<void>(adaptive_actuation.receive({
                    pending.action_id,
                    optimizer::AdaptiveControlId::runtime_quality,
                    optimizer::AdaptiveActionStatus::applied,
                    static_cast<double>(pending.requested_quality),
                    static_cast<double>(outcome->value().quality),
                    pending.generation,
                    completed_ns,
                    "kf2_loopback_readback",
                    {},
                    true,
                    static_cast<double>(pending.previous_quality)}));
                adaptive_resource_quality.apply(outcome->value());
                const int effective_quality =
                    adaptive_resource_quality.effective_quality();
                const auto resource_name =
                    game::adaptive_resource_control_name(
                        outcome->value().resource);
                events->append({
                    0, diagnostics::Severity::info,
                    "ADAPTIVE_RUNTIME_QUALITY_APPLIED",
                    L"Live KF2 " + std::wstring{
                        resource_name.begin(), resource_name.end()} +
                        L" quality changed to " +
                        std::to_wstring(outcome->value().quality) +
                        L"% (effective " +
                        std::to_wstring(effective_quality) +
                        L"%; CPU " +
                        std::to_wstring(adaptive_resource_quality.cpu) +
                        L"%, GPU " +
                        std::to_wstring(adaptive_resource_quality.gpu) +
                        L"%, VRAM " +
                        std::to_wstring(adaptive_resource_quality.vram) +
                        L"%, RAM " +
                        std::to_wstring(adaptive_resource_quality.ram) +
                        L"%) after an exact authenticated APPLIED readback",
                    L"optimizer"});
            } else {
                static_cast<void>(adaptive_actuation.receive({
                    pending.action_id,
                    optimizer::AdaptiveControlId::runtime_quality,
                    optimizer::AdaptiveActionStatus::failed,
                    static_cast<double>(pending.requested_quality),
                    {},
                    pending.generation,
                    completed_ns,
                    "kf2_loopback_readback",
                    "bridge_send_or_readback_failed"}));
                events->append({
                    0, diagnostics::Severity::warning,
                    "ADAPTIVE_RUNTIME_QUALITY_FAILED",
                    L"Live KF2 quality was not changed because the authenticated bridge did not return an exact APPLIED readback",
                    L"optimizer"});
            }
            adaptive_control_pending.reset();
        }
    }
    if (!game_process || !adaptive_locks_valid || adaptive_overhead_frozen) {
        status.adaptive_state = adaptive_locks_valid && !adaptive_overhead_frozen
                ? L"ready" : L"frozen";
        status.adaptive_bottleneck = L"unknown";
        status.adaptive_cpu_parallelism = L"not available";
        status.adaptive_action = L"blocked";
        status.adaptive_reason = adaptive_overhead_frozen
            ? L"Controller iteration budget repeatedly exceeded; Adaptive remains frozen until a safe reset"
            : adaptive_locks_valid
                ? L"Waiting for a verified KF2 process"
                : L"Per-setting locks are invalid; Adaptive fails closed";
        status.adaptive_confidence_percent = 0;
        status.adaptive_drop_risk_percent = 0;
        status.adaptive_quality_score =
            optimizer_settings.adaptive_maximum_quality;
        status.adaptive_headroom_available_percent = 0;
        status.adaptive_data_quality = L"NOT_AVAILABLE";
        status.adaptive_prediction = L"not available";
        status.adaptive_session = L"SESSION_UNKNOWN";
        status.adaptive_source = L"not selected";
        status.adaptive_safety = L"no actuator";
        status.adaptive_evidence = L"NOT_AVAILABLE";
        status.adaptive_runtime_corpse_limit.reset();
        status.adaptive_corpse_capability = L"UNAVAILABLE";
        status.adaptive_corpse_action_status = L"NONE";
        status.adaptive_flex_requested_substeps.reset();
        status.adaptive_flex_effective_substeps.reset();
        status.adaptive_flex_action_status = L"NONE";
        status.adaptive_flex_capability = L"UNAVAILABLE";
        status.adaptive_particle_capability = L"UNAVAILABLE";
        status.adaptive_restore_generation = 0;
        status.adaptive_shadow_mode = optimizer_settings.adaptive_shadow_mode;
        const auto launch_profile = optimizer::bound_adaptive_profile(
            stored_adaptive_profile(optimizer_settings),
            optimizer_settings.adaptive_minimum_quality,
            optimizer_settings.adaptive_maximum_quality);
        status.recommended_profile = launch_profile
            ? std::wstring{optimizer::adaptive_profile_label(*launch_profile)}
            : L"not available";
        status.recommendation_reason = launch_profile
                ? L"Automatic launch profile is ready; live telemetry will refine the next session"
                : L"No verified named profile fits the selected quality limits";
        model.set_status(std::move(status));
        return;
    }

    if (!active_gameplay) {
        if (adaptive_gameplay_active) {
            adaptive_gameplay_active = false;
            adaptive_governor.reset();
            adaptive_profile_gate.reset();
            adaptive_decision = {};
            events->append({0, diagnostics::Severity::info,
                "ADAPTIVE_GAMEPLAY_PAUSED",
                L"Adaptive stopped evaluating menu/loading frames and preserved the last stable next-launch profile",
                L"optimizer"});
        }
        status.adaptive_state = L"observing";
        status.adaptive_bottleneck = L"unknown";
        status.adaptive_cpu_parallelism = L"waiting for active gameplay";
        status.adaptive_action = L"hold";
        status.adaptive_reason =
            L"Waiting for active gameplay; menu and loading frames are excluded";
        status.adaptive_confidence_percent = 0;
        status.adaptive_drop_risk_percent = 0;
        status.adaptive_quality_score =
            optimizer_settings.adaptive_maximum_quality;
        status.adaptive_headroom_available_percent = 0;
        status.adaptive_data_quality = L"NOT_AVAILABLE";
        status.adaptive_prediction = L"not available";
        status.adaptive_session = L"SESSION_UNKNOWN";
        status.adaptive_source = L"not selected";
        status.adaptive_safety = L"no actuator";
        status.adaptive_evidence = L"NOT_AVAILABLE";
        status.adaptive_restore_generation = 0;
        status.adaptive_shadow_mode = optimizer_settings.adaptive_shadow_mode;
        const auto launch_profile = optimizer::bound_adaptive_profile(
            stored_adaptive_profile(optimizer_settings),
            optimizer_settings.adaptive_minimum_quality,
            optimizer_settings.adaptive_maximum_quality);
        status.recommended_profile = launch_profile
            ? std::wstring{optimizer::adaptive_profile_label(*launch_profile)}
            : L"not available";
        status.recommendation_reason = status.adaptive_reason;
        last_adaptive_state = optimizer::AdaptiveControllerState::observing;
        last_adaptive_disposition = optimizer::AdaptiveDisposition::hold;
        last_adaptive_bottleneck = optimizer::AdaptiveBottleneck::unknown;
        model.set_status(std::move(status));
        return;
    }

    if (!adaptive_gameplay_active) {
        adaptive_gameplay_active = true;
        adaptive_governor.reset();
        adaptive_profile_gate.reset();
        adaptive_decision = {};
        adaptive_resource_quality.reset(
            optimizer_settings.adaptive_maximum_quality);
        adaptive_quality_last_dispatch_ns = 0;
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_GAMEPLAY_STARTED",
            L"Adaptive began a fresh controller window after verified active gameplay was detected",
            L"optimizer"});
    }

    const int current_quality = std::clamp(
        adaptive_resource_quality.effective_quality(),
        optimizer_settings.adaptive_minimum_quality,
        optimizer_settings.adaptive_maximum_quality);
    const bool flex_pressure_candidate = frame.flex && frame.flex->fresh &&
        frame.flex->aggregate_particles_fresh &&
        frame.flex->particle_capacity > 0 &&
        frame.flex->aggregate_active_particles >= 0 &&
        frame.flex->last_update_tick != 0;
    const auto sample_build = telemetry_pipeline::build_adaptive_sample(
        frame,
        {.current_quality = current_quality,
         .minimum_quality = optimizer_settings.adaptive_minimum_quality,
         .user_max_dead_bodies = effective_corpse_limit(),
         .current_map = adaptive_map,
         .map_generation = adaptive_map_generation,
         .last_telemetry_sample = adaptive_telemetry_sample,
         .flex_now_ms =
             flex_pressure_candidate ? GetTickCount64() : 0});
    adaptive_map = sample_build.map;
    adaptive_map_generation = sample_build.map_generation;
    adaptive_telemetry_sample = sample_build.telemetry_sample;
    const auto& sample = sample_build.sample;
    if (!adaptive_provider_confirmed &&
        sample.capabilities.corpse_telemetry ==
            optimizer::AdaptiveCapabilityState::available) {
        adaptive_provider_confirmed = true;
        events->append({0, diagnostics::Severity::info,
            "GAMEPLAY_PROVIDER_CONFIRMED",
            L"KF2 runtime telemetry confirmed the protected Published provider and exposed its current capabilities",
            L"game"});
    }
    if (requires_fresh_frame_window(sample_build) && present_source) {
        present_source->reset_statistics();
        adaptive_governor.reset();
        adaptive_decision = {};
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_FRAME_WINDOW_RESET",
            L"Adaptive discarded pre-map and loading-frame statistics and is collecting a fresh gameplay window",
            L"optimizer"});
        return;
    }

    auto policy = adaptive_policy_from(optimizer_settings);
    policy.target_fps = effective_target_fps();
    policy.quality_change_budget = effective_quality_change_budget();
    const auto controller_started_ns = monotonic_ns();
    adaptive_decision = adaptive_governor.evaluate(
        policy, sample, now_ns, adaptive_lock_cache);
    const auto controller_finished_ns = monotonic_ns();
    const auto controller_cost_ns =
        controller_finished_ns >= controller_started_ns
            ? controller_finished_ns - controller_started_ns
            : policy.controller_iteration_budget_ns + 1;
    if (controller_cost_ns > policy.controller_iteration_budget_ns) {
        ++adaptive_overhead_breaches;
        adaptive_decision.disposition =
            optimizer::AdaptiveDisposition::shadow;
        adaptive_decision.reason = "controller_overhead_budget_shadow";
        if (adaptive_overhead_breaches >= 3) {
            adaptive_overhead_frozen = true;
            adaptive_decision.state =
                optimizer::AdaptiveControllerState::frozen;
            adaptive_decision.disposition =
                optimizer::AdaptiveDisposition::blocked;
            adaptive_decision.reason =
                "controller_iteration_budget_repeatedly_exceeded";
            adaptive_decision.watchdog_frozen = true;
        }
    } else if (adaptive_overhead_breaches > 0) {
        --adaptive_overhead_breaches;
    }

    const auto widen = [](std::string_view value) {
        return std::wstring{value.begin(), value.end()};
    };
    status.adaptive_corpse_capability = widen(
        optimizer::adaptive_capability_state_name(
            sample.capabilities.corpse_control));
    status.adaptive_flex_capability = widen(
        optimizer::adaptive_capability_state_name(
            sample.capabilities.flex_solver_substep_control));
    status.adaptive_particle_capability = widen(
        optimizer::adaptive_capability_state_name(
            sample.capabilities.flex_particle_budget_control));
    status.adaptive_runtime_corpse_limit =
        sample.adaptive_corpse_runtime_limit;
    if (sample.adaptive_corpse_runtime_limit &&
        sample.capabilities.corpse_control ==
            optimizer::AdaptiveCapabilityState::available) {
        const auto control = optimizer::AdaptiveControlId::corpse_runtime_limit;
        const double observed = static_cast<double>(
            *sample.adaptive_corpse_runtime_limit);
        const auto previous = adaptive_actuation.effective_value(control);
        if (!previous) {
            adaptive_actuation.establish_effective(control, observed);
        } else if (*previous != observed) {
            const auto& action = adaptive_actuation.propose(
                control, observed, previous,
                optimizer::AdaptiveCapabilityState::available,
                now_ns, "corpse_autonomous_provider");
            if (action.status == optimizer::AdaptiveActionStatus::proposed &&
                adaptive_actuation.dispatch(control, now_ns)) {
                static_cast<void>(adaptive_actuation.receive({
                    action.action_id,
                    action.control,
                    optimizer::AdaptiveActionStatus::applied,
                    action.requested_value,
                    observed,
                    action.generation,
                    now_ns,
                    "corpse_telemetry_readback",
                    {},
                    true,
                    previous}));
            }
        }
    }
    if (const auto* corpse_action = adaptive_actuation.current(
            optimizer::AdaptiveControlId::corpse_runtime_limit)) {
        status.adaptive_corpse_action_status = widen(
            optimizer::adaptive_action_status_name(corpse_action->status));
    } else {
        status.adaptive_corpse_action_status = L"NONE";
    }

    const auto runtime_control =
        optimizer::AdaptiveControlId::runtime_quality;
    const int effective_runtime_quality =
        adaptive_resource_quality.effective_quality();
    adaptive_actuation.establish_effective(
        runtime_control, static_cast<double>(effective_runtime_quality));
    bool bridge_available = frame.gameplay &&
        frame.gameplay->telemetry_control_port.has_value() &&
        game::valid_adaptive_control_token(adaptive_control_token) &&
        !adaptive_control_dispatcher.busy();
    const auto pressure_resource =
        telemetry_pipeline::adaptive_runtime_resource(
            adaptive_decision.resources.primary,
            adaptive_decision.resources.primary_confidence);
    const int selected_runtime_quality =
        adaptive_decision.state ==
                    optimizer::AdaptiveControllerState::stable
            ? effective_runtime_quality
            : adaptive_resource_quality.control_quality(pressure_resource);
    const auto runtime_selection =
        telemetry_pipeline::select_adaptive_runtime_control({
            .state = adaptive_decision.state,
            .data_quality = adaptive_decision.data.quality,
            .primary_resource = adaptive_decision.resources.primary,
            .primary_confidence =
                adaptive_decision.resources.primary_confidence,
            .current_quality = selected_runtime_quality,
            .minimum_quality =
                optimizer_settings.adaptive_minimum_quality,
            .maximum_quality =
                optimizer_settings.adaptive_maximum_quality,
            .quality_change_budget = effective_quality_change_budget(),
            .current_frame_pressure =
                adaptive_decision.current_frame_pressure,
            .current_resource_pressure =
                adaptive_decision.current_resource_pressure,
            .recovery_eligible =
                adaptive_decision.quality_recovery_eligible,
            .active_gameplay = active_gameplay,
            .verified_offline = sample.session_class ==
                optimizer::AdaptiveSessionClass::verified_offline,
            .bridge_available = bridge_available,
            .zed_time_active = sample.zed_time_protected,
            .shadow_mode = optimizer_settings.adaptive_shadow_mode,
            .now_ns = now_ns,
            .last_dispatch_ns = adaptive_quality_last_dispatch_ns});
    if (runtime_selection) {
        const auto previous_quality = selected_runtime_quality;
        const auto& proposed = adaptive_actuation.propose(
            runtime_control,
            static_cast<double>(runtime_selection->quality),
            static_cast<double>(previous_quality),
            optimizer::AdaptiveCapabilityState::available,
            now_ns, "kf2_loopback_readback");
        if (proposed.status == optimizer::AdaptiveActionStatus::proposed &&
            adaptive_actuation.dispatch(runtime_control, now_ns)) {
            const auto next_sequence =
                adaptive_control_sequence ==
                        std::numeric_limits<std::uint64_t>::max()
                    ? 1 : adaptive_control_sequence + 1;
            const auto started = adaptive_control_dispatcher.start({
                .port = *frame.gameplay->telemetry_control_port,
                .token = adaptive_control_token,
                .sequence = next_sequence,
                .resource = runtime_selection->resource,
                .quality = runtime_selection->quality});
            if (started.has_value() && started.value()) {
                adaptive_control_sequence = next_sequence;
                adaptive_quality_last_dispatch_ns = now_ns;
                adaptive_control_pending = AdaptiveRuntimePendingRequest{
                    .action_id = proposed.action_id,
                    .generation = proposed.generation,
                    .previous_quality = previous_quality,
                    .requested_quality = runtime_selection->quality};
            } else {
                static_cast<void>(adaptive_actuation.receive({
                    proposed.action_id,
                    runtime_control,
                    optimizer::AdaptiveActionStatus::failed,
                    static_cast<double>(runtime_selection->quality),
                    {},
                    proposed.generation,
                    monotonic_ns(),
                    "kf2_loopback_readback",
                    "bridge_worker_start_failed"}));
                events->append({
                    0, diagnostics::Severity::warning,
                    "ADAPTIVE_RUNTIME_QUALITY_FAILED",
                    L"Live KF2 quality was not changed because the background bridge worker could not start",
                    L"optimizer"});
            }
        }
    }
    adaptive_decision.quality_score =
        static_cast<double>(
            adaptive_resource_quality.effective_quality());
    status.adaptive_state = std::wstring{
        optimizer::adaptive_stability_state_name(
            adaptive_decision.stability_state)};
    status.adaptive_bottleneck = std::wstring{
        optimizer::adaptive_bottleneck_name(
            adaptive_decision.bottleneck.type)};
    {
        std::wostringstream cpu;
        cpu << optimizer::adaptive_cpu_workload_name(
            adaptive_decision.cpu.workload);
        if (adaptive_decision.cpu.effective_core_usage) {
            cpu << L" | " << std::fixed << std::setprecision(2)
                << *adaptive_decision.cpu.effective_core_usage
                << L" core equivalents";
        }
        if (adaptive_decision.cpu.active_threads) {
            cpu << L" | " << *adaptive_decision.cpu.active_threads
                << L" active threads";
        }
        if (adaptive_decision.cpu.critical_thread_percent) {
            cpu << L" | main " << std::fixed << std::setprecision(1)
                << *adaptive_decision.cpu.critical_thread_percent << L"%";
        }
        if (adaptive_decision.cpu.dominant_thread_share_percent) {
            cpu << L" / " << std::fixed << std::setprecision(1)
                << *adaptive_decision.cpu.dominant_thread_share_percent
                << L"% CPU-time share";
        }
        if (adaptive_decision.cpu.affinity_physical_cores ||
            adaptive_decision.cpu.affinity_logical_processors) {
            cpu << L" | affinity ";
            if (adaptive_decision.cpu.affinity_physical_cores) {
                cpu << *adaptive_decision.cpu.affinity_physical_cores
                    << L"C/";
            }
            cpu << adaptive_decision.cpu.affinity_logical_processors
                       .value_or(0) << L"T";
            if (adaptive_decision.cpu.affinity_limited) {
                cpu << L" (subset)";
            }
        }
        status.adaptive_cpu_parallelism = cpu.str();
    }
    status.adaptive_action = adaptive_decision.selected_setting.empty()
        ? std::wstring{optimizer::adaptive_disposition_name(
              adaptive_decision.disposition)}
        : widen(adaptive_decision.selected_setting) + L" (" +
              std::wstring{optimizer::adaptive_disposition_name(
                  adaptive_decision.disposition)} + L")";
    status.adaptive_reason = widen(adaptive_decision.reason);
    status.adaptive_confidence_percent = static_cast<int>(std::clamp(
        adaptive_decision.bottleneck.confidence * 100.0, 0.0, 100.0));
    status.adaptive_drop_risk_percent = static_cast<int>(std::clamp(
        adaptive_decision.drop_risk * 100.0, 0.0, 100.0));
    status.adaptive_quality_score = static_cast<int>(std::clamp(
        adaptive_decision.quality_score, 0.0, 100.0));
    status.adaptive_headroom_available_percent = static_cast<int>(
        std::clamp(adaptive_decision.headroom * 100.0, 0.0, 100.0));
    status.adaptive_data_quality =
        adaptive_decision.data.quality ==
                optimizer::AdaptiveDataQuality::valid
            ? L"VALID"
            : adaptive_decision.data.quality ==
                      optimizer::AdaptiveDataQuality::degraded
                ? L"DEGRADED" : L"NOT_AVAILABLE";
    if (adaptive_decision.predicted_frame_time_ms) {
        std::wostringstream prediction;
        prediction << std::fixed << std::setprecision(2)
                   << *adaptive_decision.predicted_frame_time_ms
                   << L" ms (" << static_cast<int>(std::clamp(
                          adaptive_decision.prediction_confidence * 100.0,
                          0.0, 100.0)) << L"%)";
        status.adaptive_prediction = prediction.str();
    } else {
        status.adaptive_prediction = L"not available";
    }
    status.adaptive_session =
        sample.session_class == optimizer::AdaptiveSessionClass::verified_offline
            ? L"VERIFIED_OFFLINE"
            : sample.session_class == optimizer::AdaptiveSessionClass::verified_online
                ? L"VERIFIED_ONLINE"
                : sample.session_class ==
                          optimizer::AdaptiveSessionClass::host_or_listen_server
                    ? L"HOST_OR_LISTEN_SERVER" : L"SESSION_UNKNOWN";
    status.adaptive_restore_generation =
        adaptive_decision.restore_generation;
    const auto* selected_record = adaptive_decision.selected_setting.empty()
        ? nullptr
        : optimizer::find_adaptive_setting(
              adaptive_decision.selected_setting);
    const auto* runtime_record =
        adaptive_actuation.current(runtime_control);
    if (runtime_record) {
        status.adaptive_source = L"authenticated KF2 loopback";
        status.adaptive_safety = L"VERIFIED_OFFLINE / EXACT_READBACK";
        status.adaptive_evidence = widen(
            optimizer::adaptive_action_status_name(runtime_record->status));
    } else if (adaptive_decision.selected_setting ==
        "AdaptiveCorpseRuntimeLimit") {
        status.adaptive_source = L"protected autonomous corpse provider";
        status.adaptive_safety = L"PROTECTED / AUTONOMOUS_RUNTIME";
        status.adaptive_evidence = widen(
            optimizer::adaptive_capability_state_name(
                sample.capabilities.corpse_control));
    } else if (selected_record) {
        status.adaptive_source = widen(selected_record->source);
        status.adaptive_safety = widen(
            optimizer::adaptive_safety_class_name(
                selected_record->safety_class)) + L" / " + widen(
            optimizer::adaptive_actuation_class_name(
                selected_record->actuation_class));
        status.adaptive_evidence = widen(
            optimizer::adaptive_evidence_state_name(
                selected_record->evidence_state));
    } else {
        status.adaptive_source = L"not selected";
        status.adaptive_safety = L"no actuator";
        status.adaptive_evidence = L"NOT_AVAILABLE";
    }
    status.adaptive_shadow_mode = optimizer_settings.adaptive_shadow_mode;

    const auto bounded_profile = optimizer::bound_adaptive_profile(
        adaptive_decision.recommended_profile,
        optimizer_settings.adaptive_minimum_quality,
        optimizer_settings.adaptive_maximum_quality);
    status.recommended_profile = bounded_profile
        ? std::wstring{optimizer::adaptive_profile_label(*bounded_profile)}
        : L"not available";
    status.recommendation_reason = bounded_profile
        ? adaptive_profile_reason(adaptive_decision)
        : L"No verified named profile fits the selected quality limits";

    std::optional<optimizer::Profile> profile_to_persist;
    if (start_mode == StartMode::normal &&
        adaptive_locks_valid &&
        adaptive_decision.data.quality ==
            optimizer::AdaptiveDataQuality::valid &&
        adaptive_decision.state !=
            optimizer::AdaptiveControllerState::observing &&
        adaptive_decision.state !=
            optimizer::AdaptiveControllerState::frozen &&
        bounded_profile) {
        profile_to_persist = adaptive_profile_gate.evaluate({
            .current = stored_adaptive_profile(optimizer_settings),
            .recommended = *bounded_profile,
            .active_gameplay = active_gameplay,
            .telemetry_valid = true,
            .recovery_eligible =
                adaptive_decision.quality_recovery_eligible,
            .now_ns = now_ns});
    } else {
        adaptive_profile_gate.reset();
    }
    if (profile_to_persist) {
        telemetry_pipeline::apply_adaptive_profile_effect(
            *this, {*profile_to_persist}, status);
    }

    const bool decision_changed =
        adaptive_decision.state != last_adaptive_state ||
        adaptive_decision.disposition != last_adaptive_disposition ||
        adaptive_decision.bottleneck.type != last_adaptive_bottleneck;
    if (decision_changed && optimizer_settings.adaptive_logging) {
        std::wostringstream decision_log;
        decision_log << L"State=" << status.adaptive_state
                     << L"; target=" << effective_target_fps()
                     << L" FPS; bands_ms="
                     << adaptive_decision.warning_frame_time_ms << L"/"
                     << adaptive_decision.corrective_frame_time_ms << L"/"
                     << adaptive_decision.critical_frame_time_ms
                     << L"; current=";
        if (frames.fps) {
            decision_log << std::fixed << std::setprecision(2)
                         << *frames.fps << L" FPS / "
                         << frames.frame_time_ms.value_or(0.0) << L" ms";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; avg3s=";
        if (frames.average_fps) {
            decision_log << *frames.average_fps << L" FPS";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; p95=";
        if (frames.p95_ms) {
            decision_log << *frames.p95_ms << L" ms";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; stutters5s=" << frames.stutter_count;
        decision_log << L"; 1%low3s=";
        if (frames.sustained_one_percent_low_fps) {
            decision_log << *frames.sustained_one_percent_low_fps << L" FPS";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; 1%low10s=";
        if (frames.one_percent_low_fps) {
            decision_log << *frames.one_percent_low_fps << L" FPS";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; CPU=";
        if (frame.evidence.cpu_percent) {
            decision_log << *frame.evidence.cpu_percent << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; systemCPU=";
        if (frame.evidence.system_cpu_percent) {
            decision_log << *frame.evidence.system_cpu_percent << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; criticalThread=";
        if (frame.evidence.critical_core_percent) {
            decision_log << *frame.evidence.critical_core_percent
                         << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; effectiveCores=";
        if (frame.evidence.effective_core_usage) {
            decision_log << *frame.evidence.effective_core_usage;
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; activeCpuThreads=";
        if (frame.evidence.active_cpu_threads) {
            decision_log << *frame.evidence.active_cpu_threads;
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; dominantThreadShare=";
        if (frame.evidence.dominant_thread_share_percent) {
            decision_log << *frame.evidence.dominant_thread_share_percent
                         << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; cpuWorkload="
                     << status.adaptive_cpu_parallelism;
        decision_log << L"; GPU=";
        if (frame.evidence.gpu_percent) {
            decision_log << *frame.evidence.gpu_percent << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; KF2GPU=";
        if (frame.evidence.process_gpu_percent) {
            decision_log << *frame.evidence.process_gpu_percent << L"%";
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; gpuSampleAgeMs=";
        if (frame.gpu_utilization) {
            decision_log << frame.gpu_utilization->sample_age_ns / 1'000'000ULL
                         << L"; gpuContinuity="
                         << frame.gpu_utilization->continuity_samples
                         << L"; gpuSampleConfidence=" << std::setprecision(0)
                         << frame.gpu_utilization->confidence * 100.0 << L"%"
                         << L"; gpuAdapterLuid=0x" << std::hex
                         << frame.gpu_utilization->adapter_luid << std::dec;
        } else {
            decision_log << L"NOT_AVAILABLE; gpuContinuity=0"
                         << L"; gpuSampleConfidence=0%"
                         << L"; gpuAdapterLuid=NOT_AVAILABLE";
        }
        const auto primary_resource = [&]() -> const wchar_t* {
            switch (adaptive_decision.resources.primary) {
                case optimizer::ResourceKind::cpu: return L"CPU";
                case optimizer::ResourceKind::gpu: return L"GPU";
                case optimizer::ResourceKind::vram: return L"VRAM";
                case optimizer::ResourceKind::ram: return L"RAM";
                case optimizer::ResourceKind::unknown: return L"UNKNOWN";
            }
            return L"UNKNOWN";
        }();
        const auto resource_scope = [&]() -> const wchar_t* {
            using optimizer::ResourceKind;
            switch (adaptive_decision.resources.primary) {
                case ResourceKind::cpu:
                    return adaptive_decision.resources.shared_cpu_pressure
                        ? L"SHARED" : L"KF2";
                case ResourceKind::gpu:
                    return adaptive_decision.resources.shared_gpu_pressure
                        ? L"SHARED" : L"KF2";
                case ResourceKind::vram:
                    return L"ADAPTER";
                case ResourceKind::ram:
                    return L"SYSTEM";
                case ResourceKind::unknown:
                    return L"UNKNOWN";
            }
            return L"UNKNOWN";
        }();
        decision_log << L"; resourcePressure=" << std::setprecision(0)
                     << L"CPU:"
                     << adaptive_decision.resources.cpu.smoothed * 100.0
                     << L"%,GPU:"
                     << adaptive_decision.resources.gpu.smoothed * 100.0
                     << L"%,VRAM:"
                     << adaptive_decision.resources.vram.smoothed * 100.0
                     << L"%,RAM:"
                     << adaptive_decision.resources.ram.smoothed * 100.0
                     << L"%; resourcePrimary=" << primary_resource
                     << L"; resourceScope=" << resource_scope
                     << L"; resourceConfidence="
                     << adaptive_decision.resources.primary_confidence * 100.0
                     << L"%; frameDeficitMs=" << std::setprecision(2)
                     << adaptive_decision.resources.frame_budget_deficit_ms
                     << L"; predictedDeficitMs="
                     << adaptive_decision.resources.predicted_deficit_ms;
        decision_log << L"; prediction=" << status.adaptive_prediction
                     << L"; dropRisk="
                     << status.adaptive_drop_risk_percent << L"%"
                     << L"; bottleneck=" << status.adaptive_bottleneck
                     << L"; confidence="
                     << status.adaptive_confidence_percent << L"%"
                     << L"; action=" << status.adaptive_action
                     << L"; corpseUserMax=" << effective_corpse_limit()
                     << L"; corpseRuntime=";
        if (status.adaptive_runtime_corpse_limit) {
            decision_log << *status.adaptive_runtime_corpse_limit;
        } else {
            decision_log << L"NOT_AVAILABLE";
        }
        decision_log << L"; corpseCapability="
                     << status.adaptive_corpse_capability
                     << L"; corpseAction="
                     << status.adaptive_corpse_action_status
                     << L"; flexCapability="
                     << status.adaptive_flex_capability
                     << L"; particleCapability="
                     << status.adaptive_particle_capability
                     << L"; reason=" << status.adaptive_reason
                     << L"; source=" << status.adaptive_source
                     << L"; safety=" << status.adaptive_safety
                     << L"; evidence=" << status.adaptive_evidence
                     << L"; session=" << status.adaptive_session
                     << L"; quality=" << status.adaptive_quality_score
                     << L"%; resourceQuality=CPU:"
                     << adaptive_resource_quality.cpu
                     << L"%,GPU:" << adaptive_resource_quality.gpu
                     << L"%,VRAM:" << adaptive_resource_quality.vram
                     << L"%,RAM:" << adaptive_resource_quality.ram
                     << L"%; measuredEffect=NOT_AVAILABLE"
                     << L"; restoreGeneration="
                     << status.adaptive_restore_generation;
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_DECISION",
            decision_log.str(),
            L"optimizer"});
    }
    last_adaptive_state = adaptive_decision.state;
    last_adaptive_disposition = adaptive_decision.disposition;
    last_adaptive_bottleneck = adaptive_decision.bottleneck.type;
    model.set_status(std::move(status));
}

}  // namespace kf2::app
