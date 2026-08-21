#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::diagnostics {

app::runtime::DispatchResult refresh(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult benchmark_baseline(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult benchmark_compare(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult clear(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult export_report(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult export_inventory(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult export_support(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult flex_audit(
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

inline constexpr std::array<app::runtime::ActionImplementation, 13> kActions{{
    {app::runtime::ActionId::refresh_status,
     &app::runtime::bind_no_payload<&refresh>},
    {app::runtime::ActionId::diagnostics_benchmark_baseline,
     &app::runtime::bind_no_payload<&benchmark_baseline>},
    {app::runtime::ActionId::diagnostics_benchmark_compare,
     &app::runtime::bind_no_payload<&benchmark_compare>},
    {app::runtime::ActionId::diagnostics_clear,
     &app::runtime::bind_no_payload<&clear>},
    {app::runtime::ActionId::diagnostics_export,
     &app::runtime::bind_no_payload<&export_report>},
    {app::runtime::ActionId::diagnostics_export_inventory,
     &app::runtime::bind_no_payload<&export_inventory>},
    {app::runtime::ActionId::diagnostics_export_support,
     &app::runtime::bind_no_payload<&export_support>},
    {app::runtime::ActionId::diagnostics_flex_audit,
     &app::runtime::bind_no_payload<&flex_audit>},
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
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::diagnostics, "diagnostics", kActions};

}  // namespace kf2::features::diagnostics
