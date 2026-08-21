#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::game {

app::runtime::DispatchResult select_install(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult open_install(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult open_config(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult open_logs(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult launch(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult offline_telemetry(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 6> kActions{{
    {app::runtime::ActionId::game_launch,
     &app::runtime::bind_no_payload<&launch>},
    {app::runtime::ActionId::game_offline_telemetry,
     &app::runtime::bind_no_payload<&offline_telemetry>},
    {app::runtime::ActionId::game_open_config,
     &app::runtime::bind_no_payload<&open_config>},
    {app::runtime::ActionId::game_open_install,
     &app::runtime::bind_no_payload<&open_install>},
    {app::runtime::ActionId::game_open_logs,
     &app::runtime::bind_no_payload<&open_logs>},
    {app::runtime::ActionId::game_select_install,
     &app::runtime::bind_no_payload<&select_install>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::game, "game", kActions};

}  // namespace kf2::features::game
