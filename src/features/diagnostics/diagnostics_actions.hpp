#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::diagnostics {

app::runtime::DispatchResult export_support(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult flex_restore(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult full_check(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult open_data(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult open_log(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult repair_package(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult auto_repair_package(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult toggle_corpse_markers(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult toggle_zed_markers(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 9> kActions{{
    {app::runtime::ActionId::diagnostics_export_support,
     &app::runtime::bind_no_payload<&export_support>},
    {app::runtime::ActionId::diagnostics_flex_restore,
     &app::runtime::bind_no_payload<&flex_restore>},
    {app::runtime::ActionId::diagnostics_full_check,
     &app::runtime::bind_no_payload<&full_check>},
    {app::runtime::ActionId::diagnostics_open_data,
     &app::runtime::bind_no_payload<&open_data>},
    {app::runtime::ActionId::diagnostics_open_log,
     &app::runtime::bind_no_payload<&open_log>},
    {app::runtime::ActionId::diagnostics_repair_package,
     &app::runtime::bind_no_payload<&repair_package>},
    {app::runtime::ActionId::diagnostics_auto_repair,
     &app::runtime::bind_no_payload<&auto_repair_package>},
    {app::runtime::ActionId::debug_corpse_markers,
     &app::runtime::bind_no_payload<&toggle_corpse_markers>},
    {app::runtime::ActionId::debug_zed_markers,
     &app::runtime::bind_no_payload<&toggle_zed_markers>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::diagnostics, "diagnostics", kActions};

}  // namespace kf2::features::diagnostics
