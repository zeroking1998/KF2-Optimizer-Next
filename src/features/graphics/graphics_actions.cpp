#include "features/graphics/graphics_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::graphics {
namespace {

app::runtime::DispatchResult cycle(app::UiRuntime& runtime,
                                   game::VideoOption option) {
    runtime.cycle_video_option(option);
    return app::runtime::DispatchResult::handled;
}

}  // namespace

#define KF2_CYCLE_HANDLER(name, option)                                      \
    app::runtime::DispatchResult name(                                       \
        app::UiRuntime& runtime, const app::runtime::NoPayload&) {            \
        return cycle(runtime, game::VideoOption::option);                     \
    }

KF2_CYCLE_HANDLER(display, display)
KF2_CYCLE_HANDLER(resolution, resolution)
KF2_CYCLE_HANDLER(overall_quality, overall_quality)
KF2_CYCLE_HANDLER(vsync, vsync)
KF2_CYCLE_HANDLER(variable_frame_rate, variable_frame_rate)
KF2_CYCLE_HANDLER(environment_detail, environment_detail)
KF2_CYCLE_HANDLER(character_detail, character_detail)
KF2_CYCLE_HANDLER(fx, fx_quality)
KF2_CYCLE_HANDLER(texture_resolution, texture_resolution)
KF2_CYCLE_HANDLER(texture_filtering, texture_filtering)
KF2_CYCLE_HANDLER(shadow_quality, shadow_quality)
KF2_CYCLE_HANDLER(realtime_reflections, realtime_reflections)
KF2_CYCLE_HANDLER(anti_aliasing, anti_aliasing)
KF2_CYCLE_HANDLER(bloom, bloom)
KF2_CYCLE_HANDLER(motion_blur, motion_blur)
KF2_CYCLE_HANDLER(ambient_occlusion, ambient_occlusion)
KF2_CYCLE_HANDLER(depth_of_field, depth_of_field)
KF2_CYCLE_HANDLER(volumetric_lighting, volumetric_lighting)
KF2_CYCLE_HANDLER(lens_flares, lens_flares)
KF2_CYCLE_HANDLER(light_shafts, light_shafts)
KF2_CYCLE_HANDLER(flex, nvidia_flex)

#undef KF2_CYCLE_HANDLER

app::runtime::DispatchResult apply(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto result = runtime.apply_video_settings();
    runtime.model.set_notice({
        result.has_value() ? ui::NoticeSeverity::info : ui::NoticeSeverity::warning,
        result.has_value() ? L"GRAPHICS_APPLIED" : L"GRAPHICS_APPLY_BLOCKED",
        result.has_value()
            ? L"KF2 video settings were applied and verified. A restore backup is available."
            : result.error().message,
        L""});
    runtime.invalidate();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult reset(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.reset_video_settings();
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::graphics
