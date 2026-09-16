#pragma once

#include "kf2/optimizer/adaptive_profile.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct FlexControlEffect final {
    int requested_substeps{0};
    bool constrained{false};
    optimizer::AdaptiveCapabilityState capability{
        optimizer::AdaptiveCapabilityState::unavailable};
};

void apply_flex_control_effect(app::UiRuntime& runtime,
                               const FlexControlEffect& effect);

}  // namespace kf2::telemetry_pipeline
