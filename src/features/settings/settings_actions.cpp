#include "features/settings/settings_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::settings {

app::runtime::DispatchResult adaptive_toggle(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.toggle_adaptive_optimization();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_automatic(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.toggle_automatic_update_checks();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_check(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.start_update_check(update::CheckTrigger::manual);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_install(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.start_update_install();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_later(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.dismiss_update();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult updates_ignore(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.ignore_update();
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::settings
