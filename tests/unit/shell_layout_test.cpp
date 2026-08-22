#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

#include "app/runtime/action_contract.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/theme.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

const kf2::ui::SemanticNode* action(
    const kf2::ui::ShellLayoutResult& layout, std::string_view id) {
    const auto found = std::find_if(
        layout.nodes.begin(), layout.nodes.end(), [&](const auto& node) {
            return node.action_id && *node.action_id == id;
        });
    return found == layout.nodes.end() ? nullptr : &*found;
}

const kf2::ui::SemanticNode* node(
    const kf2::ui::ShellLayoutResult& layout, std::string_view id) {
    const auto found = std::find_if(
        layout.nodes.begin(), layout.nodes.end(), [&](const auto& candidate) {
            return candidate.id == id;
        });
    return found == layout.nodes.end() ? nullptr : &*found;
}

}  // namespace

int main() {
    using namespace kf2::ui;
    UiModel model;
    model.set_state_path(L"C:\\Portable\\Data");
    model.set_build_identity(L"0.0.2-alpha+test");

    const auto dashboard = layout_shell(model, 1440, 900);
    CHECK((dashboard.header == DipRect{0, 0, 1440, 72}));
    CHECK((dashboard.status_strip == DipRect{0, 72, 1440, 34}));
    CHECK((dashboard.metrics_strip == DipRect{0, 106, 1440, 100}));
    CHECK((dashboard.sidebar == DipRect{0, 206, 210, 694}));
    CHECK((dashboard.footer == DipRect{0, 900, 1440, 0}));
    CHECK(node(dashboard, "footer") == nullptr);
    CHECK(node(dashboard, "dashboard-quick-section") != nullptr);
    CHECK(node(dashboard, "dashboard-support-section") != nullptr);
    CHECK(action(dashboard, "dashboard-launch") != nullptr);
    CHECK(action(dashboard, "dashboard-settings") != nullptr);
    CHECK(action(dashboard, "dashboard-overlay") != nullptr);
    CHECK(action(dashboard, "dashboard-diagnostics") != nullptr);
    CHECK(action(dashboard, "dashboard-refresh") == nullptr);
    CHECK(action(dashboard, "game-select-install") != nullptr);
    CHECK(action(dashboard, "header-launch") == nullptr);
    CHECK(action(dashboard, "header-update-check") != nullptr);
    CHECK(action(dashboard, "header-repair") != nullptr);
    CHECK(!action(dashboard, "header-update-check")->attention);
    CHECK(action(dashboard, "header-guide") == nullptr);
    CHECK(action(dashboard, "dashboard-guide") == nullptr);
    CHECK(std::count_if(dashboard.nodes.begin(), dashboard.nodes.end(),
                        [](const auto& item) {
                            return item.role == SemanticRole::metric_card;
                         }) == 4);
    CHECK(node(dashboard, "metric-2") != nullptr);
    CHECK(node(dashboard, "metric-2")->text.find(L"GAME LOAD") !=
          std::wstring::npos);

    std::set<std::string> ids;
    std::array<DipRect, 4> navigation{};
    std::size_t navigation_index = 0;
    for (const auto& item : dashboard.nodes) {
        CHECK(ids.insert(item.id).second);
        if (item.role == SemanticRole::navigation_item) {
            CHECK(navigation_index < navigation.size());
            navigation[navigation_index++] = item.bounds;
        }
    }
    CHECK(navigation_index == navigation.size());
    for (std::size_t index = 1; index < navigation.size(); ++index) {
        CHECK(!intersects(navigation[index - 1], navigation[index]));
    }
    const DipPoint nav_point{navigation[1].x + navigation[1].width / 2.0F,
                             navigation[1].y + navigation[1].height / 2.0F};
    const auto* nav_hit = hit_test(dashboard, nav_point);
    CHECK(nav_hit != nullptr);
    CHECK(nav_hit->destination == Destination::settings);

    const auto compact_dashboard = layout_shell(model, 800, 520);
    CHECK(action(compact_dashboard, "header-launch") == nullptr);
    CHECK(action(compact_dashboard, "header-update-check") != nullptr);
    CHECK(action(compact_dashboard, "header-repair") != nullptr);
    CHECK(node(compact_dashboard, "navigation-main-group") == nullptr);
    CHECK(node(compact_dashboard, "navigation-tools-group") == nullptr);

    constexpr std::array<float, 5> dpis{96, 120, 144, 168, 192};
    for (const float dpi : dpis) {
        const auto layout = layout_shell(
            model, pixels_to_dips(dips_to_pixels(1440.0F, dpi), dpi),
            pixels_to_dips(dips_to_pixels(900.0F, dpi), dpi));
        CHECK(layout.content.width > 0.0F);
        CHECK(layout.content.height > 0.0F);
    }

    auto adaptive_status = model.status();
    adaptive_status.mode = L"Adaptive / Automatic";
    model.set_status(adaptive_status);
    static_cast<void>(model.focus_destination(Destination::optimizer));
    static_cast<void>(model.activate_focused());
    const auto optimizer = layout_shell(model, 1440, 900);
    CHECK(action(optimizer, "optimizer-preview") != nullptr);
    CHECK(action(optimizer, "optimizer-apply") != nullptr);
    CHECK(!action(optimizer, "optimizer-apply")->enabled);
    CHECK(action(optimizer, "optimizer-manual-load") == nullptr);
    CHECK(action(optimizer, "optimizer-open-settings") != nullptr);
    CHECK(action_help_text("optimizer-apply").has_value());
    auto tooltip_layout = optimizer;
    const SemanticNode preview_copy = *action(tooltip_layout, "optimizer-preview");
    set_hover_tooltip(tooltip_layout, &preview_copy);
    CHECK(std::any_of(tooltip_layout.nodes.begin(), tooltip_layout.nodes.end(),
                      [](const auto& item) {
                          return item.role == SemanticRole::tooltip;
                      }));
    set_hover_tooltip(tooltip_layout, nullptr);

    adaptive_status.mode = L"Manual / Fixed";
    model.set_status(adaptive_status);
    const auto manual_optimizer = layout_shell(model, 1440, 900);
    CHECK(action(manual_optimizer, "optimizer-manual-load") == nullptr);
    CHECK(action(manual_optimizer, "optimizer-preview") != nullptr);

    static_cast<void>(model.focus_destination(Destination::game));
    static_cast<void>(model.activate_focused());
    auto game = layout_shell(model, 1440, 900);
    CHECK(action(game, "game-launch") != nullptr);
    CHECK(!action(game, "game-launch")->enabled);
    auto game_status = model.status();
    game_status.game_detected = true;
    model.set_status(game_status);
    game = layout_shell(model, 1440, 900);
    CHECK(action(game, "game-launch")->enabled);
    CHECK(action(game, "game-launch")->text == L"LAUNCH KF2 ADAPTIVELY");
    CHECK(action(game, "game-offline-telemetry") == nullptr);

    static_cast<void>(model.focus_destination(Destination::overlay));
    static_cast<void>(model.activate_focused());
    const auto overlay = layout_shell(model, 1440, 900);
    const auto* scale = node(overlay, "overlay-scale-slider");
    CHECK(scale != nullptr);
    CHECK(scale->role == SemanticRole::slider);
    CHECK(scale->slider.has_value());
    CHECK(scale->slider->minimum == 60);
    CHECK(scale->slider->maximum == 200);
    CHECK(action(overlay, "overlay-toggle") != nullptr);
    CHECK(action(overlay, "overlay-position") != nullptr);
    CHECK(action(overlay, "overlay-scale-reset") != nullptr);
    CHECK(action(overlay, "overlay-show-memory") != nullptr);
    CHECK(node(overlay, "overlay-main-section") != nullptr);
    CHECK(node(overlay, "overlay-metrics-section") != nullptr);

    static_cast<void>(model.focus_destination(Destination::diagnostics));
    static_cast<void>(model.activate_focused());
    const auto diagnostics = layout_shell(model, 1440, 900);
    CHECK(action(diagnostics, "diagnostics-flex-audit") == nullptr);
    CHECK(action(diagnostics, "diagnostics-flex-install") == nullptr);
    CHECK(action(diagnostics, "diagnostics-flex-restore") != nullptr);
    CHECK(action(diagnostics, "diagnostics-repair-package") != nullptr);
    CHECK(action_help_text("diagnostics-repair-package").has_value());
    CHECK(action(diagnostics, "diagnostics-auto-repair") == nullptr);
    CHECK(action(diagnostics, "settings-finetuning") == nullptr);
    CHECK(action_help_text("diagnostics-auto-repair").has_value());
    CHECK(node(diagnostics, "diagnostics-check-section") != nullptr);
    CHECK(node(diagnostics, "diagnostics-recovery-section") != nullptr);
    CHECK(node(diagnostics, "diagnostics-reports-section") != nullptr);

    static_cast<void>(model.focus_destination(Destination::settings));
    static_cast<void>(model.activate_focused());
    auto settings_status = model.status();
    settings_status.mode = L"Adaptive / Automatic";
    settings_status.advanced_settings_visible = false;
    model.set_status(settings_status);
    const auto settings = layout_shell(model, 1440, 900);
    CHECK(action(settings, "settings-mode-manual") == nullptr);
    CHECK(action(settings, "settings-mode-adaptive") == nullptr);
    CHECK(node(settings, "settings-adaptive-active-section") == nullptr);
    CHECK(action(settings, "settings-advanced-toggle") == nullptr);
    CHECK(action(settings, "settings-adaptive-aggressiveness") == nullptr);
    CHECK(action(settings, "settings-adaptive-flex") == nullptr);
    CHECK(action(settings, "settings-animations") != nullptr);
    CHECK(action(settings, "settings-guide-reset") == nullptr);
    CHECK(action(settings, "settings-finetuning") == nullptr);
    CHECK(node(settings, "settings-updates-section") != nullptr);
    CHECK(action(settings, "settings-updates-automatic") != nullptr);
    CHECK(action(settings, "settings-updates-check") == nullptr);
    CHECK(action(settings, "settings-updates-install") == nullptr);
    CHECK(action(settings, "settings-target-up") == nullptr);
    CHECK(action(settings, "settings-corpses-up") == nullptr);

    const auto* target = node(settings, "settings-target-slider");
    const auto* corpses = node(settings, "settings-corpses-slider");
    const auto* gore = node(settings, "settings-gore-slider");
    CHECK(target && target->slider && target->slider->minimum == 30 &&
          target->slider->maximum == 240 &&
          target->slider->small_step == 1);
    CHECK(corpses && corpses->slider && corpses->slider->minimum == 4 &&
          corpses->slider->maximum == 2000);
    CHECK(gore == nullptr);
    CHECK(node(settings, "settings-adaptive-minimum-slider") == nullptr);
    CHECK(node(settings, "settings-adaptive-maximum-slider") == nullptr);
    CHECK(node(settings, "settings-adaptive-headroom-slider") == nullptr);
    CHECK(node(settings, "settings-flex-slider") == nullptr);

    auto checking_status = settings_status;
    checking_status.update_checking = true;
    model.set_status(checking_status);
    const auto checking_updates = layout_shell(model, 1440, 900);
    CHECK(!action(checking_updates, "header-update-check")->enabled);
    CHECK(action(checking_updates, "header-update-check")->text ==
          L"CHECKING...");

    auto available_status = settings_status;
    available_status.update_available = true;
    available_status.update_installable = true;
    available_status.update_available_version = L"0.0.4-alpha";
    available_status.update_published_at = L"2026-08-23";
    available_status.update_download_size = L"5.0 MiB";
    available_status.update_changelog =
        L"WHAT'S NEW\n- Portable update.\n\nBUG FIXES\n- Rollback.";
    model.set_status(available_status);
    const auto available_updates = layout_shell(model, 1440, 900);
    CHECK(action(available_updates, "header-update-check") == nullptr);
    CHECK(action(available_updates, "header-update-install") != nullptr);
    CHECK(action(available_updates, "header-update-install")->attention);
    CHECK(action(available_updates, "settings-updates-install") == nullptr);
    CHECK(action(available_updates, "settings-updates-later") != nullptr);

    model.set_status(settings_status);
    for (const auto& item : settings.nodes) {
        if (item.role != SemanticRole::action &&
            item.role != SemanticRole::slider) {
            continue;
        }
        if (item.action_id && item.action_id->starts_with("header-")) continue;
        CHECK(item.bounds.x >= settings.content.x);
        CHECK(item.bounds.x + item.bounds.width <=
              settings.content.x + settings.content.width + 0.01F);
    }

    settings_status.advanced_settings_visible = true;
    model.set_status(settings_status);
    const auto advanced_settings = layout_shell(model, 1440, 900);
    CHECK(action(advanced_settings, "settings-advanced-toggle") == nullptr);
    CHECK(action(advanced_settings, "settings-adaptive-aggressiveness") == nullptr);
    CHECK(action(advanced_settings, "settings-adaptive-online") == nullptr);
    CHECK(node(advanced_settings, "settings-adaptive-minimum-slider") == nullptr);
    CHECK(node(advanced_settings, "settings-adaptive-maximum-slider") == nullptr);
    CHECK(node(advanced_settings, "settings-adaptive-headroom-slider") == nullptr);
    CHECK(advanced_settings.scroll_extent == settings.scroll_extent);

    settings_status.advanced_settings_visible = false;
    model.set_status(settings_status);
    const auto compact = layout_shell(model, 800, 520);
    CHECK(compact.scroll_extent > settings.scroll_extent);
    model.set_scroll_extent(compact.scroll_extent);
    static_cast<void>(model.set_scroll(compact.scroll_extent));
    const auto scrolled = layout_shell(model, 800, 520);
    const auto* last = action(scrolled, "settings-updates-automatic");
    CHECK(last != nullptr);
    CHECK(last->bounds.y + last->bounds.height <=
          scrolled.content.y + scrolled.content.height + 0.01F);

    constexpr std::array<DipRect, 4> viewports{{
        {0, 0, 800, 520}, {0, 0, 960, 600},
        {0, 0, 1280, 720}, {0, 0, 1440, 900}}};
    for (int variant = 0; variant < 2; ++variant) {
        auto matrix_status = model.status();
        matrix_status.mode = L"Adaptive / Automatic";
        matrix_status.advanced_settings_visible = variant == 1;
        model.set_status(matrix_status);
        for (const auto destination : kDestinations) {
            static_cast<void>(model.focus_destination(destination));
            static_cast<void>(model.activate_focused());
            for (const auto viewport : viewports) {
                model.set_scroll_extent(0.0F);
                static_cast<void>(model.set_scroll(0.0F));
                const auto top = layout_shell(
                    model, viewport.width, viewport.height);
                CHECK(top.content.width > 0.0F);
                CHECK(top.content.height > 0.0F);
                for (const auto& item : top.nodes) {
                    if (!item.action_id) continue;
                    if (item.role == SemanticRole::slider) {
                        CHECK(kf2::app::runtime::find_control(*item.action_id) != nullptr);
                    } else if (item.role == SemanticRole::action) {
                        CHECK(kf2::app::runtime::parse_action(*item.action_id).has_value());
                    }
                }

                std::set<std::string> matrix_ids;
                const SemanticNode* bottommost = nullptr;
                float bottommost_edge = -1.0F;
                for (const auto& item : top.nodes) {
                    CHECK(matrix_ids.insert(item.id).second);
                    if (item.role == SemanticRole::navigation_item) {
                        CHECK(item.bounds.y >= top.sidebar.y - 0.01F);
                        CHECK(item.bounds.y + item.bounds.height <=
                              top.sidebar.y + top.sidebar.height + 0.01F);
                    }
                    const bool header_action = item.action_id &&
                        item.action_id->starts_with("header-");
                    const bool page_interactive =
                        (item.role == SemanticRole::action && !header_action) ||
                        item.role == SemanticRole::slider;
                    if (!page_interactive) continue;
                    CHECK(item.bounds.x >= top.content.x - 0.01F);
                    CHECK(item.bounds.x + item.bounds.width <=
                          top.content.x + top.content.width + 0.01F);
                    const float edge = item.bounds.y + item.bounds.height;
                    if (edge > bottommost_edge) {
                        bottommost_edge = edge;
                        bottommost = &item;
                    }
                }
                CHECK(bottommost != nullptr);
                const std::string bottommost_id = bottommost->id;
                model.set_scroll_extent(top.scroll_extent);
                static_cast<void>(model.set_scroll(top.scroll_extent));
                const auto bottom = layout_shell(
                    model, viewport.width, viewport.height);
                CHECK(std::abs(bottom.scroll_extent - top.scroll_extent) <
                      0.01F);
                const auto* visible_last = node(bottom, bottommost_id);
                CHECK(visible_last != nullptr);
                CHECK(visible_last->bounds.y >= bottom.content.y - 0.01F);
                CHECK(visible_last->bounds.y + visible_last->bounds.height <=
                      bottom.content.y + bottom.content.height + 0.01F);
            }
        }
    }

    const auto light = resolve_theme({.dark = false});
    const auto dark = resolve_theme({.dark = true});
    CHECK(contrast_ratio(light.text, light.background) >= 4.5);
    CHECK(contrast_ratio(dark.text, dark.background) >= 4.5);
    CHECK(!resolve_theme({.reduced_motion = true}).animations_enabled);
    const auto high_contrast = resolve_theme({
        .high_contrast = true,
        .dark = true,
        .system_background = 0xFF010203,
        .system_text = 0xFFF0F1F2,
        .system_accent = 0xFF00FF00,
    });
    CHECK(high_contrast.background == 0xFF010203);
    CHECK(high_contrast.text == 0xFFF0F1F2);
    CHECK(high_contrast.accent == 0xFF00FF00);
    return EXIT_SUCCESS;
}
