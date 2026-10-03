#include "application_runtime.hpp"

namespace kf2::app {

void UiRuntime::refresh_advanced_presentation() {
    auto status = model.status();
    status.advanced_available = advanced_settings.pending.has_value();
    status.advanced_game_running = installation &&
        game::game_process_may_be_running(installation->executable);
    status.advanced_dirty = advanced_settings.saved &&
        advanced_settings.pending &&
        *advanced_settings.saved != *advanced_settings.pending;
    if (advanced_settings.pending) {
        for (std::size_t option = 0;
             option < game::kAdvancedOptionCount; ++option) {
            status.advanced_values[option] = game::advanced_value_label(
                static_cast<game::AdvancedOption>(option),
                *advanced_settings.pending);
        }
        status.advanced_screen_percentage = game::advanced_slider_value(
            game::AdvancedOption::screen_percentage,
            *advanced_settings.pending);
        status.advanced_particle_percentage = game::advanced_slider_value(
            game::AdvancedOption::particle_percentage,
            *advanced_settings.pending);
        status.advanced_decal_lifetime = game::advanced_slider_value(
            game::AdvancedOption::decal_lifetime,
            *advanced_settings.pending);
    } else {
        status.advanced_values.fill(L"Unavailable");
    }
    model.set_status(std::move(status));
}

void UiRuntime::reload_advanced_settings() {
    if (!installation) {
        advanced_settings.saved.reset();
        advanced_settings.pending.reset();
        refresh_advanced_presentation();
        return;
    }
    const auto loaded = game::read_advanced_game_settings(
        session_config_snapshot
            ? session_config_snapshot->snapshot_root / L"files"
            : installation->config_root);
    if (!loaded.has_value()) {
        advanced_settings.saved.reset();
        advanced_settings.pending.reset();
        refresh_advanced_presentation();
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_UNAVAILABLE",
            loaded.error().message, L""});
        return;
    }
    advanced_settings.saved = loaded.value();
    advanced_settings.pending = loaded.value();
    refresh_advanced_presentation();
}

void UiRuntime::cycle_advanced_option(game::AdvancedOption option) {
    if (!installation || !advanced_settings.pending) {
        reload_advanced_settings();
        if (!advanced_settings.pending) return;
    }
    if (game::game_process_may_be_running(installation->executable)) {
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_GAME_RUNNING",
            L"Close KF2 before changing advanced game settings.", L""});
        invalidate();
        return;
    }
    if (!game::cycle_advanced_option(
            *advanced_settings.pending, option)) return;
    save_advanced_selection(game::advanced_option_label(option));
}

void UiRuntime::stage_advanced_slider(
    game::AdvancedOption option, int value) {
    if (!installation || !advanced_settings.pending) {
        reload_advanced_settings();
        if (!advanced_settings.pending) return;
    }
    if (game::game_process_may_be_running(installation->executable)) {
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_GAME_RUNNING",
            L"Close KF2 before changing advanced game settings.", L""});
        invalidate();
        return;
    }
    if (!game::set_advanced_slider_value(
            *advanced_settings.pending, option, value)) {
        return;
    }
    save_advanced_selection(game::advanced_option_label(option));
}

void UiRuntime::save_advanced_selection(std::wstring_view label) {
    if (!advanced_settings.saved || !advanced_settings.pending) {
        reload_advanced_settings();
        return;
    }
    if (*advanced_settings.saved == *advanced_settings.pending) {
        refresh_advanced_presentation();
        invalidate();
        return;
    }
    const auto saved = apply_advanced_settings();
    if (!saved.has_value()) {
        const auto error = saved.error();
        reload_advanced_settings();
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_SAVE_FAILED",
            std::wstring{label} + L" could not be saved safely: " + error.message,
            model.recovery_required()
                ? L"Keep KF2 closed; restore the retained backup or repair the protected launch before retrying."
                : L"The controls were reloaded from the saved personal settings."});
        invalidate();
        return;
    }
    model.set_notice({
        ui::NoticeSeverity::info, L"ADVANCED_SAVED",
        std::wstring{label} +
            L" was saved and verified. A restore backup is available.",
        L""});
    invalidate();
}

