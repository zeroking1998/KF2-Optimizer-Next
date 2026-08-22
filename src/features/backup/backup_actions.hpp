#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::backup {

app::runtime::DispatchResult create(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult restore(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 2> kActions{{
    {app::runtime::ActionId::optimizer_backup,
     &app::runtime::bind_no_payload<&create>},
    {app::runtime::ActionId::optimizer_restore,
     &app::runtime::bind_no_payload<&restore>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::backup, "backup", kActions};

}  // namespace kf2::features::backup
