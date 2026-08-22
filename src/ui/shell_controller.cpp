#include "kf2/ui/shell_controller.hpp"
#include "kf2/ui/ui_animation.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace kf2::ui {

ShellController::ShellController(UiModel& model, ShellControllerCallbacks callbacks)
    : model_{model}, callbacks_{std::move(callbacks)} {
    rebuild_layout();
}

void ShellController::on_paint() {
    if (callbacks_.paint) {
        callbacks_.paint(layout_);
    }
}

void ShellController::on_resize(platform::windows::WindowSize size) {
    client_size_ = {std::max(0.0F, size.width_dip),
                    std::max(0.0F, size.height_dip)};
    rebuild_layout();
}

void ShellController::on_dpi_changed(platform::windows::DpiChangedEvent event) {
    dpi_ = event.dpi > 0.0F ? event.dpi : 96.0F;
    client_size_ = event.size;
    rebuild_layout();
}

void ShellController::on_key(platform::windows::KeyEvent event) {
    if (event.key == platform::windows::WindowKey::f10) {
        if (callbacks_.toggle_overlay) callbacks_.toggle_overlay();
        return;
    }
    using platform::windows::WindowKey;
    if (adjust_focused_slider(event.key)) return;
    switch (event.key) {
        case WindowKey::tab:
            move_tab_focus(true);
            break;
        case WindowKey::shift_tab:
            move_tab_focus(false);
            break;
        case WindowKey::down:
        case WindowKey::right:
            apply(model_.navigate(NavigationCommand::next));
            break;
        case WindowKey::up:
        case WindowKey::left:
            apply(model_.navigate(NavigationCommand::previous));
            break;
        case WindowKey::home:
            apply(model_.navigate(NavigationCommand::home));
            break;
        case WindowKey::end:
            apply(model_.navigate(NavigationCommand::end));
            break;
        case WindowKey::enter:
        case WindowKey::space:
            activate_keyboard_focus();
            break;
        case WindowKey::page_up:
            apply(model_.set_scroll(model_.scroll_offset() -
                                    std::max(100.0F, layout_.content.height * 0.8F)));
            break;
        case WindowKey::page_down:
            apply(model_.set_scroll(model_.scroll_offset() +
                                    std::max(100.0F, layout_.content.height * 0.8F)));
            break;
        case WindowKey::f10:
            break;
    }
}

void ShellController::on_timer() {
    if (callbacks_.tick) callbacks_.tick();
    bool changed = model_.advance_numeric_presentation(theme_.animations_enabled);
    if (changed) {
        synchronize_model();
    }

    const float previous_startup = startup_progress_;
    startup_progress_ = advance_startup_progress(
        startup_progress_, theme_.animations_enabled);
    changed = changed || startup_progress_ != previous_startup;

    const float previous_page = page_transition_progress_;
    page_transition_progress_ = advance_page_progress(
        page_transition_progress_, theme_.animations_enabled);
    changed = changed || page_transition_progress_ != previous_page;

    const float previous_navigation = navigation_transition_progress_;
    navigation_transition_progress_ = advance_navigation_progress(
        navigation_transition_progress_, theme_.animations_enabled);
    changed = changed || navigation_transition_progress_ != previous_navigation;

    const float previous_glow = update_glow_progress_;
    update_glow_progress_ = advance_update_glow_progress(
        update_glow_progress_, theme_.animations_enabled);
    changed = changed || update_glow_progress_ != previous_glow;

    if (closing_) {
        const float previous_exit = exit_progress_;
        exit_progress_ = advance_exit_progress(
            exit_progress_, theme_.animations_enabled);
        layout_.exit_progress = exit_progress_;
        changed = changed || exit_progress_ != previous_exit;
        if (exit_progress_ >= 1.0F && !close_ready_) {
            close_ready_ = true;
            if (callbacks_.request_close) callbacks_.request_close();
        }
    }

    if (tooltip_target_id_) {
        const bool fade_in = hovered_node_id_ == tooltip_target_id_;
        const float previous = tooltip_opacity_;
        tooltip_opacity_ = advance_tooltip_opacity(
            tooltip_opacity_, fade_in, theme_.animations_enabled);
        if (tooltip_opacity_ <= 0.0F) {
            set_hover_tooltip(layout_, nullptr);
            tooltip_target_id_.reset();
            changed = true;
        } else if (tooltip_opacity_ != previous || changed) {
            changed = render_active_tooltip() || changed;
        }
    }

    if (interaction_target_id_) {
        const float previous = interaction_strength_;
        interaction_strength_ = advance_interaction_strength(
            interaction_strength_, interaction_held_,
            theme_.animations_enabled);
        if (interaction_strength_ <= 0.0F && !interaction_held_) {
            interaction_target_id_.reset();
            changed = true;
        } else if (interaction_strength_ != previous || changed) {
            changed = render_active_interaction() || changed;
        }
    }

    changed = advance_hover_states() || changed;
    apply_motion_state();

    if (changed && callbacks_.invalidate) {
        callbacks_.invalidate();
    }
}

