#include "features/telemetry/telemetry_flex_stage.hpp"

#include <array>
#include <format>
#include <string_view>

#include "app/application_runtime.hpp"
#include "features/telemetry/telemetry_effect_stage.hpp"

namespace kf2::telemetry_pipeline {

void observe_flex_source(app::UiRuntime& runtime) {
    runtime.observe_flex_process();
}

void run_flex_control_stage(app::UiRuntime& runtime,
                            const TelemetryFrame& frame) {
    const auto capability = frame.offline_gameplay && frame.flex &&
            frame.flex->fresh && frame.flex->pass_through_healthy &&
            (!frame.flex->diagnostics_enabled ||
             !frame.flex->solver_tracking_quarantined)
        ? optimizer::AdaptiveCapabilityState::available
        : optimizer::AdaptiveCapabilityState::unavailable;
    const optimizer::AdaptiveGeneration generation{
        frame.identity.process_start_id,
        frame.identity.process_start_id,
        runtime.adaptive_map_generation,
        runtime.adaptive_settings_generation,
        frame.offline_gameplay ? 1ULL : 0ULL};
    if (!(runtime.adaptive_actuation.generation() == generation)) {
        runtime.adaptive_actuation.rebase(generation, frame.observed_at_ns);
    }
    if (runtime.adaptive_flex_capability &&
        *runtime.adaptive_flex_capability != capability) {
        constexpr std::array flex_controls{
            optimizer::AdaptiveControlId::flex_solver_substeps,
            optimizer::AdaptiveControlId::flex_particle_budget,
            optimizer::AdaptiveControlId::flex_particle_spawn,
            optimizer::AdaptiveControlId::flex_particle_lifetime,
            optimizer::AdaptiveControlId::flex_fluid_particles,
            optimizer::AdaptiveControlId::flex_nonfluid_particles};
        for (const auto control : flex_controls) {
            runtime.adaptive_actuation.invalidate_control(control);
        }
    }
    runtime.adaptive_flex_capability = capability;
    const bool observed_solver_ready = capability ==
            optimizer::AdaptiveCapabilityState::available &&
        frame.flex && frame.flex->last_forwarded_substeps >= 1 &&
        frame.flex->last_forwarded_substeps <= 5;
    const auto decision = decide_flex_control(observed_solver_ready);
    const std::wstring_view capability_label = observed_solver_ready
        ? L"AVAILABLE" : L"UNAVAILABLE";
    if (runtime.model.status().flex_capability != capability_label) {
        auto status = runtime.model.status();
        status.flex_capability = capability_label;
        runtime.model.set_status(std::move(status));
        runtime.invalidate();
    }
    apply_flex_control_effect(
        runtime, {decision.requested_substeps, decision.constrained,
                  capability});
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {
namespace {

bool same_particle_presentation(const flex::ObservationSnapshot& previous,
                                const flex::ObservationSnapshot& current) noexcept {
    if (previous.aggregate_particles_fresh || current.aggregate_particles_fresh) {
        return previous.aggregate_particles_fresh && current.aggregate_particles_fresh &&
            previous.live_solvers == current.live_solvers &&
            previous.aggregate_active_particles == current.aggregate_active_particles &&
            previous.free_particles == current.free_particles &&
            previous.particle_capacity == current.particle_capacity &&
            previous.particle_upload_calls == current.particle_upload_calls &&
            previous.phase_upload_calls == current.phase_upload_calls &&
            previous.velocity_upload_calls == current.velocity_upload_calls &&
            previous.particle_download_calls == current.particle_download_calls &&
            previous.phase_download_calls == current.phase_download_calls &&
            previous.velocity_download_calls == current.velocity_download_calls;
    }
    if (previous.particle_capacity_available || current.particle_capacity_available) {
        return previous.particle_capacity_available && current.particle_capacity_available &&
            previous.live_solvers == current.live_solvers &&
            previous.particle_capacity == current.particle_capacity;
    }
    return previous.active_particles_fresh && current.active_particles_fresh &&
        previous.active_particles == current.active_particles;
}

}  // namespace

bool UiRuntime::save_flex_report(const flex::ObservationSnapshot& observed) {
    if (observed.update_calls == 0 || !observed.diagnostics_enabled)
        return false;
    std::ostringstream report;
    report << "{\"version\":8,\"configured_mode\":\"fixed_minimum\""
               << ",\"fixed_substeps\":1"
               << ",\"diagnostics_enabled\":true"
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
    const auto ticket = file_writer.submit(
        settings_path.parent_path() / L"flex-session-last.json", report.str());
    if (ticket == 0) return false;
    return file_writer.wait(ticket, std::chrono::seconds{5});
}

void UiRuntime::observe_flex_process() {
    if (!game_process) return;
    const auto now_ns = monotonic_ns();
    adaptive_actuation.poll(now_ns);
    const auto flex_state = flex_observation_reader.read(*game_process);
    if (!flex_state || !flex_state->fresh) return;
    const bool reuse_particle_text =
        flex_particle_text_current && last_flex_observation &&
        same_particle_presentation(*last_flex_observation, *flex_state);
    // Keep the live observation current even if presentation throws. Only a
    // completed publication permits borrowing its text on the next observation.
    flex_particle_text_current = false;
    last_flex_observation = *flex_state;
    if (const auto receipt = telemetry_pipeline::confirmed_flex_readback(
            adaptive_actuation.current(
                optimizer::AdaptiveControlId::flex_solver_substeps),
            *flex_state, now_ns);
        receipt && adaptive_actuation.receive(*receipt) ==
            optimizer::AdaptiveReceiptResult::accepted) {
        events->append({
            0, diagnostics::Severity::info,
            "FLEX_MINIMUM_APPLIED",
            L"Fixed FleX minimum readback confirmed APPLIED: requested=" +
                std::to_wstring(static_cast<int>(receipt->requested_value)) +
                L", effective=" + std::to_wstring(
                    static_cast<int>(*receipt->observed_value)) +
                L", process=" + std::to_wstring(receipt->generation.process_start_id) +
                L", map=" + std::to_wstring(receipt->generation.map) +
                L", settings=" + std::to_wstring(receipt->generation.settings),
            L"flex"});
    }
    const auto& current = model.status();
    const std::wstring flex_values = reuse_particle_text ? std::wstring{}
        : flex_state->aggregate_particles_fresh
        ? std::format(L"FleX solvers: {} | particles active/free/capacity: {}/{}/{}"
                      L" | transfers up/down: {}/{} (read-only runtime source)",
              flex_state->live_solvers, flex_state->aggregate_active_particles,
              flex_state->free_particles, flex_state->particle_capacity,
              flex_state->particle_upload_calls + flex_state->phase_upload_calls +
                  flex_state->velocity_upload_calls,
              flex_state->particle_download_calls + flex_state->phase_download_calls +
                  flex_state->velocity_download_calls)
        : flex_state->particle_capacity_available
            ? std::format(L"FleX solvers: {} | particle capacity: {}"
                          L" | active/free awaiting a fresh count",
                  flex_state->live_solvers, flex_state->particle_capacity)
        : flex_state->active_particles_fresh
            ? std::format(L"FleX active particles: {}"
                          L" (read-only runtime source; aggregate unavailable)",
                  flex_state->active_particles)
        : std::wstring{};
    const std::wstring_view flex_status = reuse_particle_text
        ? std::wstring_view{current.flex_telemetry}
        : !flex_values.empty()
        ? std::wstring_view{flex_values}
        : flex_state->active_count_calls > 0
            ? L"FleX active-particle value is stale"
            : flex_state->diagnostics_enabled
                ? L"FleX diagnostics are active; particle count not yet observed"
                : L"FleX fixed one-substep relay active; detailed diagnostics off";
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
    const std::wstring substep_values = flex_state->diagnostics_enabled
        ? std::format(L"input min/max {}/{}  •  forwarded min/max {}/{}"
                      L"  •  latest {} → {}",
              flex_state->min_substeps, flex_state->max_substeps,
              flex_state->min_forwarded_substeps, flex_state->max_forwarded_substeps,
              flex_state->last_substeps, flex_state->last_forwarded_substeps)
        : std::wstring{};
    const std::wstring_view substep_status = flex_state->diagnostics_enabled
        ? std::wstring_view{substep_values}
        : L"Detailed substep counters are off; fixed one-substep limit is active";
    const std::wstring readback_values = flex_state->diagnostics_enabled
        ? std::format(L"shared memory {}  •  updates {}/{}  •  constrained {}"
                      L"  •  reports and extra logs on",
              flex_state->pass_through_healthy ? L"healthy" : L"unhealthy",
              flex_state->successful_updates, flex_state->update_calls,
              flex_state->constrained_updates)
        : std::wstring{};
    const std::wstring_view readback_status = flex_state->diagnostics_enabled
        ? std::wstring_view{readback_values}
        : L"Minimal safety readback active; reports and extra logs are off";
    if (current.flex_telemetry != flex_status ||
        current.flex_requested_substeps != requested ||
        current.flex_effective_substeps != applied ||
        current.flex_action_status != action_status ||
        current.flex_substep_diagnostics != substep_status ||
        current.flex_readback_diagnostics != readback_status) {
        model.set_flex_observation_status(std::wstring{flex_status}, requested,
            applied, action_status, std::wstring{substep_status},
            std::wstring{readback_status});
        invalidate();
    }
    flex_particle_text_current = true;
    if (!flex_observation_announced) {
        flex_observation_announced = true;
        events->append({0, diagnostics::Severity::info,
            "FLEX_OBSERVATION_ACTIVE",
            L"FleX solver observation is active independently of overlay telemetry",
            L"flex"});
    }
}

}  // namespace kf2::app
