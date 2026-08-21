#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::navigation {

app::runtime::DispatchResult navigate_diagnostics(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult navigate_settings(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult navigate_overlay(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult navigate_optimizer(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 4> kActions{{
    {app::runtime::ActionId::navigate_diagnostics,
     &app::runtime::bind_no_payload<&navigate_diagnostics>},
    {app::runtime::ActionId::navigate_settings,
     &app::runtime::bind_no_payload<&navigate_settings>},
    {app::runtime::ActionId::navigate_overlay,
     &app::runtime::bind_no_payload<&navigate_overlay>},
    {app::runtime::ActionId::navigate_optimizer,
     &app::runtime::bind_no_payload<&navigate_optimizer>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::navigation, "navigation", kActions};

}  // namespace kf2::features::navigation