void ShellController::on_system_resume() {
    if (callbacks_.system_resume) callbacks_.system_resume();
}

void ShellController::on_pointer(platform::windows::PointerEvent event) {
    if (event.kind == platform::windows::PointerKind::leave) {
        if (dragged_slider_id_) return;
        hovered_node_id_.reset();
        clear_hover_tooltip();
        end_interaction();
        return;
    }
    if (event.kind == platform::windows::PointerKind::wheel) {
        // The wheel always scrolls the page. Changing a slider merely because
        // the pointer happens to be above it is too easy to do accidentally.
        // Sliders remain adjustable by dragging and with the keyboard.
        apply(model_.set_scroll(model_.scroll_offset() -
                                (event.wheel_delta / 120.0F) * 80.0F));
        return;
    }
    const auto* node = hit_test(layout_, {event.position.x_dip, event.position.y_dip});
    if (event.kind == platform::windows::PointerKind::press) {
        pressed_node_id_ = node && node->enabled
            ? std::optional<std::string>{node->id} : std::nullopt;
        if (node != nullptr && node->enabled &&
            (node->role == SemanticRole::navigation_item ||
             node->role == SemanticRole::action ||
             node->role == SemanticRole::slider)) {
            begin_interaction(node->id);
        }
        if (node != nullptr && node->enabled && node->action_id &&
            (node->role == SemanticRole::action ||
             node->role == SemanticRole::slider)) {
            const std::string id = node->id;
            const std::string action = *node->action_id;
            const bool slider = node->role == SemanticRole::slider;
            apply(model_.focus_action(action));
            if (slider) {
                dragged_slider_id_ = id;
                preview_slider(id, event.position.x_dip);
            }
        }
        return;
    }
    if (event.kind == platform::windows::PointerKind::move) {
        if (dragged_slider_id_) {
            preview_slider(*dragged_slider_id_, event.position.x_dip);
            return;
        }
        const bool interactive = node != nullptr && node->enabled &&
            (node->role == SemanticRole::navigation_item ||
             node->role == SemanticRole::action ||
             node->role == SemanticRole::slider);
        const std::optional<std::string> next = interactive
            ? std::optional<std::string>{node->id} : std::nullopt;
        if (next != hovered_node_id_) {
            hovered_node_id_ = next;
            if (hovered_node_id_) {
                hover_strengths_.try_emplace(*hovered_node_id_, 0.0F);
            }
            render_hover_states();
            update_hover_tooltip(interactive ? node : nullptr);
        }
        return;
    }
    if (event.kind == platform::windows::PointerKind::release &&
        dragged_slider_id_) {
        const std::string id = *dragged_slider_id_;
        preview_slider(id, event.position.x_dip);
        const auto value = dragged_slider_value_;
        dragged_slider_id_.reset();
        dragged_slider_value_.reset();
        pressed_node_id_.reset();
        end_interaction();
        if (value) commit_slider(id, *value);
        return;
    }
    if (event.kind != platform::windows::PointerKind::release &&
        event.kind != platform::windows::PointerKind::activate) {
        return;
    }
    const bool same_pressed = !pressed_node_id_ ||
        (node != nullptr && node->id == *pressed_node_id_);
    pressed_node_id_.reset();
    end_interaction();
    if (!same_pressed) return;
    if (node != nullptr && node->role == SemanticRole::navigation_item &&
        node->destination.has_value()) {
        const UiAction focus = model_.focus_destination(*node->destination);
        const UiAction activate = model_.activate_focused();
        apply({focus.changed || activate.changed});
    } else if (node != nullptr && node->role == SemanticRole::action &&
               node->action_id && node->enabled && callbacks_.activate_action) {
        // Focusing rebuilds the semantic layout and invalidates node pointers.
        // Copy the stable action identifier before that rebuild.
        const std::string action_id = *node->action_id;
        apply(model_.focus_action(action_id));
        callbacks_.activate_action(action_id);
    }
}

