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
constexpr float kTooltipHeight = 44.0F;
constexpr float kSliderHeight = 88.0F;

std::wstring status_text(const UiStatus& status) {
    if (!status.game_detected) {
        return L"Choose your Killing Floor 2 folder to get started";
    }
    const std::wstring mode = status.mode == L"Adaptive / Automatic"
        ? L"Adaptive on" : status.mode;
    std::wstring text = L"Ready   •   " + mode + L"   •   Target " +
                        std::to_wstring(status.target_fps) + L" FPS";
    if (status.live_fps && status.live_frame_time_ms) {
        std::wostringstream live;
        live << L"   •   Live " << std::fixed << std::setprecision(1)
             << *status.live_fps << L" FPS";
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

std::wstring corpse_metric(const UiStatus& status) {
    std::wstring value = L"—";
    if (status.live_active_corpses || status.live_sleeping_corpses) {
        value = std::to_wstring(status.live_active_corpses.value_or(0)) + L" / " +
                std::to_wstring(status.live_sleeping_corpses.value_or(0));
    }
    return L"CORPSES\nMoving / sleeping: " + value;
}

std::wstring load_metric(const UiStatus& status) {
    std::wostringstream text;
    text << L"GAME LOAD\nCPU ";
    if (status.live_cpu_percent) {
        text << std::fixed << std::setprecision(0) << *status.live_cpu_percent
             << L"%";
    } else {
        text << L"—";
    }
    text << L"  •  GPU ";
    if (status.live_gpu_percent) {
        text << std::fixed << std::setprecision(0) << *status.live_gpu_percent
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
    constexpr std::array<float, 2> header_action_widths{148.0F, 118.0F};
    const bool show_global_actions = width >= 430.0F &&
                                     result.header.height >= 64.0F;
    const float header_actions_width = show_global_actions
        ? std::accumulate(header_action_widths.begin(),
                          header_action_widths.end(), 0.0F) + header_action_gap
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
        const std::string update_id = status.update_available
            ? "header-update-install" : "header-update-check";
        const std::wstring update_text = status.update_installing
            ? L"UPDATING..."
            : status.update_checking ? L"CHECKING..."
            : status.update_available ? L"UPDATE AVAILABLE"
                                      : L"UPDATES";
        const bool update_enabled = !status.update_checking &&
            !status.update_installing &&
            (!status.update_available || status.update_installable);
        float x = header_actions_left;
        result.nodes.push_back({
            "header-update", SemanticRole::action,
            {x, 14.0F, header_action_widths[0], 42.0F}, update_text,
            std::nullopt, false, model.focused_action() == update_id,
            update_enabled, update_id, std::nullopt,
            status.update_available});
        x += header_action_widths[0] + header_action_gap;
        result.nodes.push_back({
            "header-repair", SemanticRole::action,
            {x, 14.0F, header_action_widths[1], 42.0F}, L"REPAIR",
            std::nullopt, false,
            model.focused_action() == "header-repair",
            !status.update_installing, "header-repair"});
    }

    result.nodes.push_back({"status", SemanticRole::status, result.status_strip,
                            status_text(model.status())});

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
        metric(L"LIVE FPS", model.status().live_fps, 1, L" FPS"),
        metric(L"FRAME TIME", model.status().live_frame_time_ms, 1, L" ms"),
        load_metric(model.status()),
        corpse_metric(model.status())};
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

    float banner_y = result.content.y;
    float banner_offset = 0.0F;
    if (model.recovery_required()) {
        result.nodes.push_back({"recovery", SemanticRole::recovery_banner,
                                {result.content.x, banner_y,
                                 result.content.width, 44.0F},
                                L"The previous session will be checked safely."});
        banner_y += 52.0F;
        banner_offset += 52.0F;
    }
    if (model.notice().has_value()) {
        result.nodes.push_back({"notice", SemanticRole::notice,
                                {result.content.x, banner_y,
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
    const auto add_section = [&](std::string id, std::wstring text, float y) {
        result.nodes.push_back({std::move(id), SemanticRole::section_heading,
                                {result.content.x, y - scroll,
                                 result.content.width, 28.0F}, std::move(text)});
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
        add_section("dashboard-support-section", L"SETTINGS", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("dashboard-settings", L"SET UP OPTIMIZATION");
        add_action("dashboard-overlay", L"SET UP OVERLAY");
        add_action("dashboard-diagnostics", L"HELP & REPAIR");
    } else if (model.selected() == Destination::game) {
        float cursor = grid_base;
        add_section("game-start-section", L"GAME LAUNCH", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("game-launch", L"LAUNCH KF2 ADAPTIVELY",
                   model.status().game_detected, true);
        add_action("game-select-install",
                   model.status().game_detected
                       ? L"CHANGE GAME FOLDER" : L"SELECT GAME FOLDER");
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("game-folders-section", L"LOCAL FOLDERS", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("game-open-install", L"OPEN GAME FOLDER",
                   model.status().game_detected);
        add_action("game-open-config", L"OPEN SETTINGS",
                   model.status().game_detected);
        add_action("game-open-logs", L"OPEN GAME LOG",
                   model.status().game_detected);
    } else if (model.selected() == Destination::optimizer) {
        const auto config = model.status().config;
        float cursor = grid_base;
        add_section("optimizer-plan-section", L"SAFE CHANGE PLAN", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("optimizer-preview", L"SHOW AUTOMATIC PLAN");
        add_action("optimizer-apply", L"APPLY PREVIEW",
                   config == ConfigWorkflowState::preview_ready ||
                       config == ConfigWorkflowState::applied ||
                       config == ConfigWorkflowState::restore_available);
        add_action("optimizer-backup", L"CREATE INI BACKUP",
                   config != ConfigWorkflowState::unavailable);
        add_action("optimizer-restore", L"RESTORE LATEST BACKUP",
                   config == ConfigWorkflowState::restore_available ||
                       config == ConfigWorkflowState::recovery_required);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("optimizer-transfer-section", L"IMPORT, EXPORT & STORAGE", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("optimizer-export", L"EXPORT PREVIEW",
                   config == ConfigWorkflowState::preview_ready ||
                       config == ConfigWorkflowState::applied ||
                       config == ConfigWorkflowState::restore_available);
        add_action("optimizer-import", L"IMPORT PREVIEW");
        add_action("optimizer-open-backups", L"OPEN BACKUP FOLDER");
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("optimizer-adaptive-note-section",
                    L"ADAPTIVE HANDLES FINE-TUNING AUTOMATICALLY",
                    cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("optimizer-open-settings", L"SET UP OPTIMIZATION");
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
    } else if (model.selected() == Destination::settings) {
        float cursor = grid_base;
        add_section("settings-goals-section", L"PERFORMANCE", cursor);
        cursor += 34.0F;
        add_slider("settings-target-slider", L"Target FPS", cursor,
                   {optimizer::kTargetFpsMinimum,
                    optimizer::kTargetFpsMaximum,
                    model.status().target_fps, 1, 10, L" FPS"});
        cursor += kSliderHeight + 12.0F;
        add_slider("settings-corpses-slider", L"Maximum corpses",
                   cursor, {4, 2000, model.status().corpse_limit,
                             1, 50, L" corpses"});
        cursor += kSliderHeight + 12.0F;
        add_section("settings-adaptive-section",
                    L"AUTOMATIC: QUALITY • PHYSICS • LOD • FLEX • CORPSES",
                    cursor);
        cursor += 42.0F;
        add_section("settings-safety-section",
                    L"APP", cursor);
        cursor += 34.0F;
        grid_base = cursor;
        action_index = 0;
        add_action("settings-animations",
                   model.status().animations_enabled
                       ? L"✓ ANIMATIONS" : L"REDUCE ANIMATIONS",
                   true, model.status().animations_enabled);
        add_action("settings-updates-automatic",
                   model.status().automatic_update_checks
                       ? L"✓ AUTOMATIC UPDATE CHECKS"
                       : L"AUTOMATIC UPDATE CHECKS OFF",
                   !model.status().update_checking &&
                       !model.status().update_installing,
                   model.status().automatic_update_checks);
        cursor = grid_base +
            static_cast<float>((action_index + action_columns - 1) /
                               action_columns) * kActionStride + 8.0F;
        add_section("settings-updates-section", L"VERSION & UPDATES", cursor);
        cursor += 34.0F;
        add_section("settings-updates-installed",
                    L"Installed " + model.status().update_installed_version +
                        L"  •  Last checked " +
                        model.status().update_last_check + L"  •  " +
                        model.status().update_status,
                    cursor);
        cursor += 34.0F;
        if (model.status().update_available) {
            add_section("settings-updates-release",
                        L"New version " +
                            model.status().update_available_version +
                            L"  •  Published " +
                            model.status().update_published_at +
                            L"  •  " +
                            model.status().update_download_size,
                        cursor);
            cursor += 34.0F;
            if (!model.status().update_changelog.empty()) {
                add_section("settings-updates-changelog",
                            model.status().update_changelog, cursor);
                cursor += 96.0F;
            }
            grid_base = cursor;
            action_index = 0;
            add_action("settings-updates-later", L"REMIND ME LATER",
                       !model.status().update_installing);
        }
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
        add_action("diagnostics-open-data", L"OPEN PORTABLE DATA");
        add_action("diagnostics-open-log", L"OPEN SESSION LOG");
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
        for (auto index = page_nodes_begin; index < result.nodes.size(); ++index) {
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
            iterator->action_id && iterator->action_id->starts_with("header-");
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

std::optional<std::wstring> action_help_text(std::string_view action_id) {
    if (action_id == "header-launch" || action_id == "dashboard-launch") {
        return L"Launches KF2 with the safe Adaptive profile.";
    }
    if (action_id == "dashboard-settings") {
        return L"Opens the main settings for target FPS, corpse ceiling, and Adaptive FleX.";
    }
    if (action_id == "dashboard-overlay") {
        return L"Opens the focused overlay settings.";
    }
    if (action_id == "dashboard-diagnostics") {
        return L"Opens checks, restoration, backups, and local reports.";
    }
    if (action_id == "header-restore") {
        return L"Restores the last verified INI state. KF2 must be closed.";
    }
    if (action_id == "header-backup") {
        return L"Immediately creates a local, verified backup of important KF2 configuration.";
    }
    if (action_id == "header-diagnostics") {
        return L"Opens diagnostics, self-checks, and local reports.";
    }
    if (action_id == "header-update-check") {
        return L"Checks the official GitHub Releases page for a newer version.";
    }
    if (action_id == "header-update-install") {
        return L"Installs the displayed verified update after your confirmation.";
    }
    if (action_id == "header-repair") {
        return L"Repairs this exact installed version from its verified official GitHub release.";
    }
    if (action_id == "optimizer-preview") {
        return L"Shows the protected before/after plan. Adaptive mode prepares the verified plan automatically at launch.";
    }
    if (action_id == "optimizer-apply") {
        return L"Creates a verified backup first, then applies only the visible preview.";
    }
    if (action_id == "optimizer-restore" ||
        action_id == "diagnostics-flex-restore") {
        return L"Restores the last verified original state. KF2 must be closed.";
    }
    if (action_id == "game-offline-telemetry") {
        return L"Enables local offline gameplay data only for solo sessions launched through this app.";
    }
    if (action_id == "diagnostics-full-check") {
        return L"Checks the package, paths, backups, telemetry, and runtime locally without changing gameplay.";
    }
    if (action_id == "diagnostics-repair-package") {
        return L"Select a complete matching KF2OptimizerNext folder. Only missing or damaged files with verified SHA-256 values are imported; the running EXE is never replaced.";
    }
    if (action_id == "diagnostics-auto-repair") {
        return L"Downloads only the GitHub release matching this exact installed version, then replaces missing or damaged files after build-identity and SHA-256 verification. The running EXE is never replaced.";
    }
    if (action_id == "diagnostics-export-support") {
        return L"Exports a bounded local support package. Nothing is uploaded.";
    }
    if (action_id == "settings-advanced-toggle") {
        return L"Shows or hides less common Adaptive settings. This view does not change game values.";
    }
    if (action_id == "settings-finetuning" ||
        action_id == "optimizer-open-settings") {
        return L"Switches between simple optimization targets and detailed verified values.";
    }
    if (action_id.starts_with("settings-adaptive-")) {
        return L"Changes only bounded Adaptive control. All safety rules remain active.";
    }
    if (action_id == "overlay-toggle") {
        return L"Shows or hides the local overlay. F10 works while KF2 is running.";
    }
    if (action_id.starts_with("overlay-")) {
        return L"Changes only the portable overlay display and does not write into the KF2 process.";
    }
    if (action_id.starts_with("diagnostics-")) {
        return L"Runs bounded local diagnostics. No data is uploaded.";
    }
    if (action_id == "settings-target-slider") {
        return L"Drag, use the mouse wheel, or press arrow keys to change target FPS. The value is stored locally and atomically.";
    }
    if (action_id == "settings-corpses-slider") {
        return L"Sets a ceiling of up to 2000 corpses. Adaptive uses it on the next protected KF2 launch and lowers it only under confirmed frame-time pressure.";
    }
    if (action_id.ends_with("-slider")) {
        return L"Fine-tune with mouse, wheel, or keyboard. Changes are stored locally.";
    }
    return std::nullopt;
}

void set_hover_tooltip(ShellLayoutResult& layout,
                       const SemanticNode* target) {
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
}

}  // namespace kf2::ui
