#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "features/telemetry/telemetry_frame.hpp"
#include "kf2/flex/flex_adaptive_policy.hpp"
#include "kf2/flex/flex_observation.hpp"
#include "kf2/optimizer/adaptive_actuation.hpp"
#include "kf2/optimizer/adaptive_governor.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct FlexControlInput final {
    bool actuator_available{false};
    int target_fps{60};
    int quality_change_budget{1};
    std::optional<double> fps;
    std::optional<double> enemy_pressure;
    std::uint64_t now_ms{0};
};

struct FlexControlDecision final {
    int requested_substeps{0};
    bool constrained{false};
};

[[nodiscard]] inline bool flex_pressure_is_actionable(
    const optimizer::AdaptiveDecision& decision) noexcept {
    return decision.current_frame_pressure &&
        (decision.resources.primary == optimizer::ResourceKind::cpu ||
         decision.resources.primary == optimizer::ResourceKind::gpu ||
         decision.resources.primary == optimizer::ResourceKind::vram);
}

[[nodiscard]] inline std::optional<double> visible_enemy_pressure(
    int visible_living_zeds) noexcept {
    if (visible_living_zeds < 5) return std::nullopt;
    return std::clamp(
        (static_cast<double>(visible_living_zeds) - 4.0) / 76.0,
        0.0, 1.0);
}

[[nodiscard]] inline bool enemy_pressure_is_actionable(
    const std::optional<double>& pressure) noexcept {
    return pressure && std::isfinite(*pressure) && *pressure > 0.0;
}

[[nodiscard]] inline FlexControlDecision decide_flex_control(
    flex::AdaptivePolicy& policy, const FlexControlInput& input) noexcept {
    const auto decision = policy.evaluate(
        input.actuator_available,
        input.target_fps, input.fps, input.now_ms,
        input.quality_change_budget, input.enemy_pressure);
    return {decision.requested_substeps, decision.constrained};
}

[[nodiscard]] inline std::optional<optimizer::AdaptiveActionReceipt>
confirmed_flex_readback(
    const optimizer::AdaptiveActionRecord* pending,
    const flex::ObservationSnapshot& observed,
    std::uint64_t now_ns) noexcept {
    if (!pending ||
        pending->status != optimizer::AdaptiveActionStatus::pending ||
        pending->control != optimizer::AdaptiveControlId::flex_solver_substeps ||
        now_ns == 0 || !std::isfinite(pending->requested_value) ||
        pending->requested_value < 0.0 || pending->requested_value > 5.0 ||
        std::floor(pending->requested_value) != pending->requested_value ||
        !observed.fresh || !observed.control_fresh) {
        return std::nullopt;
    }
    const int requested = static_cast<int>(pending->requested_value);
    if (observed.requested_substeps != requested ||
        (requested != 0 && observed.last_forwarded_substeps != requested)) {
        return std::nullopt;
    }
    return optimizer::AdaptiveActionReceipt{
        pending->action_id,
        pending->control,
        optimizer::AdaptiveActionStatus::applied,
        pending->requested_value,
        static_cast<double>(observed.last_forwarded_substeps),
        pending->generation,
        now_ns,
        "flex_shared_memory_readback",
        {},
        true,
        pending->observed_value};
}

void observe_flex_source(app::UiRuntime& runtime);
void run_flex_control_stage(app::UiRuntime& runtime,
                            const TelemetryFrame& frame);

}  // namespace kf2::telemetry_pipeline
