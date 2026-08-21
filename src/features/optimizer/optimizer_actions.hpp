#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::optimizer {

app::runtime::DispatchResult preview(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult export_preview(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult import_preview(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult apply_preview(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 4> kActions{{
    {app::runtime::ActionId::optimizer_apply,
     &app::runtime::bind_no_payload<&apply_preview>},
    {app::runtime::ActionId::optimizer_export,
     &app::runtime::bind_no_payload<&export_preview>},
    {app::runtime::ActionId::optimizer_import,
     &app::runtime::bind_no_payload<&import_preview>},
    {app::runtime::ActionId::optimizer_preview,
     &app::runtime::bind_no_payload<&preview>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::optimizer, "optimizer", kActions};

}  // namespace kf2::features::optimizer
