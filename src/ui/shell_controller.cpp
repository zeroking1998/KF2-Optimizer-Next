#include "kf2/ui/shell_controller.hpp"

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
}

void ShellController::on_system_resume() {
    if (callbacks_.system_resume) callbacks_.system_resume();
}

void ShellController::on_pointer(platform::windows::PointerEvent event) {
    if (event.kind == platform::windows::PointerKind::leave) {
        if (dragged_slider_id_) return;
        hovered_node_id_.reset();
        clear_hover_tooltip();
        return;
    }
    if (event.kind == platform::windows::PointerKind::wheel) {
        const auto* hovered = hit_test(
            layout_, {event.position.x_dip, event.position.y_dip});
        if (hovered != nullptr && hovered->role == SemanticRole::slider &&
            hovered->enabled && hovered->slider && hovered->action_id) {
            const int direction = event.wheel_delta >= 0.0F ? 1 : -1;
            const int value = std::clamp(
                hovered->slider->value + direction * hovered->slider->small_step,
                hovered->slider->minimum, hovered->slider->maximum);
            commit_slider(hovered->id, value);
            return;
        }
        apply(model_.set_scroll(model_.scroll_offset() -
                                (event.wheel_delta / 120.0F) * 80.0F));
        return;
    }
    const auto* node = hit_test(layout_, {event.position.x_dip, event.position.y_dip});
    if (event.kind == platform::windows::PointerKind::press) {
        pressed_node_id_ = node && node->enabled
            ? std::optional<std::string>{node->id} : std::nullopt;
        if (node != nullptr && node->enabled && node->action_id &&
            (node->role == SemanticRole::action ||
             node->role == SemanticRole::slider)) {
            const std::string id = node->id;
            const std::string action = *node->action_id;
            apply(model_.focus_action(action));
            if (node->role == SemanticRole::slider) {
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
                    .dark = true,
                    .reduced_motion = event.reduced_motion};
    theme_ = resolve_theme(theme_input_);
    theme_.animations_enabled = theme_.animations_enabled &&
                                user_animations_enabled_;
    if (callbacks_.invalidate) {
        callbacks_.invalidate();
    }
}

void ShellController::set_animations_enabled(bool enabled) {
    user_animations_enabled_ = enabled;
    theme_ = resolve_theme(theme_input_);
    theme_.animations_enabled = theme_.animations_enabled &&
                                user_animations_enabled_;
    if (callbacks_.invalidate) callbacks_.invalidate();
}

bool ShellController::on_close() { return true; }
LRESULT ShellController::on_get_object(WPARAM wparam, LPARAM lparam) {
    return callbacks_.get_object ? callbacks_.get_object(wparam, lparam) : 0;
}
const ShellLayoutResult& ShellController::layout() const noexcept { return layout_; }
const Theme& ShellController::theme() const noexcept { return theme_; }
float ShellController::dpi() const noexcept { return dpi_; }

void ShellController::synchronize_model() {
    layout_ = layout_shell(model_, client_size_.width_dip, client_size_.height_dip);
    model_.set_scroll_extent(layout_.scroll_extent);
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
    synchronize_model();
    hovered_node_id_.reset();
    if (callbacks_.invalidate) {
        callbacks_.invalidate();
    }
}

void ShellController::update_hover_tooltip(const SemanticNode* node) {
    std::optional<SemanticNode> target;
    if (node) target = *node;
    set_hover_tooltip(layout_, target ? &*target : nullptr);
    if (callbacks_.repaint) callbacks_.repaint();
    else if (callbacks_.invalidate) callbacks_.invalidate();
}

void ShellController::clear_hover_tooltip() {
    const auto previous = layout_.nodes.size();
    set_hover_tooltip(layout_, nullptr);
    if (layout_.nodes.size() != previous) {
        if (callbacks_.repaint) callbacks_.repaint();
        else if (callbacks_.invalidate) callbacks_.invalidate();
    }
}

void ShellController::move_tab_focus(bool forward) {
    std::vector<std::string> actions;
    std::vector<std::string> header_actions;
    for (const auto& node : layout_.nodes) {
        if ((node.role == SemanticRole::action ||
             node.role == SemanticRole::slider) &&
            node.enabled && node.action_id) {
            if (node.action_id->starts_with("header-")) {
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
    if (found->action_id && found->action_id->starts_with("header-")) return;

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
    found->slider->value = *value;
    dragged_slider_value_ = *value;
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
    callbacks_.set_slider_value(*found->action_id,
        std::clamp(value, found->slider->minimum, found->slider->maximum));
}

}  // namespace kf2::ui
