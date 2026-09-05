#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::settings {

app::runtime::DispatchResult updates_automatic(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_check(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_install(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_later(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_ignore(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_toggle(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 6> kActions{{
    {app::runtime::ActionId::settings_updates_automatic,
     &app::runtime::bind_no_payload<&updates_automatic>},
    {app::runtime::ActionId::settings_updates_check,
     &app::runtime::bind_no_payload<&updates_check>},
    {app::runtime::ActionId::settings_updates_install,
     &app::runtime::bind_no_payload<&updates_install>},
    {app::runtime::ActionId::settings_updates_later,
     &app::runtime::bind_no_payload<&updates_later>},
    {app::runtime::ActionId::settings_updates_ignore,
     &app::runtime::bind_no_payload<&updates_ignore>},
    {app::runtime::ActionId::settings_adaptive_toggle,
     &app::runtime::bind_no_payload<&adaptive_toggle>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::settings, "settings", kActions};

}  // namespace kf2::features::settings
