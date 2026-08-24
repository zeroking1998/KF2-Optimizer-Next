#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
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

    CHECK(action_help_text("game-open-config") ==
          L"Opens KF2's local configuration folder in File Explorer.");
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
    CHECK(node(dashboard, "status") != nullptr);
    CHECK(node(dashboard, "dashboard-quick-section") != nullptr);
    CHECK(node(dashboard, "dashboard-support-section") == nullptr);
    CHECK(node(dashboard, "dashboard-goals-section") != nullptr);
    CHECK(node(dashboard, "dashboard-adaptive-section") == nullptr);
    CHECK(node(dashboard, "dashboard-updates-section") != nullptr);
    CHECK(action(dashboard, "dashboard-launch") != nullptr);
    CHECK(action(dashboard, "dashboard-settings") == nullptr);
    CHECK(action(dashboard, "dashboard-overlay") == nullptr);
    CHECK(action(dashboard, "dashboard-diagnostics") == nullptr);
    CHECK(action(dashboard, "dashboard-refresh") == nullptr);
    CHECK(action(dashboard, "game-select-install") != nullptr);
    CHECK(action(dashboard, "settings-animations") == nullptr);
    CHECK(action(dashboard, "settings-updates-automatic") != nullptr);
    CHECK(node(dashboard, "header-auto-updates") != nullptr);
    CHECK(node(dashboard, "header-auto-updates")->text == L"✓ UPDATE CHECK");
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
    std::array<DipRect, 5> navigation{};
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
    CHECK(nav_hit->destination == Destination::graphics);

    const auto compact_dashboard = layout_shell(model, 800, 520);
    CHECK(action(compact_dashboard, "header-update-check") != nullptr);
    CHECK(action(compact_dashboard, "settings-updates-automatic") != nullptr);
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

    static_cast<void>(model.focus_destination(Destination::graphics));
    static_cast<void>(model.activate_focused());
    auto graphics_status = model.status();
    graphics_status.graphics_available = true;
    graphics_status.graphics_values = {
        L"Borderless", L"2560 × 1080", L"Ultra", L"Off", L"Off",
        L"Ultra", L"Ultra", L"Ultra", L"Ultra", L"16× Anisotropic",
        L"Ultra", L"On", L"On", L"High", L"On", L"HBAO+", L"On",
        L"On", L"On", L"On", L"Gibs and fluids"};
    graphics_status.graphics_aspect_ratio = L"64:27";
    model.set_status(graphics_status);
    const auto graphics = layout_shell(model, 1440, 900);
    CHECK(node(graphics, "page-body") == nullptr);
    CHECK(node(graphics, "graphics-foliage-detail") == nullptr);
    CHECK(node(graphics, "graphics-flex-section") == nullptr);
    CHECK(node(graphics, "graphics-display-info") != nullptr);
    CHECK(node(graphics, "graphics-display-info")->text.find(L"Gamma") ==
          std::wstring::npos);
    const auto* film_grain = node(graphics, "graphics-film-grain-slider");
    CHECK(film_grain != nullptr);
    CHECK(film_grain->text == L"Film grain intensity");
    CHECK(film_grain->slider.has_value());
    CHECK(film_grain->slider->minimum == 0);
    CHECK(film_grain->slider->maximum == 200);
    CHECK(film_grain->slider->small_step == 5);
    CHECK(film_grain->slider->unit == L"%");
    CHECK(action(graphics, "graphics-display") != nullptr);
    CHECK(action(graphics, "graphics-variable-frame-rate") != nullptr);
    CHECK(std::abs(action(graphics, "graphics-display")->bounds.y -
                   action(graphics, "graphics-variable-frame-rate")->bounds.y) <
          0.01F);
    CHECK(action(graphics, "graphics-light-shafts") != nullptr);
    CHECK(action(graphics, "graphics-flex") != nullptr);
    CHECK(std::abs(action(graphics, "graphics-light-shafts")->bounds.y -
                   action(graphics, "graphics-flex")->bounds.y) < 0.01F);

    model.set_notice({NoticeSeverity::info, L"GRAPHICS_APPLIED",
                      L"KF2 video settings were applied and verified.", L""});
    const auto graphics_notice_top = layout_shell(model, 1440, 900);
    const auto* top_notice = node(graphics_notice_top, "notice");
    const auto* top_heading = node(graphics_notice_top, "page-heading");
    CHECK(top_notice != nullptr);
    CHECK(top_heading != nullptr);
    CHECK(!intersects(top_notice->bounds, top_heading->bounds));
    CHECK(std::abs((top_heading->bounds.y - top_notice->bounds.y) - 52.0F) <
          0.01F);
    model.set_scroll_extent(graphics_notice_top.scroll_extent);
    for (const float requested_scroll : {20.0F, 51.0F, 96.0F}) {
        static_cast<void>(model.set_scroll(requested_scroll));
        const auto scrolled_notice = layout_shell(model, 1440, 900);
        const auto* notice = node(scrolled_notice, "notice");
        const auto* heading = node(scrolled_notice, "page-heading");
        CHECK(notice != nullptr);
        CHECK(heading != nullptr);
        CHECK(!intersects(notice->bounds, heading->bounds));
        CHECK(std::abs((heading->bounds.y - notice->bounds.y) - 52.0F) <
              0.01F);
    }
    model.clear_notice();
    model.set_scroll_extent(0.0F);
    static_cast<void>(model.set_scroll(0.0F));

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
    CHECK(action(overlay, "overlay-toggle")->selected);
    CHECK(action(overlay, "overlay-position") != nullptr);
    CHECK(action(overlay, "overlay-scale-reset") != nullptr);
    CHECK(action(overlay, "overlay-show-memory") != nullptr);
    CHECK(action(overlay, "overlay-show-memory")->selected);
    CHECK(node(overlay, "overlay-main-section") != nullptr);
    CHECK(node(overlay, "overlay-metrics-section") != nullptr);

    static_cast<void>(model.focus_destination(Destination::advanced));
    static_cast<void>(model.activate_focused());
    auto advanced_status = model.status();
    advanced_status.advanced_available = true;
    advanced_status.advanced_values.fill(L"Off");
    advanced_status.advanced_values[0] = L"On";
    advanced_status.advanced_values[10] = L"4×";
    advanced_status.advanced_values[11] = L"Full";
    advanced_status.advanced_screen_percentage = 110;
    advanced_status.advanced_particle_percentage = 85;
    advanced_status.advanced_decal_lifetime = 45;
    advanced_status.advanced_dirty = true;
    model.set_status(advanced_status);
    const auto advanced = layout_shell(model, 1440, 900);
    CHECK(node(advanced, "advanced-engine-section") != nullptr);
    CHECK(node(advanced, "advanced-rendering-section") != nullptr);
    CHECK(node(advanced, "advanced-effects-section") != nullptr);
    CHECK(node(advanced, "advanced-save-section") != nullptr);
    CHECK(action(advanced, "advanced-one-frame-thread-lag") != nullptr);
    CHECK(action(advanced, "advanced-one-frame-thread-lag")->selected);
    CHECK(action(advanced, "advanced-texture-streaming") != nullptr);
    CHECK(action(advanced, "advanced-temporal-aa") == nullptr);
    CHECK(action(advanced, "advanced-max-multisamples")->text.find(L"4×") !=
          std::wstring::npos);
    CHECK(action(advanced, "advanced-gore-level")->text.find(L"Full") !=
          std::wstring::npos);
    CHECK(action(advanced, "settings-adaptive-emergency") == nullptr);
    const auto* screen = node(advanced, "advanced-screen-percentage-slider");
    const auto* particles = node(advanced, "advanced-particle-percentage-slider");
    const auto* decals = node(advanced, "advanced-decal-lifetime-slider");
    CHECK(screen && screen->slider && screen->slider->minimum == 50 &&
          screen->slider->maximum == 200 && screen->slider->value == 110);
    CHECK(particles && particles->slider && particles->slider->minimum == 0 &&
          particles->slider->maximum == 100 && particles->slider->value == 85);
    CHECK(decals && decals->slider && decals->slider->minimum == 0 &&
          decals->slider->maximum == 120 && decals->slider->value == 45);
    CHECK(action(advanced, "advanced-apply") != nullptr);
    CHECK(action(advanced, "advanced-apply")->enabled);
    CHECK(action(advanced, "advanced-reset") != nullptr);
    CHECK(action(advanced, "advanced-reset")->text == L"RESET TO DEFAULTS");
    CHECK(action(advanced, "advanced-reset")->enabled);
    CHECK(action(advanced, "diagnostics-full-check") == nullptr);
    CHECK(action(advanced, "settings-restore-config") == nullptr);

    static_cast<void>(model.focus_destination(Destination::diagnostics));
    static_cast<void>(model.activate_focused());
    const auto diagnostics = layout_shell(model, 1440, 900);
    CHECK(action(diagnostics, "diagnostics-flex-audit") == nullptr);
    CHECK(action(diagnostics, "diagnostics-flex-install") == nullptr);
    CHECK(action(diagnostics, "diagnostics-flex-restore") != nullptr);
    CHECK(action(diagnostics, "diagnostics-repair-package") != nullptr);
    CHECK(action_help_text("diagnostics-repair-package").has_value());
    CHECK(action(diagnostics, "settings-finetuning") == nullptr);
    CHECK(node(diagnostics, "diagnostics-check-section") != nullptr);
    CHECK(node(diagnostics, "diagnostics-recovery-section") != nullptr);
    CHECK(node(diagnostics, "diagnostics-reports-section") != nullptr);

    static_cast<void>(model.focus_destination(Destination::dashboard));
    static_cast<void>(model.activate_focused());
    auto home_status = model.status();
    home_status.mode = L"Adaptive / Automatic";
    home_status.game_detected = true;
    home_status.update_check_completed = true;
    home_status.update_status = L"The installed version is current.";
    model.set_status(home_status);
    const auto home = layout_shell(model, 1440, 900);
    CHECK(node(home, "status") != nullptr);
    CHECK(node(home, "status")->text.find(L"Target 60 FPS") !=
          std::wstring::npos);
    CHECK(node(home, "status")->text.find(L"Maximum corpses 20") !=
          std::wstring::npos);

    home_status.active_target_fps = 240;
    home_status.active_corpse_limit = 2000;
    model.set_status(home_status);
    const auto staged_target_home = layout_shell(model, 1440, 900);
    CHECK(node(staged_target_home, "status")->text.find(
              L"Target 240 FPS") != std::wstring::npos);
    CHECK(node(staged_target_home, "status")->text.find(
              L"60 next start") != std::wstring::npos);
    CHECK(node(staged_target_home, "status")->text.find(
              L"Maximum corpses 2000") != std::wstring::npos);
    CHECK(node(staged_target_home, "status")->text.find(
              L"20 next start") != std::wstring::npos);
    home_status.active_target_fps.reset();
    home_status.active_corpse_limit.reset();
    model.set_status(home_status);
    CHECK(node(home, "dashboard-updates-installed")->text.find(
              L"No newer version available") != std::wstring::npos);
    CHECK(action(home, "settings-mode-manual") == nullptr);
    CHECK(action(home, "settings-mode-adaptive") == nullptr);
    CHECK(action(home, "settings-animations") == nullptr);
    CHECK(action(home, "settings-advanced-toggle") == nullptr);
    CHECK(action(home, "settings-updates-automatic") != nullptr);
    CHECK(action(home, "settings-updates-check") == nullptr);
    CHECK(action(home, "settings-updates-install") == nullptr);
    CHECK(action(home, "settings-target-up") == nullptr);
    CHECK(action(home, "settings-corpses-up") == nullptr);

    const auto* target = node(home, "settings-target-slider");
    const auto* corpses = node(home, "settings-corpses-slider");
    const auto* gore = node(home, "settings-gore-slider");
    CHECK(target && target->slider && target->slider->minimum == 30 &&
          target->slider->maximum == 240 &&
          target->slider->small_step == 1);
    CHECK(corpses && corpses->slider && corpses->slider->minimum == 4 &&
          corpses->slider->maximum == 2000);
    CHECK(gore == nullptr);
    CHECK(node(home, "settings-adaptive-minimum-slider") == nullptr);
    CHECK(node(home, "settings-adaptive-maximum-slider") == nullptr);
    CHECK(node(home, "settings-adaptive-headroom-slider") == nullptr);
    CHECK(node(home, "settings-flex-slider") == nullptr);

    auto checking_status = home_status;
    checking_status.update_checking = true;
    model.set_status(checking_status);
    const auto checking_updates = layout_shell(model, 1440, 900);
    CHECK(!action(checking_updates, "header-update-check")->enabled);
    CHECK(!action(checking_updates, "settings-updates-automatic")->enabled);
    CHECK(action(checking_updates, "header-update-check")->text ==
          L"CHECKING...");

    auto available_status = home_status;
    available_status.update_available = true;
    available_status.update_newer_version_known = true;
    available_status.update_prompt_visible = true;
    available_status.update_check_completed = true;
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
    CHECK(node(available_updates, "update-dialog-title") != nullptr);
    CHECK(action(available_updates, "settings-updates-install") != nullptr);
    CHECK(action(available_updates, "settings-updates-later") != nullptr);
    CHECK(action(available_updates, "settings-updates-ignore") != nullptr);

    auto cached_available_status = home_status;
    cached_available_status.update_newer_version_known = true;
    cached_available_status.update_prompt_visible = true;
    cached_available_status.update_available_version = L"0.0.4-alpha";
    model.set_status(cached_available_status);
    const auto cached_available = layout_shell(model, 1440, 900);
    CHECK(action(cached_available, "header-update-check") != nullptr);
    CHECK(action(cached_available, "header-update-check")->attention);
    CHECK(action(cached_available, "header-update-check")->text ==
          L"UPDATE AVAILABLE");
    CHECK(action(cached_available, "settings-updates-check") != nullptr);
    CHECK(action(cached_available, "settings-updates-install") == nullptr);

    const std::array<const ShellLayoutResult*, 7> tooltip_layouts{
        &home, &graphics, &overlay, &advanced, &diagnostics,
        &available_updates, &cached_available};
    for (const auto* tooltip_layout : tooltip_layouts) {
        for (const auto& item : tooltip_layout->nodes) {
            if ((item.role != SemanticRole::action &&
                 item.role != SemanticRole::slider) ||
                !item.action_id) {
                continue;
            }
            const auto help = action_help_text(*item.action_id);
            if (!help) {
                std::cerr << "missing tooltip for " << *item.action_id << '\n';
            }
            CHECK(help.has_value());
            CHECK(help->size() >= 24);
        }
    }
    std::set<std::string> visible_action_ids;
    for (const auto* candidate_layout : tooltip_layouts) {
        for (const auto& item : candidate_layout->nodes) {
            if (item.role == SemanticRole::action && item.action_id) {
                visible_action_ids.insert(*item.action_id);
            }
        }
    }
    for (const auto& binding : kf2::app::runtime::action_bindings()) {
        if (!visible_action_ids.contains(std::string{binding.name})) {
            std::cerr << "binding has no visible control: " << binding.name
                      << '\n';
        }
        CHECK(visible_action_ids.contains(std::string{binding.name}));
        const auto help = action_help_text(binding.name);
        if (!help) {
            std::cerr << "missing contract tooltip for " << binding.name
                      << '\n';
        }
        CHECK(help.has_value());
    }
    for (const auto& control : kf2::app::runtime::control_definitions()) {
        const auto help = action_help_text(control.name);
        if (!help) {
            std::cerr << "missing control tooltip for " << control.name
                      << '\n';
        }
        CHECK(help.has_value());
    }
    CHECK(action_help_text("graphics-flex")->find(L"Adaptive never") !=
          std::wstring::npos);
    CHECK(action_help_text("graphics-film-grain-slider")->find(L"0%") !=
          std::wstring::npos);
    CHECK(action_help_text("advanced-one-frame-thread-lag")->find(
              L"input delay") != std::wstring::npos);
    CHECK(action_help_text("advanced-gore-level")->find(
              L"memory use") != std::wstring::npos);
    CHECK(action_help_text("settings-updates-ignore")->find(
              L"displayed version") != std::wstring::npos);
    for (const auto& binding : kf2::app::runtime::action_bindings()) {
        const auto help = action_help_text(binding.name);
        CHECK(help.has_value());
        std::wstring normalized = *help;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](wchar_t value) {
                           return static_cast<wchar_t>(std::towlower(value));
        });
        CHECK(normalized.find(L"manual") == std::wstring::npos);
        CHECK(normalized.find(L"staged") == std::wstring::npos);
        CHECK(normalized.find(L"until apply") == std::wstring::npos);
    }

    model.set_status(home_status);
    for (const auto& item : home.nodes) {
        if (item.role != SemanticRole::action &&
            item.role != SemanticRole::slider) {
            continue;
        }
        if (item.id.starts_with("header-")) continue;
        CHECK(item.bounds.x >= home.content.x);
        CHECK(item.bounds.x + item.bounds.width <=
              home.content.x + home.content.width + 0.01F);
    }

    model.set_status(available_status);
    const auto compact = layout_shell(model, 800, 520);
    CHECK(compact.scroll_extent > home.scroll_extent);
    model.set_scroll_extent(compact.scroll_extent);
    static_cast<void>(model.set_scroll(compact.scroll_extent));
    const auto scrolled = layout_shell(model, 800, 520);
    const auto* last = action(scrolled, "settings-updates-later");
    CHECK(last != nullptr);
    CHECK(last->bounds.y + last->bounds.height <=
          scrolled.content.y + scrolled.content.height + 0.01F);

    constexpr std::array<DipRect, 4> viewports{{
        {0, 0, 800, 520}, {0, 0, 960, 600},
        {0, 0, 1280, 720}, {0, 0, 1440, 900}}};
    for (int variant = 0; variant < 2; ++variant) {
        auto matrix_status = model.status();
        matrix_status.mode = L"Adaptive / Automatic";
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
                    const bool header_action = item.id.starts_with("header-");
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
                for (const auto& item : top.nodes) {
                    const bool interactive =
                        item.role == SemanticRole::navigation_item ||
                        item.role == SemanticRole::action ||
                        item.role == SemanticRole::slider;
                    if (!interactive || !item.enabled) continue;
                    const DipRect clip = item.role == SemanticRole::navigation_item
                        ? top.sidebar
                        : item.id.starts_with("header-") ? top.header
                                                         : top.content;
                    if (item.bounds.x < clip.x || item.bounds.y < clip.y ||
                        item.bounds.x + item.bounds.width >
                            clip.x + clip.width + 0.01F ||
                        item.bounds.y + item.bounds.height >
                            clip.y + clip.height + 0.01F) {
                        continue;
                    }
                    const DipPoint center{
                        item.bounds.x + item.bounds.width / 2.0F,
                        item.bounds.y + item.bounds.height / 2.0F};
                    const auto* hit = hit_test(top, center);
                    CHECK(hit != nullptr);
                    CHECK(hit->id == item.id);
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
