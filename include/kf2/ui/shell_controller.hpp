#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "kf2/platform/windows/window_events.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/theme.hpp"
#include "kf2/ui/ui_model.hpp"

namespace kf2::ui {

struct ShellControllerCallbacks {
    std::function<void()> invalidate;
    std::function<void()> repaint;
    std::function<void(const ShellLayoutResult&)> paint;
    std::function<LRESULT(WPARAM, LPARAM)> get_object;
    std::function<void()> tick;
    std::function<void()> system_resume;
    std::function<void()> toggle_overlay;
    std::function<void(std::string_view)> activate_action;
    std::function<void(std::string_view, int)> set_slider_value;
    std::function<void()> request_close;
    std::function<void()> theme_changed;
};

class ShellController final : public platform::windows::WindowEventSink {
public:
    ShellController(UiModel& model, ShellControllerCallbacks callbacks);

    void on_paint() override;
    void on_resize(platform::windows::WindowSize size) override;
    void on_dpi_changed(platform::windows::DpiChangedEvent event) override;
    void on_key(platform::windows::KeyEvent event) override;
    void on_pointer(platform::windows::PointerEvent event) override;
    void on_theme_changed(platform::windows::ThemeChangedEvent event) override;
    [[nodiscard]] bool on_close() override;
    LRESULT on_get_object(WPARAM wparam, LPARAM lparam) override;
    void on_timer() override;
    void on_system_resume() override;

    [[nodiscard]] const ShellLayoutResult& layout() const noexcept;
    [[nodiscard]] const Theme& theme() const noexcept;
    [[nodiscard]] float dpi() const noexcept;
    void synchronize_model();
    void focus_target(Destination destination,
                      std::optional<std::string_view> action = std::nullopt);

private:
    void apply(UiAction action);
    void rebuild_layout();
    void move_tab_focus(bool forward);
    void ensure_focused_action_visible();
    void activate_keyboard_focus();
    void update_hover_tooltip(const SemanticNode* node);
    void clear_hover_tooltip();
    [[nodiscard]] bool render_active_tooltip();
    void begin_interaction(std::string_view node_id);
    void end_interaction();
    [[nodiscard]] bool render_active_interaction();
    [[nodiscard]] bool advance_hover_states();
    void render_hover_states();
    void apply_motion_state();
    [[nodiscard]] std::optional<DipRect> selected_navigation_bounds() const;
    [[nodiscard]] bool adjust_focused_slider(
        platform::windows::WindowKey key);
    [[nodiscard]] std::optional<int> slider_value_at(
        const SemanticNode& node, float x_dip) const noexcept;
    void preview_slider(std::string_view node_id, float x_dip);
    void commit_slider(std::string_view node_id, int value);

    UiModel& model_;
    ShellControllerCallbacks callbacks_;
    platform::windows::WindowSize client_size_{1440, 900};
    float dpi_{96};
    ThemeInput theme_input_{};
    Theme theme_{resolve_theme(theme_input_)};
    ShellLayoutResult layout_;
    std::optional<std::string> hovered_node_id_;
    std::optional<std::string> tooltip_target_id_;
    float tooltip_opacity_{0.0F};
    std::optional<std::string> interaction_target_id_;
    float interaction_strength_{0.0F};
    bool interaction_held_{false};
    std::unordered_map<std::string, float> hover_strengths_;
    float startup_progress_{0.0F};
    float page_transition_progress_{0.0F};
    float navigation_transition_progress_{1.0F};
    float navigation_from_y_{0.0F};
    float navigation_to_y_{0.0F};
    float update_glow_progress_{1.0F};
    bool update_was_available_{false};
    std::optional<Destination> rendered_destination_;
    float exit_progress_{0.0F};
    bool closing_{false};
    bool close_ready_{false};
    std::optional<std::string> pressed_node_id_;
    std::optional<std::string> dragged_slider_id_;
    std::optional<int> dragged_slider_value_;
};

}  // namespace kf2::ui
