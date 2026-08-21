#include "features/navigation/navigation_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::navigation {
namespace {

app::runtime::DispatchResult focus(app::UiRuntime& runtime,
                                   ui::Destination destination) {
    runtime.controller.focus_target(destination, std::nullopt);
    runtime.invalidate();
    return app::runtime::DispatchResult::handled;
}

}  // namespace

app::runtime::DispatchResult navigate_diagnostics(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return focus(runtime, ui::Destination::diagnostics);
}

app::runtime::DispatchResult navigate_settings(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return focus(runtime, ui::Destination::settings);
}

app::runtime::DispatchResult navigate_overlay(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return focus(runtime, ui::Destination::overlay);
}

app::runtime::DispatchResult navigate_optimizer(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return focus(runtime, ui::Destination::optimizer);
}

}  // namespace kf2::features::navigation
