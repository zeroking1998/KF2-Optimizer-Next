#include "application_runtime.hpp"
#include "runtime/action_contract.hpp"
#include "runtime/action_router.hpp"
#include "runtime/feature_composition.hpp"

namespace kf2::app {
Result<bool> UiRuntime::set_overlay(bool enabled) {
    if (start_mode != StartMode::normal) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"This action is unavailable", 0});
    }
    if (enabled == overlay_enabled) {
        return Result<bool>::success(overlay_enabled);
    }
    const bool previous = overlay_enabled;
    overlay_enabled = enabled;
    if (!enabled && overlay_window) {
        const auto previous_presentation = overlay_presentation;
        overlay::OverlayPresentation hidden;
        overlay_presentation = hidden;
        auto hidden_result = overlay_window->update(hidden);
        if (!hidden_result.has_value()) {
            overlay_enabled = previous;
            overlay_presentation = previous_presentation;
            return hidden_result;
        }
    }
    telemetry_tick();
    optimizer_settings.overlay_enabled = overlay_enabled;
    const auto saved = platform::windows::atomic_replace_utf8(
        settings_path, config::serialize_settings(optimizer_settings));
    if (!saved.has_value()) {
        overlay_enabled = previous;
        optimizer_settings.overlay_enabled = previous;
        if (overlay_window) telemetry_tick();
        return Result<bool>::failure(saved.error());
    }
    auto status = model.status();
    status.overlay_enabled = overlay_enabled;
    model.set_status(std::move(status));
    invalidate();
    return Result<bool>::success(overlay_enabled);
}

Result<bool> UiRuntime::toggle_overlay() {
    const auto now = monotonic_ns();
    constexpr std::uint64_t kToggleDebounceNs = 400'000'000ULL;
    if (last_overlay_toggle_ns != 0 && now > last_overlay_toggle_ns &&
        now - last_overlay_toggle_ns < kToggleDebounceNs) {
        return Result<bool>::success(overlay_enabled);
    }
    last_overlay_toggle_ns = now;
    return set_overlay(!overlay_enabled);
}
void UiRuntime::set_slider_value(std::string_view id, int requested_value) {
    const auto show_notice = [this](ui::NoticeSeverity severity,
                                    std::wstring code,
                                    std::wstring message) {
        model.set_notice(
            {severity, std::move(code), std::move(message), L""});
        invalidate();
    };
    if (start_mode != StartMode::normal) {
        return;
    }

    const auto control = runtime::resolve_control(id, requested_value);
    if (!control) return;
    const int value = control->value;

    if (control->id == runtime::ControlId::film_grain) {
        if (!video_pending) reload_video_settings();
        if (!video_pending) return;
        if (installation && game::find_running_game_process(
                installation->executable).has_value()) {
            show_notice(ui::NoticeSeverity::warning, L"GRAPHICS_GAME_RUNNING",
                        L"Close KF2 before changing its video settings.");
            return;
        }
        video_pending->film_grain_percent = value;
        save_video_selection();
        return;
    }
    if (control->id == runtime::ControlId::advanced_screen_percentage) {
        stage_advanced_slider(
            game::AdvancedOption::screen_percentage, value);
        return;
    }
    if (control->id == runtime::ControlId::advanced_particle_percentage) {
        stage_advanced_slider(
            game::AdvancedOption::particle_percentage, value);
        return;
    }
    if (control->id == runtime::ControlId::advanced_decal_lifetime) {
        stage_advanced_slider(game::AdvancedOption::decal_lifetime, value);
        return;
    }

    const config::Settings previous = optimizer_settings;
    bool adaptive_policy_changed = false;
    bool overlay_changed = false;
    std::wstring code;
    std::wstring message;

    if (control->id == runtime::ControlId::target_fps) {
        optimizer_settings.target_fps = value;
        adaptive_policy_changed = true;
        code = L"TARGET_FPS_CHANGED";
        message = L"Target FPS: " +
            std::to_wstring(optimizer_settings.target_fps);
    } else if (control->id == runtime::ControlId::corpse_limit) {
        optimizer_settings.corpse_limit = value;
        adaptive_policy_changed = true;
        code = L"CORPSE_LIMIT_CHANGED";
        message = L"Adaptive corpse ceiling: " +
            std::to_wstring(optimizer_settings.corpse_limit) +
            L" (from the next protected KF2 launch)";
    } else if (control->id == runtime::ControlId::overlay_scale) {
        optimizer_settings.overlay_scale_percent = value;
        overlay_scale = static_cast<float>(
            optimizer_settings.overlay_scale_percent) / 100.0F;
        overlay_changed = true;
        code = L"OVERLAY_SCALE_CHANGED";
        message = L"Overlay scale: " +
            std::to_wstring(optimizer_settings.overlay_scale_percent) + L" %";
    }

    if (optimizer_settings.target_fps == previous.target_fps &&
        optimizer_settings.corpse_limit == previous.corpse_limit &&
        optimizer_settings.adaptive_minimum_quality ==
            previous.adaptive_minimum_quality &&
        optimizer_settings.adaptive_maximum_quality ==
            previous.adaptive_maximum_quality &&
        optimizer_settings.adaptive_headroom_percent ==
            previous.adaptive_headroom_percent &&
        optimizer_settings.overlay_scale_percent ==
            previous.overlay_scale_percent) {
        return;
    }

    const auto saved = platform::windows::atomic_replace_utf8(
        settings_path, config::serialize_settings(optimizer_settings));
    if (!saved.has_value()) {
        optimizer_settings = previous;
        overlay_scale = static_cast<float>(
            optimizer_settings.overlay_scale_percent) / 100.0F;
        show_notice(ui::NoticeSeverity::error, L"SETTINGS_SAVE_FAILED",
                    saved.error().message);
        return;
    }

    if (adaptive_policy_changed) {
        const bool session_value_staged_for_restart =
            adaptive_session_policy.has_value() &&
            control->id == runtime::ControlId::corpse_limit;
        if (session_value_staged_for_restart) {
            adaptive_policy_changed = false;
        }
    }
    const bool live_target_updated =
        optimizer_settings.target_fps != previous.target_fps &&
        adaptive_session_policy.has_value();
    if (live_target_updated) {
        adaptive_session_policy->target_fps = optimizer_settings.target_fps;
    }
    if (adaptive_policy_changed) {
        auto generation = adaptive_actuation.generation();
        generation.settings = ++adaptive_settings_generation;
        adaptive_actuation.rebase(generation, monotonic_ns());
        if (optimizer_settings.target_fps == previous.target_fps) {
            adaptive_governor.reset();
        }
        adaptive_overhead_breaches = 0;
        adaptive_overhead_frozen = false;
    }
    preview.reset();
    auto status = model.status();
    status.target_fps = optimizer_settings.target_fps;
    if (live_target_updated) {
        status.active_target_fps = optimizer_settings.target_fps;
    }
    status.corpse_limit = optimizer_settings.corpse_limit;
    status.overlay_scale_percent = optimizer_settings.overlay_scale_percent;
    update_adaptive_policy_status(status);
    model.set_status(std::move(status));

    if (optimizer_settings.target_fps != previous.target_fps && installation) {
        const bool game_running = game::find_running_game_process(
            installation->executable).has_value();
        if (game_running) {
            message +=
                L"; Adaptive is using this target now; the native cap will use it after KF2 restarts";
        } else {
            const auto synchronized = synchronize_frame_rate_cap();
            if (!synchronized.has_value()) {
                events->append({0, diagnostics::Severity::error,
                    "TARGET_FPS_PERSIST_FAILED",
                    synchronized.error().message, L"config"});
                show_notice(ui::NoticeSeverity::error,
                            L"TARGET_FPS_PERSIST_FAILED",
                            L"Target FPS was saved, but KF2's native cap could not be updated: " +
                                synchronized.error().message);
                return;
            }
            message += L"; KF2's native startup cap is ready";
        }
    }

    if (overlay_changed) telemetry_tick();
    show_notice(ui::NoticeSeverity::info, std::move(code),
                std::move(message));
}

