#include "kf2/ui/shell_layout.hpp"

#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace kf2::ui {
namespace {

constexpr float kHeaderHeight = 72.0F;
constexpr float kStatusHeight = 34.0F;
constexpr float kMetricsHeight = 100.0F;
constexpr float kSidebarWidth = 210.0F;
constexpr float kFooterHeight = 0.0F;
constexpr float kActionGap = 12.0F;
constexpr float kActionHeight = 44.0F;
constexpr float kActionStride = kActionHeight + kActionGap;
constexpr float kTooltipHeight = 64.0F;
constexpr float kSliderHeight = 88.0F;

std::wstring status_text(const UiModel& model) {
    const auto& status = model.status();
    if (!status.game_detected) {
        return L"Choose your Killing Floor 2 folder to get started";
    }
    const std::wstring mode = status.mode == L"Adaptive / Automatic"
        ? (status.adaptive_optimization_enabled ? L"Adaptive on"
                                                : L"Adaptive off")
        : status.mode;
    const int configured_target = model.presented_target_fps();
    const int displayed_target = status.active_target_fps.value_or(
        configured_target);
    std::wstring target = L"Target " + std::to_wstring(displayed_target) +
                          L" FPS";
    if (status.active_target_fps &&
        *status.active_target_fps != configured_target) {
        target += L"   •   " + std::to_wstring(configured_target) +
                  L" next start";
    }
    const int configured_corpses = model.presented_corpse_limit();
    const int displayed_corpses = status.active_corpse_limit.value_or(
        configured_corpses);
    std::wstring corpses = L"Maximum corpses " +
                           std::to_wstring(displayed_corpses);
    if (status.active_corpse_limit &&
        *status.active_corpse_limit != configured_corpses) {
        corpses += L"   •   " + std::to_wstring(configured_corpses) +
                   L" next start";
    }
    std::wstring text = L"Ready   •   " + mode + L"   •   " + target +
                        L"   •   " + corpses;
    const auto presented_fps = model.presented_live_fps();
    if (presented_fps && model.presented_live_frame_time_ms()) {
        std::wostringstream live;
        live << L"   •   Live " << std::fixed << std::setprecision(1)
             << *presented_fps << L" FPS";
        text += live.str();
    }
    return text;
}

std::wstring metric(std::wstring_view label, std::optional<double> value,
                    int precision, std::wstring_view suffix) {
    std::wostringstream text;
    text << label << L'\n';
    if (value) {
        text << std::fixed << std::setprecision(precision) << *value << suffix;
    } else {
        text << L"—";
    }
    return text.str();
}

std::wstring corpse_metric(const UiModel& model) {
    const auto active = model.presented_live_active_corpses();
    const auto sleeping = model.presented_live_sleeping_corpses();
    std::wstring value = L"—";
    if (active || sleeping) {
        value = std::to_wstring(active.value_or(0)) + L" / " +
                std::to_wstring(sleeping.value_or(0));
    }
    return L"CORPSES\nMoving / sleeping: " + value;
}

std::wstring load_metric(const UiModel& model) {
    const auto cpu = model.presented_live_cpu_percent();
    const auto gpu = model.presented_live_gpu_percent();
    std::wostringstream text;
    text << L"GAME LOAD";
    if (model.status().game_gpu_name) {
        text << L'\n' << *model.status().game_gpu_name;
    }
    text << L"\nCPU ";
    if (cpu) {
        text << std::fixed << std::setprecision(0) << *cpu
             << L"%";
    } else {
        text << L"—";
    }
    text << L"  •  GPU ";
    if (gpu) {
        text << std::fixed << std::setprecision(0) << *gpu
             << L"%";
    } else {
        text << L"—";
    }
    return text.str();
}

}  // namespace

float pixels_to_dips(float pixels, float dpi) noexcept {
    return dpi > 0.0F ? pixels * 96.0F / dpi : 0.0F;
}

float dips_to_pixels(float dips, float dpi) noexcept {
    return dpi > 0.0F ? dips * dpi / 96.0F : 0.0F;
}

bool contains(const DipRect& rectangle, DipPoint point) noexcept {
    return point.x >= rectangle.x && point.y >= rectangle.y &&
           point.x < rectangle.x + rectangle.width &&
           point.y < rectangle.y + rectangle.height;
}

