#pragma once

#include <cstdint>
#include <optional>

#include "features/telemetry/telemetry_frame.hpp"
#include "kf2/flex/flex_observation.hpp"
#include "kf2/optimizer/adaptive_actuation.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct FlexControlDecision final {
    int requested_substeps{0};
    bool constrained{false};
};

[[nodiscard]] inline FlexControlDecision decide_flex_control(
    bool actuator_available) noexcept {
    return actuator_available ? FlexControlDecision{1, true}
                              : FlexControlDecision{};
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
