#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "kf2/ui/shell_controller.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::platform::windows;
    using namespace kf2::ui;

    UiModel model;
    int invalidations = 0;
    int repaints = 0;
    int paints = 0;
    int ticks = 0;
    int overlay_toggles = 0;
    int resumes = 0;
    std::string last_action;
    std::string last_slider;
    int last_slider_value = -1;
    ShellController controller{
        model,
        {.invalidate = [&] { ++invalidations; },
         .repaint = [&] { ++repaints; },
         .paint = [&](const ShellLayoutResult&) { ++paints; },
         .tick = [&] { ++ticks; },
         .system_resume = [&] { ++resumes; },
         .toggle_overlay = [&] { ++overlay_toggles; },
         .activate_action = [&](std::string_view action) {
             last_action.assign(action);
         },
         .set_slider_value = [&](std::string_view id, int value) {
             last_slider.assign(id);
             last_slider_value = value;
         }}};

    controller.on_resize({1440, 900});
    CHECK(controller.layout().root.width == 1440);
    controller.on_paint();
    CHECK(paints == 1);

    controller.on_key({WindowKey::end});
    CHECK(model.focused_destination() == Destination::diagnostics);
    controller.on_key({WindowKey::enter});
    CHECK(model.selected() == Destination::diagnostics);
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "diagnostics-refresh");
    controller.on_key({WindowKey::shift_tab});
    CHECK(!model.focused_action());
    controller.on_key({WindowKey::home});
    CHECK(model.focused_destination() == Destination::dashboard);
    controller.on_key({WindowKey::right});
    CHECK(model.focused_destination() == Destination::game);
    controller.on_key({WindowKey::left});
    CHECK(model.focused_destination() == Destination::dashboard);

    const auto optimizer_nav = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.destination == Destination::optimizer;
        });
    CHECK(optimizer_nav != controller.layout().nodes.end());
    controller.on_pointer({PointerKind::activate,
        {optimizer_nav->bounds.x + 4, optimizer_nav->bounds.y + 4}, 0});
    CHECK(model.selected() == Destination::optimizer);
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "optimizer-preview");
    controller.on_key({WindowKey::enter});
    CHECK(last_action == "optimizer-preview");
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "optimizer-import");
    controller.on_key({WindowKey::shift_tab});
    CHECK(model.focused_action() == "optimizer-preview");

    const auto preview = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.action_id == "optimizer-preview";
        });
    CHECK(preview != controller.layout().nodes.end());
    controller.on_pointer({PointerKind::move,
        {preview->bounds.x + 4, preview->bounds.y + 4}, 0});
    CHECK(std::any_of(controller.layout().nodes.begin(),
                      controller.layout().nodes.end(),
                      [](const SemanticNode& item) {
                          return item.role == SemanticRole::tooltip;
                      }));
    controller.on_pointer({PointerKind::leave, {}, 0});
    CHECK(std::none_of(controller.layout().nodes.begin(),
                       controller.layout().nodes.end(),
                       [](const SemanticNode& item) {
                           return item.role == SemanticRole::tooltip;
                       }));

    auto adaptive_status = model.status();
    adaptive_status.mode = L"Adaptive / Automatic";
    model.set_status(adaptive_status);
    controller.focus_target(Destination::settings, "settings-target-slider");
    CHECK(model.selected() == Destination::settings);
    controller.on_key({WindowKey::right});
    CHECK(last_slider == "settings-target-slider");
    CHECK(last_slider_value == 61);

    const auto target = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-target-slider";
        });
    CHECK(target != controller.layout().nodes.end());
    const float drag_y = target->bounds.y + target->bounds.height - 25.0F;
    controller.on_pointer({PointerKind::press,
        {target->bounds.x + 30.0F, drag_y}, 0});
    controller.on_pointer({PointerKind::move,
        {target->bounds.x + target->bounds.width - 30.0F, drag_y}, 0});
    CHECK(repaints > 0);
    controller.on_pointer({PointerKind::release,
        {target->bounds.x + target->bounds.width - 30.0F, drag_y}, 0});
    CHECK(last_slider == "settings-target-slider");
    CHECK(last_slider_value >= 235);
    CHECK(last_slider_value <= 240);

    controller.focus_target(Destination::settings, "settings-advanced-toggle");
    controller.on_key({WindowKey::enter});
    CHECK(last_action == "settings-advanced-toggle");

    controller.focus_target(Destination::dashboard, "dashboard-settings");
    controller.on_key({WindowKey::enter});
    CHECK(last_action == "dashboard-settings");

    controller.focus_target(Destination::optimizer);
    controller.on_resize({800, 520});
    CHECK(model.scroll_extent() > 0);
    controller.on_pointer({PointerKind::wheel, {400, 400}, -120});
    CHECK(model.scroll_offset() > 0);
    controller.on_key({WindowKey::page_up});
    CHECK(model.scroll_offset() == 0);

    controller.on_dpi_changed({192, {800, 520}});
    CHECK(controller.dpi() == 192);
    controller.on_theme_changed({true, true, false});
    CHECK(!controller.theme().animations_enabled);
    CHECK(invalidations >= 8);
    controller.on_timer();
    CHECK(ticks == 1);
    controller.on_system_resume();
    CHECK(resumes == 1);
    controller.on_key({WindowKey::f10});
    CHECK(overlay_toggles == 1);
    CHECK(controller.on_close());
    return EXIT_SUCCESS;
}
