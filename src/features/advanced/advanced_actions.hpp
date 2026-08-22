#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::advanced {

#define KF2_ADVANCED_HANDLER(name) \
    app::runtime::DispatchResult name(app::UiRuntime&, const app::runtime::NoPayload&)

KF2_ADVANCED_HANDLER(one_frame_thread_lag);
KF2_ADVANCED_HANDLER(per_frame_sleep);
KF2_ADVANCED_HANDLER(per_frame_yield);
KF2_ADVANCED_HANDLER(background_level_streaming);
KF2_ADVANCED_HANDLER(texture_streaming);
KF2_ADVANCED_HANDLER(priority_streaming);
KF2_ADVANCED_HANDLER(dynamic_streaming);
KF2_ADVANCED_HANDLER(temporal_aa);
KF2_ADVANCED_HANDLER(hardware_shadow_filtering);
KF2_ADVANCED_HANDLER(downsampled_translucency);
KF2_ADVANCED_HANDLER(floating_point_render_targets);
KF2_ADVANCED_HANDLER(max_multisamples);
KF2_ADVANCED_HANDLER(gore_level);
KF2_ADVANCED_HANDLER(apply);
KF2_ADVANCED_HANDLER(reset);

#undef KF2_ADVANCED_HANDLER

inline constexpr std::array<app::runtime::ActionImplementation, 15> kActions{{
    {app::runtime::ActionId::advanced_one_frame_thread_lag, &app::runtime::bind_no_payload<&one_frame_thread_lag>},
    {app::runtime::ActionId::advanced_per_frame_sleep, &app::runtime::bind_no_payload<&per_frame_sleep>},
    {app::runtime::ActionId::advanced_per_frame_yield, &app::runtime::bind_no_payload<&per_frame_yield>},
    {app::runtime::ActionId::advanced_background_level_streaming, &app::runtime::bind_no_payload<&background_level_streaming>},
    {app::runtime::ActionId::advanced_texture_streaming, &app::runtime::bind_no_payload<&texture_streaming>},
    {app::runtime::ActionId::advanced_priority_streaming, &app::runtime::bind_no_payload<&priority_streaming>},
    {app::runtime::ActionId::advanced_dynamic_streaming, &app::runtime::bind_no_payload<&dynamic_streaming>},
    {app::runtime::ActionId::advanced_temporal_aa, &app::runtime::bind_no_payload<&temporal_aa>},
    {app::runtime::ActionId::advanced_hardware_shadow_filtering, &app::runtime::bind_no_payload<&hardware_shadow_filtering>},
    {app::runtime::ActionId::advanced_downsampled_translucency, &app::runtime::bind_no_payload<&downsampled_translucency>},
    {app::runtime::ActionId::advanced_floating_point_render_targets, &app::runtime::bind_no_payload<&floating_point_render_targets>},
    {app::runtime::ActionId::advanced_max_multisamples, &app::runtime::bind_no_payload<&max_multisamples>},
    {app::runtime::ActionId::advanced_gore_level, &app::runtime::bind_no_payload<&gore_level>},
    {app::runtime::ActionId::advanced_apply, &app::runtime::bind_no_payload<&apply>},
    {app::runtime::ActionId::advanced_reset, &app::runtime::bind_no_payload<&reset>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::advanced, "advanced", kActions};

}  // namespace kf2::features::advanced
