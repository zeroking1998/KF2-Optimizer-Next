#pragma once

#include <optional>
#include <utility>

#include "kf2/optimizer/adaptive_profile.hpp"
#include "kf2/ui/ui_model.hpp"

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

struct AdaptiveProfileEffect final {
    optimizer::Profile profile{optimizer::Profile::balanced};
};

struct TelemetryEffectBatch final {
    std::optional<FlexControlEffect> flex_control;
    std::optional<AdaptiveProfileEffect> adaptive_profile;
};

template <typename FlexApply, typename ProfileApply>
void apply_effects_in_order(const TelemetryEffectBatch& effects,
                            FlexApply&& apply_flex,
                            ProfileApply&& apply_profile) {
    if (effects.flex_control) {
        std::forward<FlexApply>(apply_flex)(*effects.flex_control);
    }
    if (effects.adaptive_profile) {
        std::forward<ProfileApply>(apply_profile)(*effects.adaptive_profile);
    }
}

void apply_flex_control_effect(app::UiRuntime& runtime,
                               const FlexControlEffect& effect);
void apply_adaptive_profile_effect(
    app::UiRuntime& runtime, const AdaptiveProfileEffect& effect,
    ui::UiStatus& status);

}  // namespace kf2::telemetry_pipeline
