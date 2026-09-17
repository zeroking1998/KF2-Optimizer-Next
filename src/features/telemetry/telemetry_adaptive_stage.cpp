#include "features/telemetry/telemetry_adaptive_stage.hpp"

#include "app/application_runtime.hpp"

namespace kf2::telemetry_pipeline {

void run_adaptive_stage(app::UiRuntime& runtime,
                        const TelemetryFrame& frame) {
    runtime.update_adaptive_controller(frame);
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {
void UiRuntime::poll_adaptive_runtime_mode() {
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
}

void UiRuntime::reconcile_adaptive_runtime_mode(
    const telemetry_pipeline::TelemetryFrame& frame) {
    const auto now_ns = frame.observed_at_ns;
    if (frame.gameplay && frame.gameplay->telemetry_control_port &&
        game_process &&
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
}

void UiRuntime::log_adaptive_performance_sample(
    const telemetry_pipeline::TelemetryFrame& frame) {
    const auto now_ns = frame.observed_at_ns;
    const bool active_gameplay = frame.active_gameplay;
    const bool adaptive_mode =
        optimizer_settings.adaptive_optimization_enabled;
    const bool performance_mode_changed =
        !last_performance_sample_adaptive_mode.has_value() ||
        *last_performance_sample_adaptive_mode != adaptive_mode;
    if (optimizer_settings.adaptive_logging &&
        telemetry_pipeline::should_log_performance_sample(
            active_gameplay, adaptive_runtime_mode_confirmed,
            performance_mode_changed, frame.frames, now_ns,
            last_performance_sample_log_ns)) {
        std::wostringstream measurement;
        measurement << std::fixed << std::setprecision(2)
                    << L"mode=" << (adaptive_mode ? L"on" : L"off")
                    << L"; current=" << *frame.frames.fps << L" FPS"
                    << L"; frame=" << *frame.frames.frame_time_ms << L" ms"
                    << L"; avg3s=" << *frame.frames.average_fps << L" FPS"
                    << L"; p95=" << *frame.frames.p95_ms << L" ms"
                    << L"; 1%low3s="
                    << *frame.frames.sustained_one_percent_low_fps << L" FPS"
                    << L"; 1%low10s=" << *frame.frames.one_percent_low_fps
                    << L" FPS; stutters5s=" << frame.frames.stutter_count
                    << L"; sampleNs=" << now_ns
                    << L"; processStartId=" << frame.identity.process_start_id;
        if (frame.gameplay && !frame.gameplay->map.empty()) {
            measurement << L"; map="
                        << std::wstring{frame.gameplay->map.begin(),
                                        frame.gameplay->map.end()};
        }
        events->append({0, diagnostics::Severity::info,
                        "PERFORMANCE_SAMPLE", measurement.str(),
                        L"telemetry"});
        last_performance_sample_log_ns = now_ns;
        last_performance_sample_adaptive_mode = adaptive_mode;
    }
}

telemetry_pipeline::CorpseTelemetryTracker::Result
UiRuntime::update_adaptive_corpse_status(
    const telemetry_pipeline::TelemetryFrame& frame,
    ui::UiStatus& status) {
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
    return corpse_state;
}

bool UiRuntime::present_pending_adaptive_runtime_mode(
    ui::UiStatus& status) {
    if (!adaptive_runtime_mode_confirmed &&
        (adaptive_runtime_mode_pending.has_value() ||
         adaptive_runtime_mode_port.has_value())) {
        const bool desired_enabled =
            optimizer_settings.adaptive_optimization_enabled;
        status.adaptive_optimization_enabled = desired_enabled;
        status.adaptive_state = desired_enabled ? L"waiting" : L"off";
        status.adaptive_action = desired_enabled ? L"blocked" : L"none";
        status.adaptive_reason = desired_enabled
            ? adaptive_mode_dispatcher.busy()
                ? L"Waiting for the current KF2 provider to confirm Adaptive optimization"
                : L"Adaptive optimization will start after protected KF2 confirmation"
            : adaptive_mode_dispatcher.busy()
                ? L"Adaptive optimization is off; KF2 runtime restoration is being confirmed"
                : L"Adaptive optimization is off; runtime restoration confirmation is pending";
        status.adaptive_safety = L"fail closed";
        status.adaptive_evidence = L"MODE_READBACK_PENDING";
        return true;
    }
    return false;
}

void UiRuntime::log_adaptive_quality_response(
    const std::optional<optimizer::QualityResponse::Report>& report) {
    if (!report || !optimizer_settings.adaptive_logging) return;

    std::wostringstream message;
    message << L"seq=" << report->sequence << L"; resource="
            << std::wstring{report->resource.begin(), report->resource.end()}
            << L"; quality=" << report->from << L"->" << report->to
            << L"; result="
            << std::wstring{report->result.begin(), report->result.end()}
            << L"; nominalWindowMs=5000; settleMs=1000; causalProof=false";
    const auto append = [&](const wchar_t* label, const auto& window) {
        message << L"; " << label << L"SpanMs="
                << window.span_ns / 1'000'000
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
    append(L"before", report->before);
    append(L"after", report->after);
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_QUALITY_RESPONSE", message.str(), L"optimizer"});
}

optimizer::QualityResponse::Context
UiRuntime::observe_adaptive_quality_response(
    const telemetry_pipeline::TelemetryFrame& frame) {
    const auto now_ns = frame.observed_at_ns;
    optimizer::QualityResponse::Context response_context{
        frame.identity, frame.gameplay ? frame.gameplay->map : "",
        frame.adapter_luid, effective_target_fps(),
        frame.gameplay ? frame.gameplay->telemetry_living_visible : std::nullopt,
        frame.gameplay ? frame.gameplay->telemetry_corpse_total : std::nullopt,
        frame.active_gameplay &&
        frame.offline_gameplay && frame.gameplay && frame.adapter_luid &&
        frame.gameplay->telemetry_sample.value_or(0) > 0 &&
        frame.gameplay->telemetry_observed_ns != 0 &&
        now_ns >= frame.gameplay->telemetry_observed_ns &&
        now_ns - frame.gameplay->telemetry_observed_ns <=
            game::kGameLogObservationFreshnessNs &&
        frame.gameplay->telemetry_zed_time_active == false &&
        frame.frames.quality == telemetry::SampleQuality::good};
    const auto baseline_end = quality_response.baseline_end_ns();
    if (present_source &&
        baseline_end >= optimizer::QualityResponse::window_ns &&
        now_ns >= baseline_end &&
        now_ns - baseline_end <=
            optimizer::QualityResponse::delivery_grace_ns) {
        quality_response.refresh_baseline(
            now_ns, present_source->measure_window(
                baseline_end - optimizer::QualityResponse::window_ns,
                baseline_end));
    }
    const auto response_end = quality_response.end_ns();
    const auto post_window = present_source && response_end &&
            now_ns >= response_end
        ? present_source->measure_window(
              response_end - optimizer::QualityResponse::window_ns,
              response_end)
        : telemetry::PresentSource::Window{};
    const auto response_report =
        quality_response.observe(response_context, now_ns, post_window);
    log_adaptive_quality_response(response_report);
    if (!response_report) return response_context;

    const auto feedback =
        telemetry_pipeline::adaptive_quality_response_feedback(
            response_report->result, response_report->resource,
            response_report->from, response_report->to);
    if (!feedback.reduction_floor_quality) return response_context;

    adaptive_quality_reduction_floor.apply({
        0, *feedback.resource, *feedback.reduction_floor_quality});
    if (feedback.rollback_quality) {
        adaptive_quality_rollback_target = std::max(
            adaptive_quality_rollback_target.value_or(
                optimizer_settings.adaptive_minimum_quality),
            *feedback.rollback_quality);
        adaptive_quality_rollback_resource = feedback.resource;
        events->append({
            0, diagnostics::Severity::info,
            "ADAPTIVE_QUALITY_ROLLBACK_QUEUED",
            L"The last quality reduction did not improve frame pacing; its previous quality is being restored and will not be reduced again during this gameplay session",
            L"optimizer"});
    } else {
        events->append({
            0, diagnostics::Severity::info,
            "ADAPTIVE_QUALITY_REDUCTION_HELD",
            L"The quality response was not comparable enough to justify another reduction; the current resource level is held for this gameplay session",
            L"optimizer"});
    }
    return response_context;
}

void UiRuntime::poll_adaptive_quality_dispatcher() {
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
                    if (adaptive_quality_rollback_resource &&
                        outcome->value().resource ==
                            *adaptive_quality_rollback_resource &&
                        adaptive_quality_rollback_target &&
                        outcome->value().quality >=
                            *adaptive_quality_rollback_target) {
                        adaptive_quality_rollback_target.reset();
                        adaptive_quality_rollback_resource.reset();
                    }
                    adaptive_quality_last_applied_ns = completed_ns;
                    adaptive_frame_not_before_ns = completed_ns;
                    adaptive_governor.notify_quality_applied(completed_ns);
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
}
}  // namespace kf2::app