void ShellController::on_theme_changed(platform::windows::ThemeChangedEvent event) {
    theme_input_ = {.high_contrast = event.high_contrast,
                    .dark = true};
    theme_ = resolve_theme(theme_input_);
    if (!theme_.animations_enabled) {
        startup_progress_ = 1.0F;
        page_transition_progress_ = 1.0F;
        navigation_transition_progress_ = 1.0F;
        update_glow_progress_ = 1.0F;
        apply_motion_state();
    }
    if (!theme_.animations_enabled && tooltip_target_id_) {
        if (hovered_node_id_ == tooltip_target_id_) {
            tooltip_opacity_ = 1.0F;
            static_cast<void>(render_active_tooltip());
        } else {
            set_hover_tooltip(layout_, nullptr);
            tooltip_target_id_.reset();
            tooltip_opacity_ = 0.0F;
        }
    }
    if (!theme_.animations_enabled && interaction_target_id_ &&
        !interaction_held_) {
        interaction_target_id_.reset();
        interaction_strength_ = 0.0F;
    }
    if (callbacks_.invalidate) {
        callbacks_.invalidate();
    }
    if (callbacks_.theme_changed) callbacks_.theme_changed();
}

bool ShellController::on_close() {
    if (close_ready_ || !theme_.animations_enabled ||
        !callbacks_.request_close) {
        return true;
    }
    closing_ = true;
    if (callbacks_.invalidate) callbacks_.invalidate();
    return false;
}
LRESULT ShellController::on_get_object(WPARAM wparam, LPARAM lparam) {
    return callbacks_.get_object ? callbacks_.get_object(wparam, lparam) : 0;
}
const ShellLayoutResult& ShellController::layout() const noexcept { return layout_; }
const Theme& ShellController::theme() const noexcept { return theme_; }
float ShellController::dpi() const noexcept { return dpi_; }

void ShellController::synchronize_model() {
    const Destination destination = model_.selected();
    const bool page_changed = rendered_destination_.has_value() &&
                              *rendered_destination_ != destination;
    const float current_navigation_y = interpolate_motion(
        navigation_from_y_, navigation_to_y_,
        smooth_motion(navigation_transition_progress_));
    layout_ = layout_shell(model_, client_size_.width_dip, client_size_.height_dip);
    model_.set_scroll_extent(layout_.scroll_extent);

    const auto selected_bounds = selected_navigation_bounds();
    if (!rendered_destination_) {
        if (selected_bounds) {
            navigation_from_y_ = selected_bounds->y;
            navigation_to_y_ = selected_bounds->y;
        }
    } else if (page_changed && selected_bounds) {
        navigation_from_y_ = current_navigation_y;
        navigation_to_y_ = selected_bounds->y;
        navigation_transition_progress_ = theme_.animations_enabled ? 0.0F : 1.0F;
        page_transition_progress_ = theme_.animations_enabled ? 0.0F : 1.0F;
    }
    rendered_destination_ = destination;

    const bool update_available = model_.status().update_newer_version_known;
    if (update_available && !update_was_available_) {
        update_glow_progress_ = theme_.animations_enabled ? 0.0F : 1.0F;
    }
    update_was_available_ = update_available;
    render_hover_states();
    static_cast<void>(render_active_interaction());
    static_cast<void>(render_active_tooltip());
    apply_motion_state();
}

