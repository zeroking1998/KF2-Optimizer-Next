#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kf2/ui/ui_model.hpp"

namespace kf2::ui {

struct DipPoint {
    float x{0};
    float y{0};
};

struct DipRect {
    float x{0};
    float y{0};
    float width{0};
    float height{0};
    bool operator==(const DipRect&) const = default;
};

enum class SemanticRole {
    root,
    brand,
    navigation_group,
    navigation_item,
    status,
    metric_card,
    page_heading,
    page_body,
    section_heading,
    footer,
    recovery_banner,
    notice,
    action,
    slider,
    tooltip,
};

struct SliderInfo {
    int minimum{0};
    int maximum{100};
    int value{0};
    int small_step{1};
    int large_step{10};
    std::wstring unit;

    bool operator==(const SliderInfo&) const = default;
};

struct SemanticNode {
    std::string id;
    SemanticRole role{SemanticRole::root};
    DipRect bounds;
    std::wstring text;
    std::optional<Destination> destination;
    bool selected{false};
    bool focused{false};
    bool enabled{true};
    std::optional<std::string> action_id;
    std::optional<SliderInfo> slider;
    bool attention{false};
    float opacity{1.0F};
    float hover{0.0F};
    float interaction{0.0F};
    bool pressed{false};
};

struct ShellLayoutResult {
    DipRect root;
    DipRect header;
    DipRect sidebar;
    DipRect status_strip;
    DipRect metrics_strip;
    DipRect content;
    DipRect footer;
    float scroll_offset{0};
    float scroll_extent{0};
    float startup_progress{1.0F};
    float page_transition_progress{1.0F};
    std::optional<DipRect> navigation_indicator;
    float update_glow_progress{1.0F};
    float exit_progress{0.0F};
    std::vector<SemanticNode> nodes;
};

[[nodiscard]] float pixels_to_dips(float pixels, float dpi) noexcept;
[[nodiscard]] float dips_to_pixels(float dips, float dpi) noexcept;
[[nodiscard]] bool contains(const DipRect& rectangle, DipPoint point) noexcept;
[[nodiscard]] bool intersects(const DipRect& left, const DipRect& right) noexcept;
[[nodiscard]] ShellLayoutResult layout_shell(const UiModel& model,
                                             float width_dip,
                                             float height_dip);
[[nodiscard]] const SemanticNode* hit_test(const ShellLayoutResult& layout,
                                           DipPoint point) noexcept;
[[nodiscard]] std::optional<std::wstring> action_help_text(
    std::string_view action_id);
void set_hover_tooltip(ShellLayoutResult& layout,
                       const SemanticNode* target,
                       float opacity = 1.0F);

}  // namespace kf2::ui
