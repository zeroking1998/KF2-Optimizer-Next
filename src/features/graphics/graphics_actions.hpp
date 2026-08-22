#pragma once

#include <array>

#include "app/runtime/feature_registry.hpp"

namespace kf2::features::graphics {

#define KF2_GRAPHICS_HANDLER(name) \
    app::runtime::DispatchResult name(app::UiRuntime&, const app::runtime::NoPayload&)

KF2_GRAPHICS_HANDLER(display);
KF2_GRAPHICS_HANDLER(resolution);
KF2_GRAPHICS_HANDLER(overall_quality);
KF2_GRAPHICS_HANDLER(vsync);
KF2_GRAPHICS_HANDLER(variable_frame_rate);
KF2_GRAPHICS_HANDLER(environment_detail);
KF2_GRAPHICS_HANDLER(character_detail);
KF2_GRAPHICS_HANDLER(fx);
KF2_GRAPHICS_HANDLER(texture_resolution);
KF2_GRAPHICS_HANDLER(texture_filtering);
KF2_GRAPHICS_HANDLER(shadow_quality);
KF2_GRAPHICS_HANDLER(realtime_reflections);
KF2_GRAPHICS_HANDLER(anti_aliasing);
KF2_GRAPHICS_HANDLER(bloom);
KF2_GRAPHICS_HANDLER(motion_blur);
KF2_GRAPHICS_HANDLER(ambient_occlusion);
KF2_GRAPHICS_HANDLER(depth_of_field);
KF2_GRAPHICS_HANDLER(volumetric_lighting);
KF2_GRAPHICS_HANDLER(lens_flares);
KF2_GRAPHICS_HANDLER(light_shafts);
KF2_GRAPHICS_HANDLER(flex);
KF2_GRAPHICS_HANDLER(apply);
KF2_GRAPHICS_HANDLER(reset);

#undef KF2_GRAPHICS_HANDLER

inline constexpr std::array<app::runtime::ActionImplementation, 23> kActions{{
    {app::runtime::ActionId::graphics_display, &app::runtime::bind_no_payload<&display>},
    {app::runtime::ActionId::graphics_resolution, &app::runtime::bind_no_payload<&resolution>},
    {app::runtime::ActionId::graphics_overall_quality, &app::runtime::bind_no_payload<&overall_quality>},
    {app::runtime::ActionId::graphics_vsync, &app::runtime::bind_no_payload<&vsync>},
    {app::runtime::ActionId::graphics_variable_frame_rate, &app::runtime::bind_no_payload<&variable_frame_rate>},
    {app::runtime::ActionId::graphics_environment_detail, &app::runtime::bind_no_payload<&environment_detail>},
    {app::runtime::ActionId::graphics_character_detail, &app::runtime::bind_no_payload<&character_detail>},
    {app::runtime::ActionId::graphics_fx, &app::runtime::bind_no_payload<&fx>},
    {app::runtime::ActionId::graphics_texture_resolution, &app::runtime::bind_no_payload<&texture_resolution>},
    {app::runtime::ActionId::graphics_texture_filtering, &app::runtime::bind_no_payload<&texture_filtering>},
    {app::runtime::ActionId::graphics_shadow_quality, &app::runtime::bind_no_payload<&shadow_quality>},
    {app::runtime::ActionId::graphics_realtime_reflections, &app::runtime::bind_no_payload<&realtime_reflections>},
    {app::runtime::ActionId::graphics_anti_aliasing, &app::runtime::bind_no_payload<&anti_aliasing>},
    {app::runtime::ActionId::graphics_bloom, &app::runtime::bind_no_payload<&bloom>},
    {app::runtime::ActionId::graphics_motion_blur, &app::runtime::bind_no_payload<&motion_blur>},
    {app::runtime::ActionId::graphics_ambient_occlusion, &app::runtime::bind_no_payload<&ambient_occlusion>},
    {app::runtime::ActionId::graphics_depth_of_field, &app::runtime::bind_no_payload<&depth_of_field>},
    {app::runtime::ActionId::graphics_volumetric_lighting, &app::runtime::bind_no_payload<&volumetric_lighting>},
    {app::runtime::ActionId::graphics_lens_flares, &app::runtime::bind_no_payload<&lens_flares>},
    {app::runtime::ActionId::graphics_light_shafts, &app::runtime::bind_no_payload<&light_shafts>},
    {app::runtime::ActionId::graphics_flex, &app::runtime::bind_no_payload<&flex>},
    {app::runtime::ActionId::graphics_apply, &app::runtime::bind_no_payload<&apply>},
    {app::runtime::ActionId::graphics_reset, &app::runtime::bind_no_payload<&reset>},
}};

inline constexpr app::runtime::FeatureDefinition kFeature{
    app::runtime::FeatureId::graphics, "graphics", kActions};

}  // namespace kf2::features::graphics