void ShellController::focus_target(
    Destination destination, std::optional<std::string_view> action) {
    static_cast<void>(model_.focus_destination(destination));
    static_cast<void>(model_.activate_focused());
    if (action) {
        static_cast<void>(model_.focus_action(std::string{*action}));
    }
    rebuild_layout();
    if (action) ensure_focused_action_visible();
}

void ShellController::apply(UiAction action) {
    if (!action.changed) {
        return;
    }
    rebuild_layout();
}

void ShellController::rebuild_layout() {
    tooltip_target_id_.reset();
    tooltip_opacity_ = 0.0F;
    synchronize_model();
    if (callbacks_.invalidate) {
        callbacks_.invalidate();
    }
}

void ShellController::update_hover_tooltip(const SemanticNode* node) {
    if (!node) {
        clear_hover_tooltip();
        return;
    }
    const SemanticNode target = *node;
    const bool same_target = tooltip_target_id_ == target.id;
    tooltip_target_id_ = target.id;
    if (!theme_.animations_enabled) {
        tooltip_opacity_ = 1.0F;
    } else if (!same_target || tooltip_opacity_ <= 0.0F) {
        tooltip_opacity_ = 0.08F;
    }
    set_hover_tooltip(layout_, &target, tooltip_opacity_);
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

void ShellController::clear_hover_tooltip() {
    if (!tooltip_target_id_) return;
    if (theme_.animations_enabled && tooltip_opacity_ > 0.0F) {
        if (callbacks_.repaint) callbacks_.repaint();
        else if (callbacks_.invalidate) callbacks_.invalidate();
        return;
    }
    set_hover_tooltip(layout_, nullptr);
    tooltip_target_id_.reset();
    tooltip_opacity_ = 0.0F;
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

bool ShellController::render_active_tooltip() {
    if (!tooltip_target_id_) return false;
    const auto target = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.id == *tooltip_target_id_ &&
                   node.role != SemanticRole::tooltip;
        });
    if (target == layout_.nodes.end()) {
        set_hover_tooltip(layout_, nullptr);
        tooltip_target_id_.reset();
        tooltip_opacity_ = 0.0F;
        return true;
    }
    const SemanticNode target_copy = *target;
    set_hover_tooltip(layout_, &target_copy, tooltip_opacity_);
    return true;
}

void ShellController::begin_interaction(std::string_view node_id) {
    interaction_target_id_ = std::string{node_id};
    interaction_strength_ = 1.0F;
    interaction_held_ = true;
    static_cast<void>(render_active_interaction());
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

void ShellController::end_interaction() {
    if (!interaction_target_id_) return;
    interaction_held_ = false;
    if (!theme_.animations_enabled) {
        interaction_target_id_.reset();
        interaction_strength_ = 0.0F;
    } else {
        static_cast<void>(render_active_interaction());
    }
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

bool ShellController::render_active_interaction() {
    for (auto& node : layout_.nodes) {
        node.interaction = 0.0F;
        node.pressed = false;
    }
    if (!interaction_target_id_) return false;
    const auto target = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.id == *interaction_target_id_ &&
                   (node.role == SemanticRole::navigation_item ||
                    node.role == SemanticRole::action ||
                    node.role == SemanticRole::slider);
        });
    if (target == layout_.nodes.end()) {
        interaction_target_id_.reset();
        interaction_strength_ = 0.0F;
        interaction_held_ = false;
        return true;
    }
    target->interaction = std::clamp(interaction_strength_, 0.0F, 1.0F);
    target->pressed = interaction_held_;
    return true;
}

