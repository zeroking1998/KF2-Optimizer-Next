#include "features/telemetry/telemetry_flex_stage.hpp"

#include "app/application_runtime.hpp"
#include "features/telemetry/telemetry_effect_stage.hpp"

namespace kf2::telemetry_pipeline {

void observe_flex_source(app::UiRuntime& runtime) {
    runtime.observe_flex_process();
}

void run_flex_control_stage(app::UiRuntime& runtime,
                            const TelemetryFrame& frame) {
    if (!runtime.optimizer_settings.adaptive_optimization_enabled) return;
    std::optional<double> enemy_pressure;
    if (frame.offline_gameplay && frame.gameplay &&
        frame.gameplay->telemetry_living_visible &&
        frame.gameplay->telemetry_observed_ns != 0 &&
        frame.observed_at_ns >= frame.gameplay->telemetry_observed_ns &&
        frame.observed_at_ns - frame.gameplay->telemetry_observed_ns <=
            game::kGameLogObservationFreshnessNs) {
        enemy_pressure = visible_enemy_pressure(
            *frame.gameplay->telemetry_living_visible);
    }
    const auto capability = frame.offline_gameplay && frame.flex &&
            frame.flex->fresh && frame.flex->pass_through_healthy &&
            !frame.flex->solver_tracking_quarantined
        ? optimizer::AdaptiveCapabilityState::available
        : optimizer::AdaptiveCapabilityState::unavailable;
    const optimizer::AdaptiveGeneration generation{
        frame.identity.process_start_id,
        frame.identity.process_start_id,
        runtime.adaptive_map_generation,
        runtime.adaptive_settings_generation,
        (frame.offline_gameplay ? 1ULL : 0ULL) |
            (capability == optimizer::AdaptiveCapabilityState::available
                 ? 2ULL : 0ULL)};
    if (!(runtime.adaptive_actuation.generation() == generation)) {
        runtime.adaptive_actuation.rebase(generation, frame.observed_at_ns);
    }
    const bool pressure_actionable =
        flex_pressure_is_actionable(runtime.adaptive_decision) ||
        enemy_pressure_is_actionable(enemy_pressure);
    const bool observed_solver_ready = capability ==
            optimizer::AdaptiveCapabilityState::available &&
        frame.flex && runtime.flex_adaptive_policy.synchronize_observed(
            frame.flex->last_forwarded_substeps);
    const auto decision = decide_flex_control(
        runtime.flex_adaptive_policy,
        {.actuator_available = observed_solver_ready && pressure_actionable,
         .target_fps = runtime.effective_target_fps(),
         .quality_change_budget =
             runtime.effective_quality_change_budget(),
         .fps = frame.frames.fps,
         .enemy_pressure = enemy_pressure,
         .now_ms = GetTickCount64()});
    apply_flex_control_effect(
        runtime, {decision.requested_substeps, decision.constrained,
                  capability});
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {

bool UiRuntime::save_flex_report(const flex::ObservationSnapshot& observed) {
    if (observed.update_calls == 0) return false;
    std::ostringstream report;
    report << "{\"version\":6,\"configured_mode\":\"auto\""
               << ",\"adaptive_substep_range\":\"1..5\""
               << ",\"update_calls\":" << observed.update_calls
               << ",\"successful_updates\":" << observed.successful_updates
               << ",\"destroy_calls\":" << observed.destroy_calls
               << ",\"min_substeps\":" << observed.min_substeps
               << ",\"max_substeps\":" << observed.max_substeps
               << ",\"last_substeps\":" << observed.last_substeps
               << ",\"last_forwarded_substeps\":" << observed.last_forwarded_substeps
               << ",\"min_forwarded_substeps\":" << observed.min_forwarded_substeps
               << ",\"max_forwarded_substeps\":" << observed.max_forwarded_substeps
               << ",\"last_delta_time\":" << observed.last_delta_time
               << ",\"constrained_updates\":" << observed.constrained_updates
               << ",\"material_intervention\":"
               << (observed.constrained_updates > 0 ? "true" : "false")
               << ",\"requested_substeps\":" << observed.requested_substeps
               << ",\"control_fresh\":"
               << (observed.control_fresh ? "true" : "false")
               << ",\"active_count_calls\":" << observed.active_count_calls
               << ",\"active_particles_observed\":"
               << (observed.active_count_calls > 0 ? "true" : "false")
               << ",\"active_particles_fresh\":"
               << (observed.active_particles_fresh ? "true" : "false")
               << ",\"active_particles\":" << observed.active_particles
               << ",\"min_active_particles\":" << observed.min_active_particles
               << ",\"max_active_particles\":" << observed.max_active_particles
               << ",\"solver_create_calls\":" << observed.create_calls
               << ",\"live_solvers\":" << observed.live_solvers
               << ",\"max_live_solvers\":" << observed.max_live_solvers
               << ",\"particle_capacity_available\":"
               << (observed.particle_capacity_available ? "true" : "false")
               << ",\"particle_capacity\":" << observed.particle_capacity
               << ",\"aggregate_particles_fresh\":"
               << (observed.aggregate_particles_fresh ? "true" : "false")
               << ",\"aggregate_active_particles\":"
               << observed.aggregate_active_particles
               << ",\"free_particles\":" << observed.free_particles
               << ",\"fence_set_calls\":" << observed.fence_set_calls
               << ",\"fence_wait_calls\":" << observed.fence_wait_calls
               << ",\"particle_upload_calls\":"
               << observed.particle_upload_calls
               << ",\"particle_download_calls\":"
               << observed.particle_download_calls
               << ",\"phase_upload_calls\":"
               << observed.phase_upload_calls
               << ",\"phase_download_calls\":"
               << observed.phase_download_calls
               << ",\"velocity_upload_calls\":"
               << observed.velocity_upload_calls
               << ",\"velocity_download_calls\":"
               << observed.velocity_download_calls
               << ",\"upload_elements\":" << observed.upload_elements
               << ",\"download_elements\":" << observed.download_elements
               << ",\"bounds_calls\":" << observed.bounds_calls
               << ",\"params_calls\":" << observed.params_calls
               << ",\"last_upload_elements\":"
               << observed.last_upload_elements
               << ",\"last_download_elements\":"
               << observed.last_download_elements
               << ",\"last_upload_memory\":" << observed.last_upload_memory
               << ",\"last_download_memory\":" << observed.last_download_memory
               << ",\"missing_original_calls\":"
               << observed.missing_original_calls
               << ",\"tracking_drop_calls\":"
               << observed.tracking_drop_calls
               << ",\"invalid_argument_calls\":"
               << observed.invalid_argument_calls
               << ",\"solver_tracking_quarantined\":"
               << (observed.solver_tracking_quarantined ? "true" : "false")
               << ",\"relay_healthy\":"
               << (observed.pass_through_healthy ? "true" : "false") << '}';
    const auto saved = platform::windows::atomic_replace_utf8(
        settings_path.parent_path() / L"flex-session-last.json", report.str());
    return saved.has_value();
}

void UiRuntime::observe_flex_process() {
    if (!game_process) return;
    const auto now_ns = monotonic_ns();
    adaptive_actuation.poll(now_ns);
    const auto flex_state = flex::read_observation(*game_process);
    if (!flex_state || !flex_state->fresh) return;
    last_flex_observation = *flex_state;
    last_flex_observation_calls = flex_state->update_calls;
    if (const auto receipt = telemetry_pipeline::confirmed_flex_readback(
            adaptive_actuation.current(
                optimizer::AdaptiveControlId::flex_solver_substeps),
            *flex_state, now_ns);
        receipt && adaptive_actuation.receive(*receipt) ==
            optimizer::AdaptiveReceiptResult::accepted) {
        events->append({
            0, diagnostics::Severity::info,
            "FLEX_ADAPTIVE_APPLIED",
            L"FleX solver readback confirmed APPLIED: requested=" +
                std::to_wstring(static_cast<int>(receipt->requested_value)) +
                L", effective=" + std::to_wstring(
                    static_cast<int>(*receipt->observed_value)) +
                L", process=" + std::to_wstring(receipt->generation.process_start_id) +
                L", map=" + std::to_wstring(receipt->generation.map) +
                L", settings=" + std::to_wstring(receipt->generation.settings),
            L"flex"});
    }
    auto status = model.status();
    const std::wstring flex_status = flex_state->aggregate_particles_fresh
        ? L"FleX solvers: " + std::to_wstring(flex_state->live_solvers) +
              L" | particles active/free/capacity: " +
              std::to_wstring(flex_state->aggregate_active_particles) + L"/" +
              std::to_wstring(flex_state->free_particles) + L"/" +
              std::to_wstring(flex_state->particle_capacity) +
              L" | transfers up/down: " +
              std::to_wstring(flex_state->particle_upload_calls +
                               flex_state->phase_upload_calls +
                               flex_state->velocity_upload_calls) + L"/" +
              std::to_wstring(flex_state->particle_download_calls +
                               flex_state->phase_download_calls +
                               flex_state->velocity_download_calls) +
              L" (read-only runtime source)"
        : flex_state->particle_capacity_available
            ? L"FleX solvers: " + std::to_wstring(flex_state->live_solvers) +
                  L" | particle capacity: " +
                  std::to_wstring(flex_state->particle_capacity) +
                  L" | active/free awaiting a fresh count"
        : flex_state->active_particles_fresh
            ? L"FleX active particles: " +
                  std::to_wstring(flex_state->active_particles) +
                  L" (read-only runtime source; aggregate unavailable)"
        : flex_state->active_count_calls > 0
            ? L"FleX active-particle value is stale"
            : L"FleX solver active; particle count not yet observed";
    const auto* action = adaptive_actuation.current(
        optimizer::AdaptiveControlId::flex_solver_substeps);
    const auto effective = adaptive_actuation.effective_value(
        optimizer::AdaptiveControlId::flex_solver_substeps);
    const std::optional<int> requested = action
        ? std::optional<int>{static_cast<int>(action->requested_value)}
        : std::nullopt;
    const std::optional<int> applied = effective
        ? std::optional<int>{static_cast<int>(*effective)} : std::nullopt;
    const auto action_status_view = action
        ? optimizer::adaptive_action_status_name(action->status)
        : std::string_view{"NONE"};
    const std::wstring action_status{
        action_status_view.begin(), action_status_view.end()};
    if (status.flex_telemetry != flex_status ||
        status.adaptive_flex_requested_substeps != requested ||
        status.adaptive_flex_effective_substeps != applied ||
        status.adaptive_flex_action_status != action_status) {
        status.flex_telemetry = flex_status;
        status.adaptive_flex_requested_substeps = requested;
        status.adaptive_flex_effective_substeps = applied;
        status.adaptive_flex_action_status = action_status;
        model.set_status(std::move(status));
        invalidate();
    }
    const auto report_tick = GetTickCount64();
    if (last_flex_report_tick == 0 || report_tick < last_flex_report_tick ||
        report_tick - last_flex_report_tick >= 2000) {
        if (save_flex_report(*flex_state)) last_flex_report_tick = report_tick;
    }
    if (!flex_observation_announced) {
        flex_observation_announced = true;
        events->append({0, diagnostics::Severity::info,
            "FLEX_OBSERVATION_ACTIVE",
            L"FleX solver observation is active independently of overlay telemetry",
            L"flex"});
    }
}

}  // namespace kf2::app