void UiRuntime::execute_action(std::string_view action) {
    constexpr bool protected_game_launch = true;
    const auto resolved = runtime::resolve_action(
        action, {.protected_game_launch = protected_game_launch});
    if (!resolved) return;
    if (resolved->normal_mode_required &&
        start_mode != StartMode::normal) {
        return;
    }

    static_cast<void>(runtime::dispatch_action(
        *this,
        runtime::ActionRequest{
            resolved->id, action, runtime::NoPayload{}},
        runtime::feature_definitions()));
}

Result<config::ApplyResult> UiRuntime::apply(
    const config::ApplyPreconditions& preconditions) {
    if (start_mode != StartMode::normal) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::access_denied,
             L"This action is unavailable", 0});
    }
    if (!preview) return Result<config::ApplyResult>::failure(
        {ErrorCode::invalid_argument, L"No configuration preview is ready", 0});
    auto applied = config::apply_preview(*preview, backups, preconditions);
    if (applied.has_value()) {
        const auto pruned = backups.prune_verified({.keep_latest = 8});
        if (!pruned.has_value()) {
            events->append({0, diagnostics::Severity::warning,
                            "BACKUP_RETENTION_FAILED",
                            pruned.error().message, L"backup"});
        }
        events->append({0, diagnostics::Severity::info, "CONFIG_APPLIED",
                        preview_context + L": applied " +
                            std::to_wstring(preview->items.size()) +
                            L" settings across " +
                            std::to_wstring(applied.value().files_changed) +
                            L" files; verified restore backup created",
                        L"config"});
    } else {
        events->append({0, diagnostics::Severity::warning,
                        "CONFIG_APPLY_BLOCKED", applied.error().message,
                        L"config"});
    }
    invalidate();
    return applied;
}

Result<backup::RestoreResult> UiRuntime::restore(
    std::string_view id, const config::ApplyPreconditions& preconditions) {
    if (start_mode != StartMode::normal) {
        return Result<backup::RestoreResult>::failure(
            {ErrorCode::access_denied,
             L"This action is unavailable", 0});
    }
    if (!installation) {
        return Result<backup::RestoreResult>::failure(
            {ErrorCode::not_found,
             L"A verified KF2 installation is required before restore", 0});
    }
    auto restored = backup::restore_backup(
        backups, id, installation->config_root, preconditions);
    if (restored.has_value()) {
        const auto capped = synchronize_frame_rate_cap();
        if (!capped.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "TARGET_FPS_PERSIST_FAILED", capped.error().message,
                L"config"});
        }
        events->append({0, diagnostics::Severity::info, "CONFIG_RESTORED",
                        L"Restored " +
                            std::to_wstring(restored.value().files_restored) +
                            L" configuration files from the verified backup; "
                            L"a pre-restore backup was created",
                        L"config"});
    } else {
        events->append({0, diagnostics::Severity::warning,
                        "RESTORE_BLOCKED", restored.error().message,
                        L"config"});
    }
    invalidate();
    return restored;
}

}  // namespace kf2::app
