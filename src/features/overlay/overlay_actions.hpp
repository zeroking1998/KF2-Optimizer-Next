#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::overlay {

app::runtime::DispatchResult position(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult scale_reset(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult show_cpu(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult show_fps(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult show_frame_time(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult show_gpu(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult show_memory(
    app::UiRuntime&, const app::runtime::NoPayload&);
app::runtime::DispatchResult toggle(
    app::UiRuntime&, const app::runtime::NoPayload&);

inline constexpr std::array<app::runtime::ActionImplementation, 8> kActions{{
    {app::runtime::ActionId::overlay_position,
     &app::runtime::bind_no_payload<&position>},
    {app::runtime::ActionId::overlay_scale_reset,
     &app::runtime::bind_no_payload<&scale_reset>},
    {app::runtime::ActionId::overlay_show_cpu,
     &app::runtime::bind_no_payload<&show_cpu>},
    {app::runtime::ActionId::overlay_show_fps,
     &app::runtime::bind_no_payload<&show_fps>},
    {app::runtime::ActionId::overlay_show_frame_time,
     &app::runtime::bind_no_payload<&show_frame_time>},
    {app::runtime::ActionId::overlay_show_gpu,
     &app::runtime::bind_no_payload<&show_gpu>},
    {app::runtime::ActionId::overlay_show_memory,
     &app::runtime::bind_no_payload<&show_memory>},
    {app::runtime::ActionId::overlay_toggle,
     &app::runtime::bind_no_payload<&toggle>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::overlay, "overlay", kActions};

}  // namespace kf2::features::overlay