bool intersects(const DipRect& left, const DipRect& right) noexcept {
    return left.x < right.x + right.width && left.x + left.width > right.x &&
           left.y < right.y + right.height && left.y + left.height > right.y;
}

ShellLayoutResult layout_shell(const UiModel& model, float width_dip,
                               float height_dip) {
    const float width = std::max(0.0F, width_dip);
    const float height = std::max(0.0F, height_dip);
    const float footer_y = std::max(0.0F, height - kFooterHeight);
    const float sidebar_width = std::min(kSidebarWidth, width);
    const float main_top = std::min(
        footer_y, kHeaderHeight + kStatusHeight + kMetricsHeight);

    ShellLayoutResult result;
    result.root = {0, 0, width, height};
    result.header = {0, 0, width, std::min(kHeaderHeight, footer_y)};
    result.status_strip = {0, std::min(kHeaderHeight, footer_y), width,
                           std::min(kStatusHeight,
                               std::max(0.0F, footer_y - kHeaderHeight))};
    result.metrics_strip = {
        0, std::min(kHeaderHeight + kStatusHeight, footer_y), width,
        std::min(kMetricsHeight,
            std::max(0.0F, footer_y - kHeaderHeight - kStatusHeight))};
    result.sidebar = {0, main_top, sidebar_width,
                      std::max(0.0F, footer_y - main_top)};
    result.footer = {0, footer_y, width, std::min(kFooterHeight, height)};
    result.content = {sidebar_width + 16.0F, main_top + 12.0F,
                      std::max(0.0F, width - sidebar_width - 28.0F),
                      std::max(0.0F, footer_y - main_top - 24.0F)};

    const float preferred_action_width = 230.0F;
    const std::size_t maximum_columns = 3U;
    const auto available_columns = static_cast<std::size_t>(std::max(
        1.0, std::floor(static_cast<double>(result.content.width + kActionGap) /
                        static_cast<double>(preferred_action_width + kActionGap))));
    const std::size_t action_columns = std::max<std::size_t>(
        std::size_t{1}, std::min(maximum_columns, available_columns));
    const float scroll = std::max(0.0F, model.scroll_offset());
    result.scroll_offset = scroll;

    result.nodes.push_back({"root", SemanticRole::root, result.root,
                            L"KF2 Optimizer Next"});
    constexpr float header_action_gap = 8.0F;
    constexpr std::array<float, 3> header_action_widths{
        148.0F, 160.0F, 118.0F};
    const bool show_global_actions = width >= 760.0F &&
                                     result.header.height >= 64.0F;
    const float header_actions_width = show_global_actions
        ? std::accumulate(header_action_widths.begin(),
                          header_action_widths.end(), 0.0F) +
              header_action_gap *
                  static_cast<float>(header_action_widths.size() - 1)
        : 0.0F;
    const float header_actions_left = show_global_actions
        ? width - 12.0F - header_actions_width
        : width;
    result.nodes.push_back({"brand-mark", SemanticRole::brand,
                            {22.0F, 8.0F, 58.0F, 54.0F}, L"///"});
    std::wstring brand = L"KF2 OPTIMIZER";
    result.nodes.push_back({"brand", SemanticRole::brand,
                            {92.0F, 18.0F,
                             std::max(0.0F, header_actions_left - 108.0F), 36.0F},
                            std::move(brand)});

    if (show_global_actions) {
        const auto& status = model.status();
        const bool install_action = status.update_available;
        const std::string update_id = install_action
            ? "header-update-install" : "header-update-check";
        const std::wstring update_text = status.update_installing
            ? L"UPDATING..."
            : status.update_checking ? L"CHECKING..."
            : status.update_newer_version_known ? L"UPDATE AVAILABLE"
                                                : L"UPDATES";
        const bool update_enabled = !status.update_checking &&
            !status.update_installing &&
            (!install_action || status.update_installable);
        float x = header_actions_left;
        result.nodes.push_back({
            "header-update", SemanticRole::action,
            {x, 14.0F, header_action_widths[0], 42.0F}, update_text,
            std::nullopt, false, model.focused_action() == update_id,
            update_enabled, update_id, std::nullopt,
            status.update_newer_version_known});
        x += header_action_widths[0] + header_action_gap;
        result.nodes.push_back({
            "header-auto-updates", SemanticRole::action,
            {x, 14.0F, header_action_widths[1], 42.0F},
            status.automatic_update_checks
                ? L"✓ UPDATE CHECK" : L"UPDATE CHECK",
            std::nullopt, status.automatic_update_checks,
            model.focused_action() == "settings-updates-automatic",
            !status.update_checking && !status.update_installing,
            "settings-updates-automatic"});
        x += header_action_widths[1] + header_action_gap;
        result.nodes.push_back({
            "header-repair", SemanticRole::action,
            {x, 14.0F, header_action_widths[2], 42.0F}, L"REPAIR",
            std::nullopt, false,
            model.focused_action() == "header-repair",
            !status.update_installing, "header-repair"});
    }

    result.nodes.push_back({"status", SemanticRole::status, result.status_strip,
                            status_text(model)});

    constexpr std::size_t metric_count = 4;
    constexpr float metric_gap = 8.0F;
    constexpr float metric_margin = 12.0F;
    const float metric_width = std::max(
        0.0F, (width - metric_margin * 2.0F -
               metric_gap * static_cast<float>(metric_count - 1)) /
                  static_cast<float>(metric_count));
    const float metric_y = result.metrics_strip.y + 8.0F;
    const float metric_height = std::max(0.0F, result.metrics_strip.height - 16.0F);
    const std::array<std::wstring, metric_count> metric_texts{
        metric(L"LIVE FPS", model.presented_live_fps(), 1, L" FPS"),
        metric(L"FRAME TIME", model.presented_live_frame_time_ms(), 1, L" ms"),
        load_metric(model),
        corpse_metric(model)};
    for (std::size_t index = 0; index < metric_count; ++index) {
        result.nodes.push_back({
            "metric-" + std::to_string(index), SemanticRole::metric_card,
            {metric_margin + static_cast<float>(index) * (metric_width + metric_gap),
             metric_y, metric_width, metric_height}, metric_texts[index]});
    }

    const bool compact_navigation = result.sidebar.height < 360.0F;
    const float nav_height = compact_navigation ? 26.0F : 38.0F;
    const float nav_stride = nav_height + (compact_navigation ? 4.0F : 6.0F);
    float nav_cursor = result.sidebar.y + (compact_navigation ? 10.0F : 16.0F);
    for (std::size_t index = 0; index < kDestinations.size(); ++index) {
        const Destination destination = kDestinations[index];
        result.nodes.push_back({
            "nav-" + std::to_string(index), SemanticRole::navigation_item,
            {12.0F, nav_cursor,
             std::max(0.0F, sidebar_width - 24.0F), nav_height},
            std::wstring{destination_label(destination)}, destination,
            model.selected() == destination,
            !model.focused_action().has_value() &&
                model.focused_destination() == destination});
        nav_cursor += nav_stride;
    }

    const auto scrolling_nodes_begin = result.nodes.size();
    float banner_y = result.content.y;
    float banner_offset = 0.0F;
    if (model.recovery_required()) {
        result.nodes.push_back({"recovery", SemanticRole::recovery_banner,
                                {result.content.x, banner_y - scroll,
                                 result.content.width, 44.0F},
                                L"The previous session will be checked safely."});
        banner_y += 52.0F;
        banner_offset += 52.0F;
    }
    if (model.notice().has_value()) {
        result.nodes.push_back({"notice", SemanticRole::notice,
                                {result.content.x, banner_y - scroll,
                                 result.content.width, 44.0F},
                                model.notice()->message});
        banner_offset += 52.0F;
    }

    const auto page_nodes_begin = result.nodes.size();
    result.nodes.push_back({"page-heading", SemanticRole::page_heading,
                            {result.content.x, result.content.y + banner_offset - scroll,
                             result.content.width, 38.0F},
                            model.page_heading()});
    const auto page_body = model.page_body();
    if (!page_body.empty()) {
        result.nodes.push_back({
            "page-body", SemanticRole::page_body,
            {result.content.x,
             result.content.y + banner_offset + 42.0F - scroll,
             result.content.width, 52.0F}, page_body});
    }
    const float action_width = std::max(
        0.0F, (result.content.width - kActionGap *
            static_cast<float>(action_columns - 1)) /
            static_cast<float>(action_columns));
    std::size_t action_index = 0;
    const float page_action_base = page_body.empty() ? 58.0F : 108.0F;
    float grid_base = result.content.y + banner_offset + page_action_base;
    const auto add_action_at = [&](std::string id, std::wstring text,
                                   DipRect bounds, bool enabled = true,
                                   bool selected = false) {
        const std::string action_id = id;
        result.nodes.push_back({std::move(id), SemanticRole::action,
                                {bounds.x, bounds.y - scroll,
                                 bounds.width, bounds.height}, std::move(text),
                                std::nullopt, selected,
                                model.focused_action() == action_id, enabled,
                                action_id});
    };
    const auto add_action = [&](std::string id, std::wstring text,
                                bool enabled = true, bool selected = false) {
        const auto column = action_index % action_columns;
        const auto row = action_index / action_columns;
        ++action_index;
        add_action_at(std::move(id), std::move(text),
            {result.content.x + static_cast<float>(column) *
                 (action_width + kActionGap),
             grid_base + static_cast<float>(row) * kActionStride,
             action_width, kActionHeight}, enabled, selected);
    };
    const auto add_section = [&](std::string id, std::wstring text, float y,
                                 float height = 28.0F) {
        result.nodes.push_back({std::move(id), SemanticRole::section_heading,
                                {result.content.x, y - scroll,
                                 result.content.width, height}, std::move(text)});
    };
    const auto add_slider = [&](std::string id, std::wstring label, float y,
                                SliderInfo slider, bool enabled = true) {
        const std::string action_id = id;
        result.nodes.push_back({std::move(id), SemanticRole::slider,
                                {result.content.x, y - scroll,
                                 result.content.width, kSliderHeight},
                                std::move(label), std::nullopt, false,
                                model.focused_action() == action_id, enabled,
                                action_id, std::move(slider)});
    };
    if (model.selected() == Destination::dashboard) {
        float cursor = grid_base;
        const auto& status = model.status();
        if (status.update_prompt_visible) {
            add_section("update-dialog-title", L"UPDATE AVAILABLE", cursor);
            cursor += 34.0F;
            std::wstring release =
                L"Installed " + status.update_installed_version +
                L"  •  New " + status.update_available_version;
            if (status.update_available) {
                release += L"  •  Published " + status.update_published_at +
                           L"  •  " + status.update_download_size;
            } else {
                release += L"  •  Last checked " + status.update_last_check;
            }
            add_section("update-dialog-release", std::move(release), cursor,
                        34.0F);
            cursor += 42.0F;
            if (status.update_available && !status.update_changelog.empty()) {
                add_section("update-dialog-changelog",
                            status.update_changelog, cursor, 88.0F);
                cursor += 96.0F;
            } else if (!status.update_available) {
                add_section(
                    "update-dialog-cached-note",
                    L"Select Load update details to refresh the verified release information.",
                    cursor, 34.0F);
                cursor += 42.0F;
            }
            grid_base = cursor;
            action_index = 0;
            if (status.update_available) {
                add_action("settings-updates-install", L"INSTALL UPDATE",
                           status.update_installable, true);
            } else {
                add_action("settings-updates-check",
                           L"LOAD UPDATE DETAILS", true, true);
            }
            add_action("settings-updates-later", L"LATER",
                       !status.update_installing);
            add_action("settings-updates-ignore", L"DON'T SHOW AGAIN",
                       !status.update_installing);
            cursor = grid_base +
                static_cast<float>((action_index + action_columns - 1) /
                                   action_columns) * kActionStride + 12.0F;
        }
        add_section("dashboard-quick-section", L"START", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("dashboard-launch", L"LAUNCH KF2",
                   model.status().game_detected, true);
        add_action("game-select-install",
                   model.status().game_detected
                       ? L"CHANGE GAME FOLDER" : L"SELECT GAME FOLDER");
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("dashboard-goals-section", L"GOALS", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("settings-adaptive-toggle",
                   status.adaptive_optimization_enabled
                       ? L"✓ ADAPTIVE OPTIMIZATION: ON"
                       : L"ADAPTIVE OPTIMIZATION: OFF",
                   true, status.adaptive_optimization_enabled);
        cursor = grid_base + kActionStride + 4.0F;
        add_slider("settings-target-slider", L"Target FPS", cursor,
                   {optimizer::kTargetFpsMinimum,
                    optimizer::kTargetFpsMaximum,
                    model.status().target_fps, 1, 10, L" FPS"});
        cursor += kSliderHeight + 12.0F;
        add_slider("settings-corpses-slider", L"Maximum corpses",
                   cursor, {4, 2000, model.status().corpse_limit,
                             1, 50, L" corpses"});
        cursor += kSliderHeight + 12.0F;
        add_section("dashboard-updates-section", L"VERSION & UPDATES", cursor);
        cursor += 34.0F;
        const std::wstring availability = status.update_checking
            ? status.update_status
            : status.update_newer_version_known
                ? L"Available version: " + status.update_available_version
                : status.update_check_completed
                    ? L"No newer version available"
                    : status.update_status;
        add_section("dashboard-updates-installed",
                    L"Installed " + status.update_installed_version +
                        L"  •  " + availability +
                        L"  •  Last checked " +
                        status.update_last_check,
                    cursor);
    } else if (model.selected() == Destination::graphics) {
        const auto& status = model.status();
        const bool editable = status.graphics_available &&
                               !status.graphics_game_running;
        constexpr float graphics_action_gap = 10.0F;
        constexpr float graphics_action_stride =
            kActionHeight + graphics_action_gap;
        constexpr float graphics_section_advance = 28.0F;
        const std::size_t graphics_columns = std::max<std::size_t>(
            std::size_t{1}, std::min(std::size_t{4}, available_columns));
        const float graphics_action_width = std::max(
            0.0F, (result.content.width - graphics_action_gap *
                static_cast<float>(graphics_columns - 1)) /
                static_cast<float>(graphics_columns));
        const auto graphics_value = [&](std::size_t option) {
            return option < status.graphics_values.size() &&
                   !status.graphics_values[option].empty()
                ? status.graphics_values[option] : std::wstring{L"Unavailable"};
        };
        const auto graphics_action = [&](std::string id,
                                         std::wstring_view label,
                                         std::size_t option) {
            const auto column = action_index % graphics_columns;
            const auto row = action_index / graphics_columns;
            ++action_index;
            add_action_at(
                std::move(id),
                std::wstring{label} + L":  " + graphics_value(option),
                {result.content.x + static_cast<float>(column) *
                     (graphics_action_width + graphics_action_gap),
                 grid_base + static_cast<float>(row) * graphics_action_stride,
                 graphics_action_width, kActionHeight},
                editable);
        };
        float cursor = grid_base;
        add_section("graphics-basic-section", L"BASIC", cursor);
        cursor += graphics_section_advance;
        grid_base = cursor;
        action_index = 0;
        graphics_action("graphics-display", L"Display", 0);
        graphics_action("graphics-resolution", L"Resolution", 1);
        graphics_action("graphics-vsync", L"Vertical sync", 3);
        graphics_action("graphics-variable-frame-rate",
                        L"Variable frame rate", 4);
        cursor = grid_base +
            static_cast<float>((action_index + graphics_columns - 1) /
                               graphics_columns) * graphics_action_stride + 4.0F;
        add_section("graphics-display-info",
                    L"Aspect ratio:  " + status.graphics_aspect_ratio,
                    cursor);
        cursor += 26.0F;
        add_slider("graphics-film-grain-slider", L"Film grain intensity", cursor,
                   {0, 200, status.graphics_film_grain_percent,
                    5, 25, L"%"});
        cursor += kSliderHeight + 10.0F;

        add_section("graphics-quality-section", L"QUALITY", cursor);
        cursor += graphics_section_advance;
        grid_base = cursor;
        action_index = 0;
        graphics_action("graphics-overall-quality", L"Graphics quality", 2);
        graphics_action("graphics-environment-detail", L"Environment detail", 5);
        graphics_action("graphics-character-detail", L"Character detail", 6);
        graphics_action("graphics-fx", L"FX", 7);
        graphics_action("graphics-texture-resolution", L"Texture resolution", 8);
        graphics_action("graphics-texture-filtering", L"Texture filtering", 9);
        graphics_action("graphics-shadow-quality", L"Shadow quality", 10);
        graphics_action("graphics-realtime-reflections", L"Realtime reflections", 11);
        graphics_action("graphics-anti-aliasing", L"Anti-aliasing", 12);
        cursor = grid_base +
            static_cast<float>((action_index + graphics_columns - 1) /
                               graphics_columns) * graphics_action_stride + 4.0F;

        add_section("graphics-effects-section", L"EFFECTS", cursor);
        cursor += graphics_section_advance;
        grid_base = cursor;
        action_index = 0;
        graphics_action("graphics-bloom", L"Bloom", 13);
        graphics_action("graphics-motion-blur", L"Motion blur", 14);
        graphics_action("graphics-ambient-occlusion", L"Ambient occlusion", 15);
        graphics_action("graphics-depth-of-field", L"Depth of field", 16);
        graphics_action("graphics-volumetric-lighting", L"Volumetric lighting FX", 17);
        graphics_action("graphics-lens-flares", L"Lens flares", 18);
        graphics_action("graphics-light-shafts", L"Light shafts", 19);
        graphics_action("graphics-flex", L"NVIDIA FleX", 20);
        cursor = grid_base +
            static_cast<float>((action_index + graphics_columns - 1) /
                               graphics_columns) * graphics_action_stride + 4.0F;
        add_section("graphics-save-section",
                    status.graphics_game_running
                        ? L"CLOSE KF2 TO APPLY CHANGES"
                        : status.graphics_dirty ? L"UNSAVED CHANGES"
                                                 : L"NO UNSAVED CHANGES",
                    cursor);
        cursor += graphics_section_advance;
        grid_base = cursor;
        action_index = 0;
        add_action("graphics-apply", L"APPLY GRAPHICS",
                   editable && status.graphics_dirty, true);
        add_action("graphics-reset", L"RESET TO DEFAULTS", editable);
        add_action("game-open-config", L"OPEN KF2 SETTINGS FOLDER",
                   status.graphics_available);
    } else if (model.selected() == Destination::overlay) {
        float cursor = grid_base;
        add_section("overlay-main-section", L"OVERLAY", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("overlay-toggle",
                   model.status().overlay_enabled
                       ? L"✓ OVERLAY ENABLED (F10)"
                       : L"ENABLE OVERLAY (F10)",
                   true, model.status().overlay_enabled);
        add_action("overlay-position", L"POSITION: " +
                   model.status().overlay_position);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("overlay-size-section", L"SCALE AND POSITION", cursor);
        cursor += 34.0F;
        add_slider("overlay-scale-slider", L"Overlay scale", cursor,
                   {60, 200, model.status().overlay_scale_percent, 5, 20, L"%"});
        cursor += kSliderHeight + 14.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("overlay-scale-reset", L"RESET SCALE TO 100%");
        cursor = grid_base + kActionStride + 8.0F;
        add_section("overlay-metrics-section", L"DISPLAYED METRICS", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("overlay-show-fps",
                   model.status().overlay_show_fps ? L"✓ FPS" : L"FPS",
                   true, model.status().overlay_show_fps);
        add_action("overlay-show-frame-time",
                   model.status().overlay_show_frame_time
                       ? L"✓ FRAME TIME" : L"FRAME TIME",
                   true, model.status().overlay_show_frame_time);
        add_action("overlay-show-cpu",
                   model.status().overlay_show_cpu ? L"✓ CPU" : L"CPU",
                   true, model.status().overlay_show_cpu);
        add_action("overlay-show-gpu",
                   model.status().overlay_show_gpu ? L"✓ GPU" : L"GPU",
                   true, model.status().overlay_show_gpu);
        add_action("overlay-show-memory",
                   model.status().overlay_show_memory
                       ? L"✓ RAM / VRAM" : L"RAM / VRAM",
                   true, model.status().overlay_show_memory);
    } else if (model.selected() == Destination::advanced) {
        const auto& status = model.status();
        const bool editable = status.advanced_available &&
                              !status.advanced_game_running;
        const auto value = [&](std::size_t option) -> std::wstring {
            return option < status.advanced_values.size()
                ? status.advanced_values[option] : L"Unavailable";
        };
        const auto advanced_action = [&](std::string id,
                                         std::wstring_view label,
                                         std::size_t option) {
            const auto current = value(option);
            add_action(std::move(id),
                       std::wstring{label} + L":  " + current,
                       editable, current == L"On");
        };

        float cursor = grid_base;
        add_section("advanced-engine-section", L"ENGINE & STREAMING", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        advanced_action("advanced-one-frame-thread-lag",
                        L"One-frame thread lag", 0);
        advanced_action("advanced-per-frame-sleep",
                        L"Per-frame sleep", 1);
        advanced_action("advanced-per-frame-yield",
                        L"Per-frame yield", 2);
        advanced_action("advanced-background-level-streaming",
                        L"Background level streaming", 3);
        advanced_action("advanced-texture-streaming",
                        L"Texture streaming", 4);
        advanced_action("advanced-priority-streaming",
                        L"Priority texture streaming", 5);
        advanced_action("advanced-dynamic-streaming",
                        L"Dynamic texture streaming", 6);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;

        add_section("advanced-rendering-section", L"RENDERING", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        advanced_action("advanced-hardware-shadow-filtering",
                        L"Hardware shadow filtering", 7);
        advanced_action("advanced-downsampled-translucency",
                        L"Downsampled translucency", 8);
        advanced_action("advanced-floating-point-render-targets",
                        L"Floating-point render targets", 9);
        advanced_action("advanced-max-multisamples",
                        L"Multisampling", 10);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_slider("advanced-screen-percentage-slider", L"Render scale",
                   cursor, {50, 200, status.advanced_screen_percentage,
                            1, 10, L"%"}, editable);
        cursor += kSliderHeight + 10.0F;

        add_section("advanced-effects-section", L"EFFECTS", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        advanced_action("advanced-gore-level",
                        L"Gore level", 11);
        cursor = grid_base + kActionStride + 4.0F;
        add_slider("advanced-particle-percentage-slider", L"Particle amount",
                   cursor, {0, 100, status.advanced_particle_percentage,
                            1, 10, L"%"}, editable);
        cursor += kSliderHeight + 10.0F;
        add_slider("advanced-decal-lifetime-slider", L"Decal lifetime",
                   cursor, {0, 120, status.advanced_decal_lifetime,
                            1, 10, L" s"}, editable);
        cursor += kSliderHeight + 10.0F;

        add_section("advanced-save-section",
                    status.advanced_game_running
                        ? L"CLOSE KF2 TO APPLY CHANGES"
                        : status.advanced_dirty ? L"UNSAVED CHANGES"
                                                : L"NO UNSAVED CHANGES",
                    cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("advanced-apply", L"APPLY ADVANCED SETTINGS",
                   editable && status.advanced_dirty, true);
        add_action("advanced-reset", L"RESET TO DEFAULTS", editable);
    } else if (model.selected() == Destination::debug) {
        const auto& status = model.status();
        float cursor = grid_base;
        add_section("debug-markers-section",
                    L"IN-GAME MARKERS — NEXT PROTECTED START", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("debug-corpse-markers",
                   status.debug_corpse_markers
                       ? L"✓ CORPSE ACTIONS AND DISTANCES"
                       : L"CORPSE ACTIONS AND DISTANCES",
                   true, status.debug_corpse_markers);
        add_action("debug-zed-markers",
                   status.debug_zed_markers
                       ? L"✓ LIVING ZED DISTANCES"
                       : L"LIVING ZED DISTANCES",
                   true, status.debug_zed_markers);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("debug-status-section", L"LIVE STATUS", cursor);
        cursor += 30.0F;
        add_section("debug-status-text",
                    L"Telemetry: " + status.telemetry +
                        L"  •  Corpse actions: " +
                        status.adaptive_corpse_action_status,
                    cursor, 40.0F);
        cursor += 48.0F;
        add_section("debug-tools-section", L"LOCAL DEBUG FILES", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("diagnostics-open-data", L"OPEN PORTABLE DATA");
        add_action("diagnostics-open-log", L"OPEN SESSION LOG");
    } else if (model.selected() == Destination::diagnostics) {
        float cursor = grid_base;
        add_section("diagnostics-check-section", L"FIX A PROBLEM", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("diagnostics-full-check", L"CHECK EVERYTHING");
        add_action("diagnostics-repair-package", L"IMPORT REPAIR PACKAGE");
        cursor = grid_base + kActionStride + 8.0F;
        add_section("diagnostics-recovery-section",
                    L"BACKUP & RESTORE", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("diagnostics-backup", L"BACK UP GAME SETTINGS",
                   model.status().game_detected);
        add_action("diagnostics-flex-restore", L"RESTORE ORIGINAL GAME FILES");
        cursor = grid_base + kActionStride + 8.0F;
        add_section("diagnostics-reports-section", L"GET HELP", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("diagnostics-export-support", L"CREATE SUPPORT PACKAGE");
    }
    float content_bottom = result.content.y;
    for (auto index = page_nodes_begin; index < result.nodes.size(); ++index) {
        const auto& node = result.nodes[index];
        content_bottom = std::max(content_bottom,
                                  node.bounds.y + scroll + node.bounds.height);
    }
    constexpr float kContentBottomPadding = 12.0F;
    result.scroll_extent = std::max(
        0.0F, content_bottom + kContentBottomPadding -
                  (result.content.y + result.content.height));
    const float actual_scroll = std::clamp(model.scroll_offset(), 0.0F,
                                           result.scroll_extent);
    const float scroll_delta = actual_scroll - scroll;
    if (scroll_delta != 0.0F) {
        for (auto index = scrolling_nodes_begin; index < result.nodes.size(); ++index) {
            result.nodes[index].bounds.y -= scroll_delta;
        }
    }
    result.scroll_offset = actual_scroll;
    return result;
}

const SemanticNode* hit_test(const ShellLayoutResult& layout,
                             DipPoint point) noexcept {
    for (auto iterator = layout.nodes.rbegin(); iterator != layout.nodes.rend();
         ++iterator) {
        const bool header_action = iterator->role == SemanticRole::action &&
            iterator->id.starts_with("header-");
        const bool page_node = iterator->role == SemanticRole::page_heading ||
            iterator->role == SemanticRole::page_body ||
            iterator->role == SemanticRole::section_heading ||
            (iterator->role == SemanticRole::action && !header_action) ||
            iterator->role == SemanticRole::slider;
        if (page_node && !contains(layout.content, point)) continue;
        if (iterator->role != SemanticRole::root && contains(iterator->bounds, point)) {
            return &*iterator;
        }
    }
    return contains(layout.root, point) ? &layout.nodes.front() : nullptr;
}

void set_hover_tooltip(ShellLayoutResult& layout,
                       const SemanticNode* target, float opacity) {
    const std::optional<SemanticNode> target_copy = target
        ? std::optional<SemanticNode>{*target} : std::nullopt;
    layout.nodes.erase(
        std::remove_if(layout.nodes.begin(), layout.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::tooltip;
                       }),
        layout.nodes.end());
    if (!target_copy ||
        (target_copy->role != SemanticRole::action &&
         target_copy->role != SemanticRole::slider) ||
        !target_copy->action_id || !target_copy->enabled) {
        return;
    }
    const auto help = action_help_text(*target_copy->action_id);
    if (!help) return;

    const float width = std::min(520.0F, layout.content.width);
    const float maximum_x = layout.content.x + layout.content.width - width;
    const float x = std::clamp(target_copy->bounds.x, layout.content.x,
                               maximum_x);
    float y = target_copy->bounds.y + target_copy->bounds.height + 6.0F;
    if (y + kTooltipHeight > layout.content.y + layout.content.height) {
        y = target_copy->bounds.y - kTooltipHeight - 6.0F;
    }
    y = std::clamp(y, layout.content.y,
                   std::max(layout.content.y,
                            layout.content.y + layout.content.height -
                                kTooltipHeight));
    layout.nodes.push_back({"hover-tooltip", SemanticRole::tooltip,
                            {x, y, width, kTooltipHeight}, *help});
    layout.nodes.back().opacity = std::clamp(opacity, 0.0F, 1.0F);
}

}  // namespace kf2::ui
