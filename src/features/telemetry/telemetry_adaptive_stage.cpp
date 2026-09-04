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
    const auto now_ns = frame.observed_at_ns;
    const bool active_gameplay = frame.active_gameplay;
    auto status = model.status();
    const auto corpse_state = corpse_telemetry_tracker.observe(frame,
        game_process && game_process->pid == frame.identity.pid &&
        game_process->process_start_id == frame.identity.process_start_id &&
        adaptive_locks_valid && !adaptive_overhead_frozen);
    using telemetry_pipeline::CorpseTelemetryState;
    status.adaptive_corpse_capability =
        corpse_state.state == CorpseTelemetryState::available ? L"AVAILABLE" :
        corpse_state.state == CorpseTelemetryState::stale ? L"STALE" : L"UNAVAILABLE";
    status.adaptive_runtime_corpse_limit = corpse_state.runtime_limit;
    if (corpse_state.state != CorpseTelemetryState::available)
        status.adaptive_corpse_action_status = L"NONE";
    if (corpse_state.event) {
        events->append({0, diagnostics::Severity::info, corpse_state.event,
            corpse_state.state == CorpseTelemetryState::stale
                ? L"Corpse telemetry is temporarily stale; last confirmed limit is display-only for up to 10 seconds; quality/corpse decisions are paused"
                : corpse_state.state == CorpseTelemetryState::available
                ? L"Fresh corpse telemetry confirmed; runtime limit is current again"
                : L"Corpse telemetry is unavailable; no cached runtime value is used",
            L"game"});
    }
    const auto log_response = [&](const auto& report) {
        if (!report || !optimizer_settings.adaptive_logging) return;
        const auto& r = *report;
        std::wostringstream message;
        message << L"seq=" << r.sequence << L"; resource=" << std::wstring{r.resource.begin(), r.resource.end()}
            << L"; quality=" << r.from << L"->" << r.to
            << L"; result=" << std::wstring{r.result.begin(), r.result.end()}
            << L"; nominalWindowMs=5000; settleMs=1000; causalProof=false";
        const auto append = [&](const wchar_t* label, const auto& window) {
            message << L"; " << label << L"SpanMs=" << window.span_ns / 1'000'000
                << L"; " << label << L"Frames=" << window.count;
            const auto metric = [&](const wchar_t* name, auto value) {
                message << L"; " << label << name << L"=";
                if (value) message << *value;
                else message << L"NOT_AVAILABLE";
            };
            metric(L"AvgFps", window.metrics.average_fps);
            metric(L"P95ms", window.metrics.p95_ms);
            metric(L"LowFps", window.metrics.one_percent_low_fps);
        };
        append(L"before", r.before);
        append(L"after", r.after);
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_QUALITY_RESPONSE", message.str(), L"optimizer"});
    };
    optimizer::QualityResponse::Context response_context{
        frame.identity, frame.gameplay ? frame.gameplay->map : "",
        frame.adapter_luid, effective_target_fps(),
        frame.gameplay ? frame.gameplay->telemetry_living_visible : std::nullopt,
        frame.gameplay ? frame.gameplay->telemetry_corpse_total : std::nullopt,
        optimizer_settings.adaptive_optimization_enabled && frame.active_gameplay &&
        frame.offline_gameplay && frame.gameplay && frame.adapter_luid &&
        frame.gameplay->telemetry_sample.value_or(0) > 0 &&
        frame.gameplay->telemetry_observed_ns != 0 &&
        now_ns >= frame.gameplay->telemetry_observed_ns &&
        now_ns - frame.gameplay->telemetry_observed_ns <= game::kGameLogObservationFreshnessNs &&
        frame.gameplay->telemetry_zed_time_active == false &&
        frame.frames.quality == telemetry::SampleQuality::good};
    const auto baseline_end = quality_response.baseline_end_ns();
    if (present_source && baseline_end >= optimizer::QualityResponse::window_ns &&
        now_ns >= baseline_end &&
        now_ns - baseline_end <= optimizer::QualityResponse::delivery_grace_ns) {
        quality_response.refresh_baseline(now_ns, present_source->measure_window(
            baseline_end - optimizer::QualityResponse::window_ns, baseline_end));
    }
    const auto response_end = quality_response.end_ns();
    const auto post_window = present_source && response_end && now_ns >= response_end
        ? present_source->measure_window(response_end - optimizer::QualityResponse::window_ns,
                                          response_end)
        : telemetry::PresentSource::Window{};
    log_response(quality_response.observe(response_context, now_ns, post_window));
    if (auto mode_outcome = adaptive_mode_dispatcher.poll()) {
        const bool expected_enabled =
            adaptive_runtime_mode_pending.value_or(
                optimizer_settings.adaptive_optimization_enabled);
        const auto expected_resource = expected_enabled
            ? game::AdaptiveResourceControl::enable
            : game::AdaptiveResourceControl::disable;
        adaptive_runtime_mode_confirmed =
            mode_outcome->has_value() &&
            mode_outcome->value().resource == expected_resource;
        events->append({
            0,
            adaptive_runtime_mode_confirmed
                ? diagnostics::Severity::info
                : diagnostics::Severity::warning,
            adaptive_runtime_mode_confirmed
                ? "ADAPTIVE_RUNTIME_MODE_RECONCILED"
                : "ADAPTIVE_RUNTIME_MODE_RECONCILE_FAILED",
            adaptive_runtime_mode_confirmed
                ? L"The current KF2 provider confirmed the saved Adaptive mode with an authenticated APPLIED readback"
                : L"The current KF2 provider did not confirm the saved Adaptive mode; automatic actions remain blocked",
            L"optimizer"});
        adaptive_runtime_mode_pending.reset();
    }
    if (frame.gameplay && frame.gameplay->telemetry_control_port &&
        game_process &&
        (corpse_state.state != CorpseTelemetryState::stale ||
         !optimizer_settings.adaptive_optimization_enabled) &&
        game::valid_adaptive_control_token(adaptive_control_token)) {
        const auto port = *frame.gameplay->telemetry_control_port;
        const bool new_provider =
            adaptive_runtime_mode_process_start_id !=
                game_process->process_start_id ||
            adaptive_runtime_mode_port != port;
        const bool retry_due = !adaptive_runtime_mode_confirmed &&
            (adaptive_runtime_mode_last_attempt_ns == 0 ||
             now_ns >= adaptive_runtime_mode_last_attempt_ns +
                           5'000'000'000ULL);
        if ((new_provider || retry_due) &&
            !adaptive_mode_dispatcher.busy()) {
            adaptive_runtime_mode_process_start_id =
                game_process->process_start_id;
            adaptive_runtime_mode_port = port;
            adaptive_runtime_mode_last_attempt_ns = now_ns;
            adaptive_runtime_mode_confirmed = false;
            const bool desired_enabled =
                optimizer_settings.adaptive_optimization_enabled;
            const auto next_sequence =
                adaptive_control_sequence ==
                        std::numeric_limits<std::uint64_t>::max()
                    ? 1 : adaptive_control_sequence + 1;
            const auto started = adaptive_mode_dispatcher.start({
                .port = port,
                .token = adaptive_control_token,
                .sequence = next_sequence,
                .resource = desired_enabled
                    ? game::AdaptiveResourceControl::enable
                    : game::AdaptiveResourceControl::disable,
                .quality = 100});
            if (started.has_value() && started.value()) {
                adaptive_control_sequence = next_sequence;
                adaptive_runtime_mode_pending = desired_enabled;
            } else {
                events->append({0, diagnostics::Severity::warning,
                    "ADAPTIVE_RUNTIME_MODE_RECONCILE_FAILED",
                    L"The background worker could not start; automatic actions remain blocked",
                    L"optimizer"});
            }
        }
    }
    if (adaptive_runtime_mode_port &&
        !adaptive_runtime_mode_confirmed) {
        status.adaptive_state = L"mode unconfirmed";
        status.adaptive_action = L"blocked";
        status.adaptive_reason =
            adaptive_mode_dispatcher.busy()
                ? L"Waiting for the current KF2 provider to confirm the saved Adaptive mode"
                : L"The current KF2 provider did not confirm the saved Adaptive mode";
        status.adaptive_safety = L"fail closed";
        status.adaptive_evidence = L"MODE_READBACK_PENDING";
        model.set_status(std::move(status));
        return;
    }
    if (!optimizer_settings.adaptive_optimization_enabled) {
        status.adaptive_optimization_enabled = false;
        status.adaptive_state = L"off";
        status.adaptive_bottleneck = L"not evaluated";
        status.adaptive_cpu_parallelism = L"telemetry only";
        status.adaptive_action = L"none";
        status.adaptive_reason =
            L"Adaptive optimization is off; monitoring remains active";
        status.adaptive_confidence_percent = 0;
        status.adaptive_drop_risk_percent = 0;
        status.adaptive_data_quality = L"NOT_EVALUATED";
        status.adaptive_prediction = L"not evaluated";
        status.adaptive_safety = L"no adaptive actuator";
        status.adaptive_evidence = L"TELEMETRY_ONLY";
        status.adaptive_corpse_action_status = L"DISABLED";
        status.adaptive_flex_action_status = L"DISABLED";
        adaptive_gameplay_active = false;
        adaptive_governor.reset();
        adaptive_profile_gate.reset();
        adaptive_decision = {};
        model.set_status(std::move(status));
        return;
    }
    status.adaptive_optimization_enabled = true;
    if (auto outcome = adaptive_control_dispatcher.poll()) {
        if (adaptive_control_pending) {
            const auto pending = *adaptive_control_pending;
            const auto completed_ns = monotonic_ns();
            if (outcome->has_value()) {
                const auto receipt_result = adaptive_actuation.receive({
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
                    static_cast<double>(pending.previous_quality)});
                if (receipt_result ==
                    optimizer::AdaptiveReceiptResult::accepted) {
                    quality_response.confirm(pending.sequence, completed_ns);
                    adaptive_resource_quality.apply(outcome->value());
                    adaptive_quality_last_applied_ns = completed_ns;
                    adaptive_frame_not_before_ns = completed_ns;
                    adaptive_governor.notify_quality_applied(completed_ns);
                    adaptive_profile_gate.reset();
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
                            L"%, overdraw " +
                            std::to_wstring(adaptive_resource_quality.overdraw) +
                            L"%, effects " +
                            std::to_wstring(adaptive_resource_quality.effects) +
                            L"%) after an exact authenticated APPLIED readback; fresh post-action frame window started",
                        L"optimizer"});
                } else {
                    events->append({0, diagnostics::Severity::warning,
                        "ADAPTIVE_RUNTIME_QUALITY_RECEIPT_REJECTED",
                        L"An outdated or invalid quality receipt was ignored; the confirmed quality and observation window were preserved",
                        L"optimizer"});
                }
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
        last_adaptive_decision_log_ns = 0;
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
        adaptive_quality_last_applied_ns = 0;
        adaptive_frame_not_before_ns = now_ns;
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
    // Keep the overlay's historical statistics intact. Only the controller
    // excludes presents from before its latest gameplay/action boundary.
    const auto frames = present_source && adaptive_frame_not_before_ns != 0
        ? present_source->drain(now_ns, 2'000'000'000ULL,
                                adaptive_frame_not_before_ns)
        : frame.frames;
    const auto sample_build = telemetry_pipeline::build_adaptive_sample(
        frame,
        {.current_quality = current_quality,
         .minimum_quality = optimizer_settings.adaptive_minimum_quality,
         .user_max_dead_bodies = effective_corpse_limit(),
         .current_map = adaptive_map,
         .map_generation = adaptive_map_generation,
         .last_telemetry_sample = adaptive_telemetry_sample,
         .flex_now_ms =
             flex_pressure_candidate ? GetTickCount64() : 0,
         .effects_control_verified = frame.offline_gameplay &&
             frame.gameplay &&
             frame.gameplay->telemetry_control_port.has_value() &&
             game::valid_adaptive_control_token(adaptive_control_token),
         .decision_frames = frames});
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
    const bool waiting_for_corpse_readback =
        corpse_state.state == CorpseTelemetryState::stale ||
        (corpse_state.state == CorpseTelemetryState::unavailable &&
         sample.capabilities.corpse_control == optimizer::AdaptiveCapabilityState::available);
    if (requires_fresh_frame_window(sample_build) || waiting_for_corpse_readback) {
        adaptive_frame_not_before_ns = now_ns;
        adaptive_governor.reset();
        adaptive_decision = {};
        adaptive_profile_gate.reset();
        if (sample_build.waiting_for_gameplay_telemetry || waiting_for_corpse_readback) {
            // Keep advancing the controller boundary while loading, without
            // repeatedly clearing the overlay history or flooding the log.
            status.adaptive_state = L"observing";
            status.adaptive_action = L"hold";
            status.adaptive_bottleneck = L"unknown";
            status.adaptive_cpu_parallelism = L"waiting for gameplay telemetry";
            status.adaptive_reason = L"Waiting for fresh gameplay telemetry";
            status.adaptive_confidence_percent = 0;
            status.adaptive_drop_risk_percent = 0;
            status.adaptive_headroom_available_percent = 0;
            status.adaptive_data_quality = L"NOT_AVAILABLE";
            status.adaptive_prediction = L"not available";
            status.adaptive_source = L"not selected";
            status.adaptive_safety = L"no actuator";
            status.adaptive_evidence = L"NOT_AVAILABLE";
            status.recommendation_reason = status.adaptive_reason;
            model.set_status(std::move(status));
            return;
        }
        if (present_source) present_source->reset_statistics();
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_FRAME_WINDOW_RESET",
            L"Adaptive discarded pre-map and loading-frame statistics and is collecting a fresh gameplay window",
            L"optimizer"});
        model.set_status(std::move(status));
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
    status.adaptive_flex_capability = widen(
        optimizer::adaptive_capability_state_name(
            sample.capabilities.flex_solver_substep_control));
    status.adaptive_particle_capability = widen(
        optimizer::adaptive_capability_state_name(
            sample.capabilities.flex_particle_budget_control));
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
            optimizer::AdaptiveControlId::corpse_runtime_limit);
        corpse_state.state == CorpseTelemetryState::available && corpse_action) {
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
            adaptive_decision.resources.primary_confidence,
            adaptive_decision.bottleneck.type,
            adaptive_decision.bottleneck.confidence,
            adaptive_resource_quality.overdraw <=
                optimizer_settings.adaptive_minimum_quality,
            sample.capabilities.gore_control ==
                optimizer::AdaptiveCapabilityState::available &&
                sample.capabilities.particle_control ==
                    optimizer::AdaptiveCapabilityState::available);
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
            .bottleneck = adaptive_decision.bottleneck.type,
            .bottleneck_confidence =
                adaptive_decision.bottleneck.confidence,
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
            .overdraw_minimum_reached =
                adaptive_resource_quality.overdraw <=
                    optimizer_settings.adaptive_minimum_quality,
            .effects_control_available =
                sample.capabilities.gore_control ==
                    optimizer::AdaptiveCapabilityState::available &&
                sample.capabilities.particle_control ==
                    optimizer::AdaptiveCapabilityState::available,
            .now_ns = now_ns,
            .last_dispatch_ns = adaptive_quality_last_dispatch_ns,
            .last_applied_ns = adaptive_quality_last_applied_ns,
            .sample_timestamp_ns = sample.timestamp_ns});
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
                log_response(quality_response.cancel("superseded_by_next_action"));
                const auto baseline = present_source && now_ns >= optimizer::QualityResponse::window_ns &&
                    now_ns - optimizer::QualityResponse::window_ns >= adaptive_frame_not_before_ns
                    ? present_source->measure_window(now_ns - optimizer::QualityResponse::window_ns, now_ns)
                    : telemetry::PresentSource::Window{};
                quality_response.begin(next_sequence,
                    std::string{game::adaptive_resource_control_name(runtime_selection->resource)},
                    previous_quality, runtime_selection->quality, now_ns,
                    response_context, baseline);
                adaptive_control_pending = AdaptiveRuntimePendingRequest{
                    .sequence = next_sequence,
                    .action_id = proposed.action_id,
                    .generation = proposed.generation,
                    .previous_quality = previous_quality,
                    .requested_quality = runtime_selection->quality};
                // Record the exact dispatch evidence separately from the
                // throttled decision log. This is a request, not an APPLIED
                // receipt; never include the authenticated bridge token.
                if (optimizer_settings.adaptive_logging) {
                    const auto resource_name = game::adaptive_resource_control_name(
                        runtime_selection->resource);
                    std::wostringstream request_log;
                    request_log << L"seq=" << next_sequence
                        << L"; resource=" << widen(resource_name)
                        << L"; quality=" << previous_quality << L"->"
                        << runtime_selection->quality
                        << L"; state=" << optimizer::adaptive_controller_state_name(
                            adaptive_decision.state)
                        << L"; framePressure=" << adaptive_decision.current_frame_pressure
                        << L"; memoryPressure=" << adaptive_decision.current_resource_pressure
                        << L"; target=" << effective_target_fps();
                    const auto append_metric = [&](std::wstring_view name,
                                                   std::optional<double> value) {
                        request_log << L"; " << name << L"=";
                        if (value) request_log << *value;
                        else request_log << L"NOT_AVAILABLE";
                    };
                    append_metric(L"fps", sample.fps);
                    append_metric(L"avg", sample.average_fps);
                    append_metric(L"p95ms", sample.p95_frame_time_ms);
                    append_metric(L"low3s", sample.sustained_one_percent_low_fps);
                    append_metric(L"low10s", sample.one_percent_low_fps);
                    request_log << L"; stutters=" << sample.stutter_count
                        << L"; sampleNs=" << sample.timestamp_ns
                        << L"; lastAppliedNs=" << adaptive_quality_last_applied_ns;
                    events->append({0, diagnostics::Severity::info,
                        "ADAPTIVE_RUNTIME_QUALITY_REQUESTED",
                        request_log.str(), L"optimizer"});
                }
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

    const bool controller_changed =
        adaptive_decision.state != last_adaptive_state ||
        adaptive_decision.disposition != last_adaptive_disposition;
    const bool bottleneck_changed =
        adaptive_decision.bottleneck.type != last_adaptive_bottleneck;
    const bool log_decision = telemetry_pipeline::should_log_adaptive_decision(
        controller_changed, bottleneck_changed, now_ns,
        last_adaptive_decision_log_ns);
    if (log_decision && optimizer_settings.adaptive_logging) {
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
        decision_log << L"; frameWindowSinceNs=" << adaptive_frame_not_before_ns
                     << L"; lastQualityAppliedNs=" << adaptive_quality_last_applied_ns;
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
        last_adaptive_state = adaptive_decision.state;
        last_adaptive_disposition = adaptive_decision.disposition;
        last_adaptive_bottleneck = adaptive_decision.bottleneck.type;
        last_adaptive_decision_log_ns = now_ns;
    } else if (!optimizer_settings.adaptive_logging) {
        last_adaptive_state = adaptive_decision.state;
        last_adaptive_disposition = adaptive_decision.disposition;
        last_adaptive_bottleneck = adaptive_decision.bottleneck.type;
        last_adaptive_decision_log_ns = now_ns;
    }
    model.set_status(std::move(status));
}

}  // namespace kf2::app