bool ShellController::advance_hover_states() {
    bool changed = false;
    for (auto item = hover_strengths_.begin(); item != hover_strengths_.end();) {
        const bool hovered = hovered_node_id_ && *hovered_node_id_ == item->first;
        const float previous = item->second;
        item->second = advance_hover_strength(
            item->second, hovered, theme_.animations_enabled);
        changed = changed || item->second != previous;
        if (!hovered && item->second <= 0.0F) {
            item = hover_strengths_.erase(item);
        } else {
            ++item;
        }
    }
    if (changed) render_hover_states();
    return changed;
}

void ShellController::render_hover_states() {
    for (auto& node : layout_.nodes) {
        const auto found = hover_strengths_.find(node.id);
        node.hover = found == hover_strengths_.end()
            ? 0.0F : std::clamp(found->second, 0.0F, 1.0F);
    }
}

void ShellController::apply_motion_state() {
    layout_.startup_progress = startup_progress_;
    layout_.page_transition_progress = page_transition_progress_;
    layout_.update_glow_progress = update_glow_progress_;
    layout_.exit_progress = exit_progress_;
    if (const auto selected = selected_navigation_bounds()) {
        DipRect indicator = *selected;
        indicator.y = interpolate_motion(
            navigation_from_y_, navigation_to_y_,
            smooth_motion(navigation_transition_progress_));
        layout_.navigation_indicator = indicator;
    } else {
        layout_.navigation_indicator.reset();
    }
}

std::optional<DipRect> ShellController::selected_navigation_bounds() const {
    const auto selected = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [](const SemanticNode& node) {
            return node.role == SemanticRole::navigation_item && node.selected;
        });
    return selected == layout_.nodes.end()
        ? std::nullopt : std::optional<DipRect>{selected->bounds};
}

void ShellController::move_tab_focus(bool forward) {
    std::vector<std::string> actions;
    std::vector<std::string> header_actions;
    for (const auto& node : layout_.nodes) {
        if ((node.role == SemanticRole::action ||
             node.role == SemanticRole::slider) &&
            node.enabled && node.action_id) {
            if (node.id.starts_with("header-")) {
                header_actions.push_back(*node.action_id);
            } else {
                actions.push_back(*node.action_id);
            }
        }
    }
    actions.insert(actions.end(), header_actions.begin(), header_actions.end());

    if (actions.empty()) {
        apply(model_.navigate(forward ? NavigationCommand::next
                                      : NavigationCommand::previous));
        return;
    }

    if (!model_.focused_action()) {
        if (model_.focused_destination() == model_.selected()) {
            apply(model_.focus_action(forward ? actions.front() : actions.back()));
            ensure_focused_action_visible();
        } else {
            apply(model_.navigate(forward ? NavigationCommand::next
                                          : NavigationCommand::previous));
        }
        return;
    }

    const auto current = std::find(actions.begin(), actions.end(),
                                   *model_.focused_action());
    if (current == actions.end()) {
        apply(model_.focus_action(forward ? actions.front() : actions.back()));
        ensure_focused_action_visible();
        return;
    }
    if (forward && std::next(current) != actions.end()) {
        apply(model_.focus_action(*std::next(current)));
        ensure_focused_action_visible();
        return;
    }
    if (!forward && current != actions.begin()) {
        apply(model_.focus_action(*std::prev(current)));
        ensure_focused_action_visible();
        return;
    }
    apply(model_.focus_destination(model_.selected()));
}

void ShellController::ensure_focused_action_visible() {
    if (!model_.focused_action()) return;
    const auto found = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.action_id == model_.focused_action();
        });
    if (found == layout_.nodes.end()) return;
    if (found->id.starts_with("header-")) return;

    float requested = model_.scroll_offset();
    constexpr float margin = 8.0F;
    if (found->bounds.y < layout_.content.y + margin) {
        requested -= layout_.content.y + margin - found->bounds.y;
    } else if (found->bounds.y + found->bounds.height >
               layout_.content.y + layout_.content.height - margin) {
        requested += found->bounds.y + found->bounds.height -
                     (layout_.content.y + layout_.content.height - margin);
    }
    apply(model_.set_scroll(requested));
}

