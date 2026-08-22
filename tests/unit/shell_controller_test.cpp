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
    int theme_changes = 0;
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
         },
         .theme_changed = [&] { ++theme_changes; }}};

    controller.on_resize({1440, 900});
    CHECK(controller.layout().root.width == 1440);
    CHECK(controller.layout().startup_progress == 0.0F);
    for (int frame = 0; frame < 79; ++frame) controller.on_timer();
    CHECK(controller.layout().startup_progress < 1.0F);
    controller.on_timer();
    controller.on_timer();
    CHECK(controller.layout().startup_progress == 1.0F);
    controller.on_paint();
    CHECK(paints == 1);

    auto update_status = model.status();
    update_status.update_newer_version_known = true;
    update_status.update_available = true;
    model.set_status(update_status);
    controller.synchronize_model();
    CHECK(controller.layout().update_glow_progress == 0.0F);
    controller.on_timer();
    CHECK(controller.layout().update_glow_progress > 0.0F);
    CHECK(controller.layout().update_glow_progress < 1.0F);
    update_status.update_newer_version_known = false;
    update_status.update_available = false;
    model.set_status(update_status);
    controller.synchronize_model();

    controller.on_key({WindowKey::end});
    CHECK(model.focused_destination() == Destination::diagnostics);
    controller.on_key({WindowKey::enter});
    CHECK(model.selected() == Destination::diagnostics);
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "diagnostics-full-check");
    controller.on_key({WindowKey::shift_tab});
    CHECK(!model.focused_action());
    controller.on_key({WindowKey::home});
    CHECK(model.focused_destination() == Destination::dashboard);
    controller.on_key({WindowKey::right});
    CHECK(model.focused_destination() == Destination::graphics);
    controller.on_key({WindowKey::left});
    CHECK(model.focused_destination() == Destination::dashboard);

    const auto help_nav = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.destination == Destination::diagnostics;
        });
    CHECK(help_nav != controller.layout().nodes.end());
    const DipRect help_nav_bounds = help_nav->bounds;
    controller.on_pointer({PointerKind::press,
        {help_nav_bounds.x + 4, help_nav_bounds.y + 4}, 0});
    const auto pressed_navigation = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.destination == Destination::diagnostics;
        });
    CHECK(pressed_navigation != controller.layout().nodes.end());
    CHECK(pressed_navigation->pressed);
    CHECK(pressed_navigation->interaction == 1.0F);
    controller.on_pointer({PointerKind::release,
        {help_nav_bounds.x + 4, help_nav_bounds.y + 4}, 0});
    CHECK(model.selected() == Destination::diagnostics);
    CHECK(controller.layout().page_transition_progress == 0.0F);
    CHECK(controller.layout().navigation_indicator.has_value());
    CHECK(controller.layout().navigation_indicator->y != help_nav_bounds.y);
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "diagnostics-full-check");
    controller.on_key({WindowKey::enter});
    CHECK(last_action == "diagnostics-full-check");
    controller.on_key({WindowKey::tab});
    CHECK(model.focused_action() == "diagnostics-repair-package");
    controller.on_key({WindowKey::shift_tab});
    CHECK(model.focused_action() == "diagnostics-full-check");

    const auto full_check = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.action_id == "diagnostics-full-check";
        });
    CHECK(full_check != controller.layout().nodes.end());
    const DipRect full_check_bounds = full_check->bounds;
    controller.on_pointer({PointerKind::move,
        {full_check_bounds.x + 4, full_check_bounds.y + 4}, 0});
    const auto tooltip = [&]() {
        return std::find_if(controller.layout().nodes.begin(),
                            controller.layout().nodes.end(),
                            [](const SemanticNode& item) {
                                return item.role == SemanticRole::tooltip;
                            });
    };
    CHECK(tooltip() != controller.layout().nodes.end());
    CHECK(tooltip()->opacity > 0.0F);
    CHECK(tooltip()->opacity < 1.0F);
    const float first_tooltip_opacity = tooltip()->opacity;
    // Runtime invalidation synchronizes the model before painting. Transient
    // hover UI must survive that layout rebuild or an animated tooltip is
    // removed on the first animation frame.
    controller.synchronize_model();
    CHECK(tooltip() != controller.layout().nodes.end());
    CHECK(tooltip()->opacity == first_tooltip_opacity);
    controller.on_timer();
    CHECK(tooltip() != controller.layout().nodes.end());
    CHECK(tooltip()->opacity > first_tooltip_opacity);
    const auto hovered_action = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "diagnostics-full-check";
        });
    CHECK(hovered_action != controller.layout().nodes.end());
    CHECK(hovered_action->hover > 0.0F);
    for (int frame = 0; frame < 44; ++frame) controller.on_timer();
    CHECK(tooltip() != controller.layout().nodes.end());
    CHECK(tooltip()->opacity == 1.0F);
    controller.on_pointer({PointerKind::leave, {}, 0});
    CHECK(tooltip() != controller.layout().nodes.end());
    controller.on_timer();
    CHECK(tooltip() != controller.layout().nodes.end());
    CHECK(tooltip()->opacity < 1.0F);
    for (int frame = 0; frame < 46; ++frame) controller.on_timer();
    CHECK(std::none_of(controller.layout().nodes.begin(),
                       controller.layout().nodes.end(),
                       [](const SemanticNode& item) {
                           return item.role == SemanticRole::tooltip;
                       }));

    controller.on_pointer({PointerKind::press,
        {full_check_bounds.x + 4, full_check_bounds.y + 4}, 0});
    const auto pressed_action = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "diagnostics-full-check";
        });
    CHECK(pressed_action != controller.layout().nodes.end());
    CHECK(pressed_action->pressed);
    CHECK(pressed_action->interaction == 1.0F);
    controller.on_pointer({PointerKind::release,
        {full_check_bounds.x + 4, full_check_bounds.y + 4}, 0});
    const auto released_action = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "diagnostics-full-check";
        });
    CHECK(released_action != controller.layout().nodes.end());
    CHECK(!released_action->pressed);
    CHECK(released_action->interaction == 1.0F);
    controller.on_timer();
    const auto releasing_action = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "diagnostics-full-check";
        });
    CHECK(releasing_action != controller.layout().nodes.end());
    CHECK(releasing_action->interaction > 0.0F);
    CHECK(releasing_action->interaction < 1.0F);
    for (int frame = 0; frame < 29; ++frame) controller.on_timer();

    auto adaptive_status = model.status();
    adaptive_status.mode = L"Adaptive / Automatic";
    adaptive_status.game_detected = true;
    model.set_status(adaptive_status);
    controller.focus_target(Destination::dashboard, "settings-target-slider");
    CHECK(model.selected() == Destination::dashboard);
    controller.on_key({WindowKey::right});
    CHECK(last_slider == "settings-target-slider");
    CHECK(last_slider_value == 61);

    const auto target = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-target-slider";
        });
    CHECK(target != controller.layout().nodes.end());
    const DipRect target_bounds = target->bounds;
    const float drag_y = target_bounds.y + target_bounds.height - 25.0F;
    last_slider.clear();
    controller.on_pointer({PointerKind::press,
        {target_bounds.x + 30.0F, drag_y}, 0});
    controller.on_pointer({PointerKind::move,
        {target_bounds.x + target_bounds.width - 30.0F, drag_y}, 0});
    CHECK(repaints > 0);
    CHECK(last_slider.empty());
    const auto target_preview = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-target-slider";
        });
    const auto target_status_preview = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) { return item.id == "status"; });
    CHECK(target_preview != controller.layout().nodes.end());
    CHECK(target_status_preview != controller.layout().nodes.end());
    CHECK(target_preview->slider.has_value());
    CHECK(target_preview->pressed);
    CHECK(target_preview->interaction == 1.0F);
    CHECK(target_status_preview->text.find(
              L"Target " + std::to_wstring(target_preview->slider->value) +
              L" FPS") != std::wstring::npos);
    controller.on_pointer({PointerKind::release,
        {target_bounds.x + target_bounds.width - 30.0F, drag_y}, 0});
    CHECK(last_slider == "settings-target-slider");
    CHECK(last_slider_value >= 235);
    CHECK(last_slider_value <= 240);
    const auto released_slider = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-target-slider";
        });
    CHECK(released_slider != controller.layout().nodes.end());
    CHECK(!released_slider->pressed);
    CHECK(released_slider->interaction == 1.0F);

    const auto corpses = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-corpses-slider";
        });
    CHECK(corpses != controller.layout().nodes.end());
    const DipRect corpse_bounds = corpses->bounds;
    const float corpse_drag_y =
        corpse_bounds.y + corpse_bounds.height - 25.0F;
    last_slider.clear();
    controller.on_pointer({PointerKind::press,
        {corpse_bounds.x + 30.0F, corpse_drag_y}, 0});
    controller.on_pointer({PointerKind::move,
        {corpse_bounds.x + corpse_bounds.width * 0.75F, corpse_drag_y}, 0});
    CHECK(last_slider.empty());
    const auto corpse_preview = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "settings-corpses-slider";
        });
    const auto corpse_status_preview = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) { return item.id == "status"; });
    CHECK(corpse_preview != controller.layout().nodes.end());
    CHECK(corpse_status_preview != controller.layout().nodes.end());
    CHECK(corpse_preview->slider.has_value());
    CHECK(corpse_status_preview->text.find(
              L"Maximum corpses " +
              std::to_wstring(corpse_preview->slider->value)) !=
          std::wstring::npos);
    controller.on_pointer({PointerKind::release,
        {corpse_bounds.x + corpse_bounds.width * 0.75F, corpse_drag_y}, 0});
    CHECK(last_slider == "settings-corpses-slider");

    controller.focus_target(Destination::dashboard,
                            "settings-updates-automatic");
    controller.on_key({WindowKey::enter});
    CHECK(last_action == "settings-updates-automatic");

    auto advanced_status = model.status();
    advanced_status.advanced_available = true;
    model.set_status(advanced_status);
    controller.synchronize_model();
    controller.focus_target(Destination::advanced,
                            "advanced-screen-percentage-slider");
    controller.on_key({WindowKey::right});
    CHECK(last_slider == "advanced-screen-percentage-slider");
    CHECK(last_slider_value == 101);

    const auto advanced_slider = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "advanced-screen-percentage-slider";
        });
    CHECK(advanced_slider != controller.layout().nodes.end());
    const auto slider_value_before_wheel = last_slider_value;
    const auto scroll_before_slider_wheel = model.scroll_offset();
    controller.on_pointer({
        PointerKind::wheel,
        {advanced_slider->bounds.x + advanced_slider->bounds.width / 2.0F,
         advanced_slider->bounds.y + advanced_slider->bounds.height / 2.0F},
        -120});
    CHECK(last_slider_value == slider_value_before_wheel);
    CHECK(model.scroll_offset() > scroll_before_slider_wheel);

    last_action.clear();
    const auto auto_checks = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) {
            return item.id == "header-auto-updates";
        });
    CHECK(auto_checks != controller.layout().nodes.end());
    const float auto_checks_x =
        auto_checks->bounds.x + auto_checks->bounds.width / 2.0F;
    const float auto_checks_y =
        auto_checks->bounds.y + auto_checks->bounds.height / 2.0F;
    controller.on_pointer(
        {PointerKind::press, {auto_checks_x, auto_checks_y}, 0});
    controller.on_pointer(
        {PointerKind::release, {auto_checks_x, auto_checks_y}, 0});
    CHECK(last_action == "settings-updates-automatic");

    controller.focus_target(Destination::diagnostics);
    controller.on_resize({800, 520});
    CHECK(model.scroll_extent() > 0);
    controller.on_pointer({PointerKind::wheel, {400, 400}, -120});
    CHECK(model.scroll_offset() > 0);
    controller.on_key({WindowKey::page_up});
    CHECK(model.scroll_offset() == 0);

    controller.on_dpi_changed({192, {800, 520}});
    CHECK(controller.dpi() == 192);

    model.commit_target_fps_presentation(60);
    model.commit_corpse_limit_presentation(20);
    controller.on_timer();
    auto animated_status = model.status();
    animated_status.game_detected = true;
    animated_status.target_fps = 120;
    animated_status.corpse_limit = 1000;
    model.set_status(animated_status);
    controller.synchronize_model();
    CHECK(model.presented_target_fps() == 60);
    CHECK(model.presented_corpse_limit() == 20);
    controller.on_timer();
    CHECK(model.presented_target_fps() > 60);
    CHECK(model.presented_target_fps() < 120);
    CHECK(model.presented_corpse_limit() > 20);
    CHECK(model.presented_corpse_limit() < 1000);
    const auto animated_header = std::find_if(
        controller.layout().nodes.begin(), controller.layout().nodes.end(),
        [](const SemanticNode& item) { return item.id == "status"; });
    CHECK(animated_header != controller.layout().nodes.end());
    CHECK(animated_header->text.find(
              L"Target " + std::to_wstring(model.presented_target_fps()) +
              L" FPS") != std::wstring::npos);
    CHECK(animated_header->text.find(
              L"Maximum corpses " +
              std::to_wstring(model.presented_corpse_limit())) !=
          std::wstring::npos);
    for (int frame = 0; frame < 80; ++frame) controller.on_timer();
    CHECK(model.presented_target_fps() == 120);
    CHECK(model.presented_corpse_limit() == 1000);

    animated_status = model.status();
    animated_status.live_fps = 60.0;
    animated_status.live_frame_time_ms = 16.7;
    animated_status.live_cpu_percent = 30.0;
    animated_status.live_gpu_percent = 40.0;
    animated_status.live_active_corpses = 10;
    animated_status.live_sleeping_corpses = 20;
    model.set_status(animated_status);
    controller.on_timer();
    CHECK(model.presented_live_fps() == 60.0);
    CHECK(model.presented_live_frame_time_ms() == 16.7);
    CHECK(model.presented_live_cpu_percent() == 30.0);
    CHECK(model.presented_live_gpu_percent() == 40.0);
    CHECK(model.presented_live_active_corpses() == 10);
    CHECK(model.presented_live_sleeping_corpses() == 20);

    animated_status.live_fps = 120.0;
    animated_status.live_frame_time_ms = 8.3;
    animated_status.live_cpu_percent = 70.0;
    animated_status.live_gpu_percent = 80.0;
    animated_status.live_active_corpses = 50;
    animated_status.live_sleeping_corpses = 100;
    model.set_status(animated_status);
    controller.on_timer();
    CHECK(*model.presented_live_fps() > 60.0);
    CHECK(*model.presented_live_fps() < 120.0);
    CHECK(*model.presented_live_frame_time_ms() < 16.7);
    CHECK(*model.presented_live_frame_time_ms() > 8.3);
    CHECK(*model.presented_live_cpu_percent() > 30.0);
    CHECK(*model.presented_live_cpu_percent() < 70.0);
    CHECK(*model.presented_live_gpu_percent() > 40.0);
    CHECK(*model.presented_live_gpu_percent() < 80.0);
    CHECK(*model.presented_live_active_corpses() > 10);
    CHECK(*model.presented_live_active_corpses() < 50);
    CHECK(*model.presented_live_sleeping_corpses() > 20);
    CHECK(*model.presented_live_sleeping_corpses() < 100);
    const auto find_node = [&](const char* id) {
        return std::find_if(
            controller.layout().nodes.begin(), controller.layout().nodes.end(),
            [id](const SemanticNode& item) { return item.id == id; });
    };
    const auto fps_card = find_node("metric-0");
    const auto frame_time_card = find_node("metric-1");
    const auto load_card = find_node("metric-2");
    const auto corpse_card = find_node("metric-3");
    CHECK(fps_card != controller.layout().nodes.end());
    CHECK(frame_time_card != controller.layout().nodes.end());
    CHECK(load_card != controller.layout().nodes.end());
    CHECK(corpse_card != controller.layout().nodes.end());
    CHECK(fps_card->text.find(L"120.0 FPS") == std::wstring::npos);
    CHECK(frame_time_card->text.find(L"8.3 ms") == std::wstring::npos);
    CHECK(load_card->text.find(L"CPU 70%") == std::wstring::npos);
    CHECK(load_card->text.find(L"GPU 80%") == std::wstring::npos);
    CHECK(corpse_card->text.find(L"50 / 100") == std::wstring::npos);
    for (int frame = 0; frame < 100; ++frame) controller.on_timer();
    CHECK(model.presented_live_fps() == 120.0);
    CHECK(model.presented_live_frame_time_ms() == 8.3);
    CHECK(model.presented_live_cpu_percent() == 70.0);
    CHECK(model.presented_live_gpu_percent() == 80.0);
    CHECK(model.presented_live_active_corpses() == 50);
    CHECK(model.presented_live_sleeping_corpses() == 100);

    controller.on_theme_changed({true, true});
    CHECK(theme_changes == 1);
    CHECK(!controller.theme().animations_enabled);
    animated_status = model.status();
    animated_status.target_fps = 144;
    animated_status.corpse_limit = 2000;
    animated_status.live_fps = 90.0;
    animated_status.live_frame_time_ms = 11.1;
    animated_status.live_cpu_percent = 35.0;
    animated_status.live_gpu_percent = 45.0;
    animated_status.live_active_corpses = 5;
    animated_status.live_sleeping_corpses = 15;
    model.set_status(animated_status);
    controller.on_timer();
    CHECK(model.presented_target_fps() == 144);
    CHECK(model.presented_corpse_limit() == 2000);
    CHECK(model.presented_live_fps() == 90.0);
    CHECK(model.presented_live_frame_time_ms() == 11.1);
    CHECK(model.presented_live_cpu_percent() == 35.0);
    CHECK(model.presented_live_gpu_percent() == 45.0);
    CHECK(model.presented_live_active_corpses() == 5);
    CHECK(model.presented_live_sleeping_corpses() == 15);
    CHECK(invalidations >= 8);
    CHECK(ticks >= 184);
    controller.on_system_resume();
    CHECK(resumes == 1);
    controller.on_key({WindowKey::f10});
    CHECK(overlay_toggles == 1);
    CHECK(controller.on_close());

    UiModel closing_model;
    int close_requests = 0;
    ShellController closing_controller{
        closing_model,
        {.request_close = [&] { ++close_requests; }}};
    CHECK(!closing_controller.on_close());
    CHECK(closing_controller.layout().exit_progress == 0.0F);
    for (int frame = 0; frame < 44; ++frame) closing_controller.on_timer();
    CHECK(closing_controller.layout().exit_progress < 1.0F);
    closing_controller.on_timer();
    closing_controller.on_timer();
    CHECK(closing_controller.layout().exit_progress == 1.0F);
    CHECK(close_requests == 1);
    CHECK(closing_controller.on_close());
    return EXIT_SUCCESS;
}