void UiRuntime::reset_advanced_settings() {
    if (!installation || !advanced_settings.pending) {
        reload_advanced_settings();
        if (!advanced_settings.pending) return;
    }
    if (game::game_process_may_be_running(installation->executable)) {
        model.set_notice({
            ui::NoticeSeverity::warning, L"ADVANCED_GAME_RUNNING",
            L"Close KF2 before changing advanced game settings.", L""});
        invalidate();
        return;
    }
    advanced_settings.pending = game::recommended_advanced_defaults();
    save_advanced_selection(L"Recommended advanced defaults");
}

Result<config::ApplyResult> UiRuntime::apply_advanced_settings() {
    if (!installation || !advanced_settings.pending ||
        !advanced_settings.saved) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::not_found,
             L"Advanced KF2 settings are unavailable", 0});
    }
    const auto changes = game::advanced_setting_changes(
        *advanced_settings.saved, *advanced_settings.pending);
    if (changes.empty()) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::invalid_argument,
             L"The selected advanced settings are already saved", 0});
    }
    if (game::game_process_may_be_running(installation->executable)) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::access_denied,
             L"Close KF2 before applying advanced game settings", 0});
    }
    if (start_mode != StartMode::normal || model.recovery_required() ||
        session_config_launch_deadline_ns != 0 || final_graphics_capture_pending ||
        (session_config_snapshot && !session_config_waiting_for_launch)) {
        return Result<config::ApplyResult>::failure({ErrorCode::access_denied,
            L"Complete the protected launch or recovery before changing advanced settings",
            0});
    }

    const bool staged = session_config_snapshot.has_value();
    if (staged && !restore_protected_session_config(
            L"Saving advanced settings before the protected launch")) {
        return Result<config::ApplyResult>::failure({ErrorCode::recovery_required,
            L"The protected originals could not be restored; recovery data was retained",
            0});
    }
    // Reuse the existing protected-launch transaction and verified apply backup.
    // Only the explicit delta reaches personal INIs, never temporary session values.
    const auto fail = [this, staged](Error error) {
        preview.reset();
        if (staged && error.code != ErrorCode::recovery_required &&
            !model.recovery_required()) {
            const auto rearmed = prepare_automatic_external_launch_profile();
            if (!rearmed.has_value() || !rearmed.value()) {
                model.set_recovery_required(true);
                error.code = ErrorCode::recovery_required;
                error.message += L"; the protected launch could not be prepared again";
                if (!rearmed.has_value()) error.message += L": " + rearmed.error().message;
            }
        }
        return Result<config::ApplyResult>::failure(std::move(error));
    };
    auto prepared = prepare(
        changes, L"Explicit user-selected advanced KF2 settings");
    if (!prepared.has_value()) {
        return fail(prepared.error());
    }
    auto result = apply({.game_running = false});
    if (!result.has_value()) return fail(result.error());
    const auto backup_id = result.value().backup.id;
    if (staged) {
        const auto rearmed = prepare_automatic_external_launch_profile();
        if (!rearmed.has_value() || !rearmed.value()) {
            last_backup_id = backup_id;
            Error error = rearmed.has_value()
                ? Error{ErrorCode::access_denied,
                    L"The protected launch is unavailable", 0}
                : rearmed.error();
            // Never restore an edit backup over an unresolved temporary profile.
            if (!restore_protected_session_config(
                    L"Rolling back an advanced-settings rebuild failure")) {
                model.set_recovery_required(true);
                error.code = ErrorCode::recovery_required;
                error.message += L"; protected restoration is incomplete; the edit backup was retained";
                return fail(std::move(error));
            }
            const auto rolled_back = backup::restore_backup(
                backups, backup_id, installation->config_root,
                {.game_running = game::game_process_may_be_running(
                    installation->executable)});
            if (!rolled_back.has_value()) {
                model.set_recovery_required(true);
                error.code = ErrorCode::recovery_required;
                error.message += L"; the edit backup could not be restored: " +
                    rolled_back.error().message;
                return fail(std::move(error));
            }
            events->append({0, diagnostics::Severity::warning,
                "ADVANCED_SETTINGS_ROLLED_BACK",
                L"The advanced edit was rolled back from its verified backup after protected-launch rebuilding failed",
                L"advanced"});
            return fail(std::move(error));
        }
    }
    last_backup_id = backup_id;
    events->append({
        0, diagnostics::Severity::info,
        "ADVANCED_SETTINGS_EXPLICIT_APPLIED",
        L"Explicit user-selected advanced KF2 INI settings were applied and verified",
        L"advanced"});
    reload_advanced_settings();
    return result;
}

}  // namespace kf2::app