void ShellController::activate_keyboard_focus() {
    if (!model_.focused_action()) {
        apply(model_.activate_focused());
        return;
    }
    const auto found = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.action_id == model_.focused_action();
        });
    if (found != layout_.nodes.end() && found->role != SemanticRole::slider &&
        found->enabled && found->action_id &&
        callbacks_.activate_action) {
        callbacks_.activate_action(*found->action_id);
    }
}

bool ShellController::adjust_focused_slider(
    platform::windows::WindowKey key) {
    if (!model_.focused_action()) return false;
    const auto found = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.role == SemanticRole::slider && node.enabled &&
                   node.action_id == model_.focused_action() && node.slider;
        });
    if (found == layout_.nodes.end()) return false;

    using platform::windows::WindowKey;
    int value = found->slider->value;
    switch (key) {
        case WindowKey::left:
        case WindowKey::down:
            value -= found->slider->small_step;
            break;
        case WindowKey::right:
        case WindowKey::up:
            value += found->slider->small_step;
            break;
        case WindowKey::page_down:
            value -= found->slider->large_step;
            break;
        case WindowKey::page_up:
            value += found->slider->large_step;
            break;
        case WindowKey::home:
            value = found->slider->minimum;
            break;
        case WindowKey::end:
            value = found->slider->maximum;
            break;
        default:
            return false;
    }
    commit_slider(found->id, std::clamp(value, found->slider->minimum,
                                        found->slider->maximum));
    return true;
}

std::optional<int> ShellController::slider_value_at(
    const SemanticNode& node, float x_dip) const noexcept {
    if (node.role != SemanticRole::slider || !node.slider || !node.enabled ||
        node.bounds.width <= 72.0F) {
        return std::nullopt;
    }
    const float left = node.bounds.x + 28.0F;
    const float width = std::max(1.0F, node.bounds.width - 56.0F);
    const float ratio = std::clamp((x_dip - left) / width, 0.0F, 1.0F);
    const int span = node.slider->maximum - node.slider->minimum;
    const int step = std::max(1, node.slider->small_step);
    int value = node.slider->minimum +
        static_cast<int>(std::lround(static_cast<float>(span) * ratio));
    value = node.slider->minimum +
        static_cast<int>(std::lround(
            static_cast<double>(value - node.slider->minimum) / step)) * step;
    return std::clamp(value, node.slider->minimum, node.slider->maximum);
}

void ShellController::preview_slider(std::string_view node_id, float x_dip) {
    const auto found = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.id == node_id;
        });
    if (found == layout_.nodes.end()) return;
    const auto value = slider_value_at(*found, x_dip);
    if (!value || (dragged_slider_value_ && *dragged_slider_value_ == *value)) {
        return;
    }
    dragged_slider_value_ = *value;
    if (node_id == "settings-target-slider") {
        model_.preview_target_fps(*value);
    } else if (node_id == "settings-corpses-slider") {
        model_.preview_corpse_limit(*value);
    }
    rebuild_layout();
    const auto previewed = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.id == node_id;
        });
    if (previewed != layout_.nodes.end() && previewed->slider) {
        previewed->slider->value = *value;
    }
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

void ShellController::commit_slider(std::string_view node_id, int value) {
    const auto found = std::find_if(
        layout_.nodes.begin(), layout_.nodes.end(), [&](const SemanticNode& node) {
            return node.id == node_id;
        });
    if (found == layout_.nodes.end() || !found->action_id || !found->slider ||
        !callbacks_.set_slider_value) {
        return;
    }
    const std::string action = *found->action_id;
    const int committed = std::clamp(
        value, found->slider->minimum, found->slider->maximum);
    if (node_id == "settings-target-slider") {
        model_.commit_target_fps_presentation(committed);
    } else if (node_id == "settings-corpses-slider") {
        model_.commit_corpse_limit_presentation(committed);
    }
    rebuild_layout();
    callbacks_.set_slider_value(action, committed);
}

}  // namespace kf2::ui
