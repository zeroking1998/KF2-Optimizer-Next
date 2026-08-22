#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::settings {

app::runtime::DispatchResult advanced_toggle(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_aggressiveness(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_budget(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_calibration(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_emergency(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_headroom_down(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_headroom_up(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_locks(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_logging(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_maximum_down(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_maximum_up(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_minimum_down(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_minimum_up(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_recovery(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult adaptive_shadow(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult animations(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult target_down(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult target_up(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult corpses_down(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult corpses_up(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_automatic(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_check(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_install(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult updates_later(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 24> kActions{{
    {app::runtime::ActionId::settings_advanced_toggle,
     &app::runtime::bind_no_payload<&advanced_toggle>},
    {app::runtime::ActionId::settings_adaptive_aggressiveness,
     &app::runtime::bind_no_payload<&adaptive_aggressiveness>},
    {app::runtime::ActionId::settings_adaptive_budget,
     &app::runtime::bind_no_payload<&adaptive_budget>},
    {app::runtime::ActionId::settings_adaptive_calibration,
     &app::runtime::bind_no_payload<&adaptive_calibration>},
    {app::runtime::ActionId::settings_adaptive_emergency,
     &app::runtime::bind_no_payload<&adaptive_emergency>},
    {app::runtime::ActionId::settings_adaptive_headroom_down,
     &app::runtime::bind_no_payload<&adaptive_headroom_down>},
    {app::runtime::ActionId::settings_adaptive_headroom_up,
     &app::runtime::bind_no_payload<&adaptive_headroom_up>},
    {app::runtime::ActionId::settings_adaptive_locks,
     &app::runtime::bind_no_payload<&adaptive_locks>},
    {app::runtime::ActionId::settings_adaptive_logging,
     &app::runtime::bind_no_payload<&adaptive_logging>},
    {app::runtime::ActionId::settings_adaptive_maximum_down,
     &app::runtime::bind_no_payload<&adaptive_maximum_down>},
    {app::runtime::ActionId::settings_adaptive_maximum_up,
     &app::runtime::bind_no_payload<&adaptive_maximum_up>},
    {app::runtime::ActionId::settings_adaptive_minimum_down,
     &app::runtime::bind_no_payload<&adaptive_minimum_down>},
    {app::runtime::ActionId::settings_adaptive_minimum_up,
     &app::runtime::bind_no_payload<&adaptive_minimum_up>},
    {app::runtime::ActionId::settings_adaptive_recovery,
     &app::runtime::bind_no_payload<&adaptive_recovery>},
    {app::runtime::ActionId::settings_adaptive_shadow,
     &app::runtime::bind_no_payload<&adaptive_shadow>},
    {app::runtime::ActionId::settings_animations,
     &app::runtime::bind_no_payload<&animations>},
    {app::runtime::ActionId::settings_target_down,
     &app::runtime::bind_no_payload<&target_down>},
    {app::runtime::ActionId::settings_target_up,
     &app::runtime::bind_no_payload<&target_up>},
    {app::runtime::ActionId::settings_corpses_down,
     &app::runtime::bind_no_payload<&corpses_down>},
    {app::runtime::ActionId::settings_corpses_up,
     &app::runtime::bind_no_payload<&corpses_up>},
    {app::runtime::ActionId::settings_updates_automatic,
     &app::runtime::bind_no_payload<&updates_automatic>},
    {app::runtime::ActionId::settings_updates_check,
     &app::runtime::bind_no_payload<&updates_check>},
    {app::runtime::ActionId::settings_updates_install,
     &app::runtime::bind_no_payload<&updates_install>},
    {app::runtime::ActionId::settings_updates_later,
     &app::runtime::bind_no_payload<&updates_later>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::settings, "settings", kActions};

}  // namespace kf2::features::settings
