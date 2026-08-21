#include "application_runtime.hpp"
#include "runtime/action_contract.hpp"
#include "runtime/action_router.hpp"
#include "runtime/feature_composition.hpp"

namespace kf2::app {

Result<bool> UiRuntime::set_overlay(bool enabled) {
    if (start_mode != StartMode::normal) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"This start mode does not permit persistent overlay changes", 0});
    }
    if (enabled == overlay_enabled) {
        return Result<bool>::success(overlay_enabled);
    }
    overlay_enabled = enabled;
    if (!enabled && overlay_window) {
        overlay::OverlayPresentation hidden;
        overlay_presentation = hidden;
        auto hidden_result = overlay_window->update(hidden);
        if (!hidden_result.has_value()) return hidden_result;
    }
    telemetry_tick();
    optimizer_settings.overlay_enabled = overlay_enabled;
    const auto saved = platform::windows::atomic_replace_utf8(
        settings_path, config::serialize_settings(optimizer_settings));
    if (!saved.has_value()) return Result<bool>::failure(saved.error());
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

Result<config::ApplyResult> UiRuntime::apply_adaptive_launch_profile() {
    if (!adaptive_locks_valid) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::access_denied,
            L"Adaptive locks are invalid; automatic launch fails closed",
            0});
    }
    const auto profile = optimizer::bound_adaptive_profile(
        stored_adaptive_profile(optimizer_settings),
        optimizer_settings.adaptive_minimum_quality,
        optimizer_settings.adaptive_maximum_quality);
    const bool protected_profile_capability_requested =
        should_prepare_protected_gameplay_provider(start_mode);
    if (protected_profile_capability_requested && !profile) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::invalid_argument,
            L"No verified Adaptive profile fits the selected quality limits",
            0});
    }
    const auto selected_profile = profile.value_or(
        stored_adaptive_profile(optimizer_settings));
    auto decision = optimizer::evaluate({
        .target_fps = optimizer_settings.target_fps,
        .quality = protected_profile_capability_requested
            ? optimizer::QualityPolicy::performance
            : optimizer::QualityPolicy::exact,
        .profile = selected_profile,
        .profile_preview_requested = true,
        .evidence = optimizer_evidence,
    });
    if (protected_profile_capability_requested) {
        const auto corpse_change = std::find_if(
            decision.changes.begin(), decision.changes.end(),
            [](const config::RequestedChange& change) {
                return change.id == config::SettingId::corpse_limit;
            });
        if (corpse_change != decision.changes.end()) {
            corpse_change->value = optimizer_settings.corpse_limit;
            corpse_change->reason =
                L"Use the selected Adaptive corpse ceiling; the protected "
                L"runtime provider only reduces it during confirmed pressure";
        }
    }
    auto changes = optimizer::filter_adaptive_locked_changes(
        decision.changes, adaptive_locks);
    if (changes.empty()) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::access_denied,
            L"Every verified Adaptive launch setting has an explicit safety lock",
            0});
    }
    changes.push_back({
        config::SettingId::physx_level, 2,
        config::ChangeSource::adaptive,
        L"Enable KF2's verified NVIDIA FleX Gibs and Fluids launch level; "
        L"the exact pre-game INI snapshot is restored after the session"});
    auto prepared = prepare(
        changes, protected_profile_capability_requested
            ? L"General Adaptive launch plan with protected profile capability " +
                  std::wstring{
                      optimizer::adaptive_profile_label(selected_profile)}
            : L"General Adaptive launch plan with frame-pacing and FleX capability");
    if (!prepared.has_value()) {
        return Result<config::ApplyResult>::failure(prepared.error());
    }
    auto applied = apply({.game_running = false});
    if (!applied.has_value()) return applied;
    last_backup_id = applied.value().backup.id;
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_LAUNCH_PROFILE_APPLIED",
        protected_profile_capability_requested
            ? L"General Adaptive applied frame pacing plus the available bounded " +
                  std::wstring{
                      optimizer::adaptive_profile_label(selected_profile)} +
                  L" protected-provider profile capability; individual locks were preserved and the exact pre-game snapshot remains protected"
            : L"General Adaptive applied frame pacing and the verified NVIDIA FleX Gibs and Fluids launch level; the exact pre-game snapshot remains protected",
        L"optimizer"});
    return applied;
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
        show_notice(ui::NoticeSeverity::warning, L"MODE_READ_ONLY",
                    L"Settings cannot be changed in this start mode.");
        return;
    }

    const auto control = runtime::resolve_control(id, requested_value);
    if (!control) return;
    const int value = control->value;

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
    } else if (control->id == runtime::ControlId::adaptive_minimum) {
        optimizer_settings.adaptive_minimum_quality = value;
        optimizer_settings.adaptive_maximum_quality = std::max(
            optimizer_settings.adaptive_maximum_quality,
            optimizer_settings.adaptive_minimum_quality);
        adaptive_policy_changed = true;
        code = L"ADAPTIVE_MINIMUM_CHANGED";
        message = L"Minimum adaptive quality: " +
            std::to_wstring(optimizer_settings.adaptive_minimum_quality) + L" %";
    } else if (control->id == runtime::ControlId::adaptive_maximum) {
        optimizer_settings.adaptive_maximum_quality = value;
        optimizer_settings.adaptive_minimum_quality = std::min(
            optimizer_settings.adaptive_minimum_quality,
            optimizer_settings.adaptive_maximum_quality);
        adaptive_policy_changed = true;
        code = L"ADAPTIVE_MAXIMUM_CHANGED";
        message = L"Maximum adaptive quality: " +
            std::to_wstring(optimizer_settings.adaptive_maximum_quality) + L" %";
    } else if (control->id == runtime::ControlId::adaptive_headroom) {
        optimizer_settings.adaptive_headroom_percent = value;
        adaptive_policy_changed = true;
        code = L"ADAPTIVE_HEADROOM_CHANGED";
        message = L"Adaptive performance headroom: " +
            std::to_wstring(optimizer_settings.adaptive_headroom_percent) + L" %";
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
        auto generation = adaptive_actuation.generation();
        generation.settings = ++adaptive_settings_generation;
        adaptive_actuation.rebase(generation, monotonic_ns());
        if (optimizer_settings.target_fps == previous.target_fps) {
            adaptive_governor.reset();
        }
        adaptive_profile_gate.reset();
        adaptive_overhead_breaches = 0;
        adaptive_overhead_frozen = false;
    }
    preview.reset();
    model.clear_preview_summary();
    auto status = model.status();
    status.target_fps = optimizer_settings.target_fps;
    status.corpse_limit = optimizer_settings.corpse_limit;
    status.overlay_scale_percent = optimizer_settings.overlay_scale_percent;
    status.config = installation ? ui::ConfigWorkflowState::detected
                                 : ui::ConfigWorkflowState::unavailable;
    update_adaptive_policy_status(status);
    model.set_status(std::move(status));

    if (overlay_changed) telemetry_tick();
    show_notice(ui::NoticeSeverity::info, std::move(code),
                std::move(message));
}

