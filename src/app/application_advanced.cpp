#include "application_runtime.hpp"

namespace kf2::app {

void UiRuntime::refresh_advanced_presentation() {
    auto status = model.status();
    status.advanced_available = advanced_pending.has_value();
    status.advanced_game_running = installation &&
        game::find_running_game_process(installation->executable).has_value();
    status.advanced_dirty = advanced_saved && advanced_pending &&
        *advanced_saved != *advanced_pending;
    if (advanced_pending) {
        for (std::size_t option = 0;
             option < game::kAdvancedOptionCount; ++option) {
            status.advanced_values[option] = game::advanced_value_label(
                static_cast<game::AdvancedOption>(option), *advanced_pending);
        }
        status.advanced_screen_percentage = game::advanced_slider_value(
            game::AdvancedOption::screen_percentage, *advanced_pending);
        status.advanced_particle_percentage = game::advanced_slider_value(
            game::AdvancedOption::particle_percentage, *advanced_pending);
        status.advanced_decal_lifetime = game::advanced_slider_value(
            game::AdvancedOption::decal_lifetime, *advanced_pending);
    } else {
        status.advanced_values.fill(L"Unavailable");
    }
    model.set_status(std::move(status));
}

void UiRuntime::reload_advanced_settings() {
    if (!installation) {
        advanced_saved.reset();
        advanced_pending.reset();
        refresh_advanced_presentation();
        return;
    }
    const auto loaded = game::read_advanced_game_settings(
        installation->config_root);
    if (!loaded.has_value()) {
        advanced_saved.reset();
        advanced_pending.reset();
        refresh_advanced_presentation();
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_UNAVAILABLE",
            loaded.error().message, L""});
        return;
    }
    advanced_saved = loaded.value();
    advanced_pending = loaded.value();
    refresh_advanced_presentation();
}

void UiRuntime::cycle_advanced_option(game::AdvancedOption option) {
    if (!installation || !advanced_pending) {
        reload_advanced_settings();
        if (!advanced_pending) return;
    }
    if (game::find_running_game_process(installation->executable).has_value()) {
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_GAME_RUNNING",
            L"Close KF2 before changing advanced game settings.", L""});
        invalidate();
        return;
    }
    if (!game::cycle_advanced_option(*advanced_pending, option)) return;
    refresh_advanced_presentation();
    model.set_notice({
        ui::NoticeSeverity::info, L"ADVANCED_STAGED",
        std::wstring{game::advanced_option_label(option)} +
            L" is staged. Select Apply advanced settings to save it.",
        L""});
    invalidate();
}

void UiRuntime::stage_advanced_slider(
    game::AdvancedOption option, int value) {
    if (!installation || !advanced_pending) {
        reload_advanced_settings();
        if (!advanced_pending) return;
    }
    if (game::find_running_game_process(installation->executable).has_value()) {
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_GAME_RUNNING",
            L"Close KF2 before changing advanced game settings.", L""});
        invalidate();
        return;
    }
    if (!game::set_advanced_slider_value(*advanced_pending, option, value)) {
        return;
    }
    refresh_advanced_presentation();
    model.set_notice({
        ui::NoticeSeverity::info, L"ADVANCED_STAGED",
        std::wstring{game::advanced_option_label(option)} +
            L" is staged. Select Apply advanced settings to save it.",
        L""});
    invalidate();
}

void UiRuntime::reset_advanced_settings() {
    if (advanced_pending) {
        advanced_pending = game::recommended_advanced_defaults();
    }
    refresh_advanced_presentation();
    model.set_notice({
        ui::NoticeSeverity::info, L"ADVANCED_RESET",
        L"Recommended advanced defaults are ready. Select Apply advanced settings to save them.",
        L""});
    invalidate();
}

Result<config::ApplyResult> UiRuntime::apply_advanced_settings() {
    if (!installation || !advanced_pending || !advanced_saved) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::not_found,
             L"Advanced KF2 settings are unavailable", 0});
    }
    const auto changes = game::advanced_setting_changes(
        *advanced_saved, *advanced_pending);
    if (changes.empty()) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::invalid_argument,
             L"No advanced game changes are staged", 0});
    }
    if (game::find_running_game_process(installation->executable).has_value()) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::access_denied,
             L"Close KF2 before applying advanced game settings", 0});
    }
    auto prepared = prepare(
        changes, L"Explicit user-selected advanced KF2 settings");
    if (!prepared.has_value()) {
        return Result<config::ApplyResult>::failure(prepared.error());
    }
    auto result = apply({.game_running = false});
    if (result.has_value()) {
        last_backup_id = result.value().backup.id;
        events->append({
            0, diagnostics::Severity::info,
            "ADVANCED_SETTINGS_EXPLICIT_APPLIED",
            L"Explicit user-selected advanced KF2 INI settings were applied and verified",
            L"advanced"});
        reload_advanced_settings();
    }
    return result;
}

}  // namespace kf2::app
