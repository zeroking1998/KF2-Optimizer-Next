#include "features/advanced/advanced_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::advanced {
namespace {

app::runtime::DispatchResult cycle(
    app::UiRuntime& runtime, game::AdvancedOption option) {
    runtime.cycle_advanced_option(option);
    return app::runtime::DispatchResult::handled;
}

}  // namespace

#define KF2_ADVANCED_CYCLE(name, option)                                  \
    app::runtime::DispatchResult name(                                    \
        app::UiRuntime& runtime, const app::runtime::NoPayload&) {         \
        return cycle(runtime, game::AdvancedOption::option);              \
    }

KF2_ADVANCED_CYCLE(one_frame_thread_lag, one_frame_thread_lag)
KF2_ADVANCED_CYCLE(per_frame_sleep, per_frame_sleep)
KF2_ADVANCED_CYCLE(per_frame_yield, per_frame_yield)
KF2_ADVANCED_CYCLE(background_level_streaming, background_level_streaming)
KF2_ADVANCED_CYCLE(texture_streaming, texture_streaming)
KF2_ADVANCED_CYCLE(priority_streaming, priority_streaming)
KF2_ADVANCED_CYCLE(dynamic_streaming, dynamic_streaming)
KF2_ADVANCED_CYCLE(hardware_shadow_filtering, hardware_shadow_filtering)
KF2_ADVANCED_CYCLE(downsampled_translucency, downsampled_translucency)
KF2_ADVANCED_CYCLE(floating_point_render_targets, floating_point_render_targets)
KF2_ADVANCED_CYCLE(max_multisamples, max_multisamples)
KF2_ADVANCED_CYCLE(gore_level, gore_level)

#undef KF2_ADVANCED_CYCLE

app::runtime::DispatchResult apply(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto result = runtime.apply_advanced_settings();
    runtime.model.set_notice({
        result.has_value() ? ui::NoticeSeverity::info
                           : ui::NoticeSeverity::warning,
        result.has_value() ? L"ADVANCED_APPLIED" : L"ADVANCED_APPLY_BLOCKED",
        result.has_value()
            ? L"Advanced KF2 settings were applied and verified. A restore backup is available."
            : result.error().message,
        L""});
    runtime.invalidate();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult reset(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.reset_advanced_settings();
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::advanced