void UiRuntime::execute_action(std::string_view action) {
    const auto notice = [this](ui::NoticeSeverity severity,
                               std::wstring code,
                               std::wstring message) {
        model.set_notice(
            {severity, std::move(code), std::move(message), L""});
        invalidate();
    };

    constexpr bool protected_game_launch = true;
    const auto resolved = runtime::resolve_action(
        action, {.protected_game_launch = protected_game_launch});
    if (!resolved) return;
    if (resolved->normal_mode_required &&
        start_mode != StartMode::normal) {
        notice(ui::NoticeSeverity::warning, L"MODE_READ_ONLY",
               L"This start mode does not permit persistent changes. Restart normally to modify files.");
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
             L"This start mode does not permit persistent configuration changes", 0});
    }
    if (!preview) return Result<config::ApplyResult>::failure(
        {ErrorCode::invalid_argument, L"No configuration preview is ready", 0});
    auto applied = config::apply_preview(*preview, backups, preconditions);
    auto status = model.status();
    status.config = applied.has_value() ? ui::ConfigWorkflowState::restore_available
                                        : ui::ConfigWorkflowState::apply_blocked;
    model.set_status(std::move(status));
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
             L"This start mode does not permit persistent configuration changes", 0});
    }
    if (!installation) {
        return Result<backup::RestoreResult>::failure(
            {ErrorCode::not_found,
             L"A verified KF2 installation is required before restore", 0});
    }
    auto restored = backup::restore_backup(
        backups, id, installation->config_root, preconditions);
    auto status = model.status();
    status.config = restored.has_value() ? ui::ConfigWorkflowState::detected
                                         : ui::ConfigWorkflowState::apply_blocked;
    if (restored.has_value()) model.clear_preview_summary();
    model.set_status(std::move(status));
    if (restored.has_value()) {
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
