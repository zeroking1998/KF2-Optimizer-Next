#include "application_runtime.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "runtime/action_contract.hpp"
#include "runtime/action_router.hpp"
#include "runtime/feature_composition.hpp"

#include <algorithm>

namespace kf2::app {

void preserve_user_flex_activation(
    std::vector<config::RequestedChange>& changes) noexcept {
    std::erase_if(changes, [](const config::RequestedChange& change) {
        return change.id == config::SettingId::physx_level;
    });
}

void enforce_temporal_aa_disabled(
    std::vector<config::RequestedChange>& changes) noexcept {
    const auto existing = std::find_if(
        changes.begin(), changes.end(), [](const config::RequestedChange& change) {
            return change.id == config::SettingId::temporal_aa;
        });
    const config::RequestedChange required{
        config::SettingId::temporal_aa, false,
        config::ChangeSource::explicit_user,
        L"Disable temporal frame-history anti-aliasing to prevent ghosting"};
    if (existing == changes.end()) {
        changes.push_back(required);
    } else {
        *existing = required;
    }
}

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

void UiRuntime::refresh_video_presentation() {
    auto status = model.status();
    status.graphics_available = video_pending.has_value();
    status.graphics_game_running = installation &&
        game::find_running_game_process(installation->executable).has_value();
    if (video_pending) {
        for (std::size_t option = 0; option < game::kVideoOptionCount; ++option) {
            status.graphics_values[option] = game::video_choice_label(
                static_cast<game::VideoOption>(option), *video_pending);
        }
        status.graphics_aspect_ratio = game::aspect_ratio_label(*video_pending);
        status.graphics_film_grain_percent = video_pending->film_grain_percent;
        status.graphics_dirty = video_saved &&
            (video_pending->choices != video_saved->choices ||
             video_pending->film_grain_percent != video_saved->film_grain_percent);
    } else {
        status.graphics_dirty = false;
    }
    model.set_status(std::move(status));
}

void UiRuntime::reload_video_settings() {
    if (!installation) {
        video_saved.reset();
        video_pending.reset();
        refresh_video_presentation();
        return;
    }
    const auto loaded = game::read_video_settings(installation->config_root);
    if (!loaded.has_value()) {
        video_saved.reset();
        video_pending.reset();
        refresh_video_presentation();
        model.set_notice({ui::NoticeSeverity::warning, L"GRAPHICS_UNAVAILABLE",
                          loaded.error().message, L""});
        return;
    }
    video_saved = loaded.value();
    video_pending = loaded.value();
    refresh_video_presentation();
}

void UiRuntime::cycle_video_option(game::VideoOption option) {
    if (!installation || !video_pending) {
        reload_video_settings();
        if (!video_pending) return;
    }
    if (game::find_running_game_process(installation->executable).has_value()) {
        model.set_notice({ui::NoticeSeverity::warning, L"GRAPHICS_GAME_RUNNING",
                          L"Close KF2 before changing its video settings.", L""});
        invalidate();
        return;
    }
    const auto selected = static_cast<std::size_t>(option);
    const int count = game::video_choice_count(option, *video_pending);
    const int current = video_pending->choices[selected];
    video_pending->choices[selected] =
        current < 0 || current >= count - 1 ? 0 : current + 1;
    if (option == game::VideoOption::overall_quality) {
        const int preset = video_pending->choices[selected];
        constexpr std::array<std::array<int, 15>, 4> presets{{
            {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
            {{1,0,1,1,1,1,0,1,1,0,0,0,0,0,0}},
            {{2,1,2,2,2,2,0,1,2,0,1,1,1,1,1}},
            {{3,2,3,3,3,3,1,1,2,1,2,1,1,1,1}},
        }};
        constexpr std::array<game::VideoOption, 15> targets{{
            game::VideoOption::environment_detail,
            game::VideoOption::character_detail,
            game::VideoOption::fx_quality,
            game::VideoOption::texture_resolution,
            game::VideoOption::texture_filtering,
            game::VideoOption::shadow_quality,
            game::VideoOption::realtime_reflections,
            game::VideoOption::anti_aliasing,
            game::VideoOption::bloom,
            game::VideoOption::motion_blur,
            game::VideoOption::ambient_occlusion,
            game::VideoOption::depth_of_field,
            game::VideoOption::volumetric_lighting,
            game::VideoOption::lens_flares,
            game::VideoOption::light_shafts,
        }};
        for (std::size_t index = 0; index < targets.size(); ++index) {
            video_pending->choices[static_cast<std::size_t>(targets[index])] =
                presets[preset][index];
        }
        // Overall quality deliberately does not touch NVIDIA FleX. FleX is a
        // separate explicit user choice and Adaptive never enables it.
    }
    refresh_video_presentation();
    model.set_notice({ui::NoticeSeverity::info, L"GRAPHICS_STAGED",
                      std::wstring{game::video_option_label(option)} +
                          L" is staged. Select Apply graphics to save it.",
                      L""});
    invalidate();
}

void UiRuntime::reset_video_settings() {
    if (video_pending) {
        video_pending = game::recommended_video_defaults(*video_pending);
    }
    refresh_video_presentation();
    model.set_notice({ui::NoticeSeverity::info, L"GRAPHICS_RESET",
                      L"Recommended graphics defaults are ready. Display mode and resolution were kept. Select Apply graphics to save them.",
                      L""});
    invalidate();
}

Result<config::ApplyResult> UiRuntime::apply_video_settings() {
    if (!installation || !video_pending || !video_saved) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::not_found, L"KF2 video settings are unavailable", 0});
    }
    if (video_pending->choices == video_saved->choices &&
        video_pending->film_grain_percent == video_saved->film_grain_percent) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::invalid_argument, L"No video changes are staged", 0});
    }
    const bool running = game::find_running_game_process(
        installation->executable).has_value();
    if (running) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::access_denied, L"Close KF2 before applying video settings", 0});
    }
    auto prepared = game::build_video_preview(
        installation->config_root, *video_pending);
    if (!prepared.has_value()) {
        return Result<config::ApplyResult>::failure(prepared.error());
    }
    preview = std::move(prepared.value());
    preview_context = L"Explicit KF2 video settings";
    auto result = apply({.game_running = false});
    if (result.has_value()) {
        last_backup_id = result.value().backup.id;
        events->append({0, diagnostics::Severity::info,
                        "GRAPHICS_EXPLICIT_APPLIED",
                        L"Explicit user-selected KF2 video settings were applied; FleX was changed only if its dedicated choice changed",
                        L"graphics"});
        reload_video_settings();
    }
    return result;
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
    preserve_user_flex_activation(changes);
    enforce_temporal_aa_disabled(changes);
    if (changes.empty()) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::access_denied,
            L"Every verified Adaptive launch setting has an explicit safety lock",
            0});
    }
    auto prepared = prepare(
        changes, protected_profile_capability_requested
            ? L"General Adaptive launch plan with protected profile capability " +
                  std::wstring{
                      optimizer::adaptive_profile_label(selected_profile)}
            : L"General Adaptive launch plan preserving the user's FleX setting");
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
            : L"General Adaptive applied frame pacing while preserving the user's FleX setting; the exact pre-game snapshot remains protected",
        L"optimizer"});
    return applied;
}

Result<bool> UiRuntime::prepare_automatic_protected_launch_capabilities() {
    if (!installation || !session_config_snapshot) {
        return Result<bool>::failure({
            ErrorCode::invalid_argument,
            L"A protected KF2 configuration snapshot is required before launch capabilities are prepared",
            0});
    }

    const auto captured_values = config::read_catalog_values(
        session_config_snapshot->snapshot_root / L"files");
    if (!captured_values.has_value()) {
        return Result<bool>::failure(captured_values.error());
    }
    const auto physx = captured_values.value().find(
        config::SettingId::physx_level);
    const auto* configured_physx_level =
        physx == captured_values.value().end()
            ? nullptr : std::get_if<int>(&physx->second);
    if (!configured_physx_level) {
        return Result<bool>::failure({
            ErrorCode::stale_data,
            L"The user's captured KF2 FleX setting could not be verified",
            0});
    }

    if (should_prepare_adaptive_flex_runtime(
            start_mode, *configured_physx_level)) {
        const auto prepared = ensure_automatic_flex_lab();
        if (!prepared.has_value()) {
            return Result<bool>::failure(prepared.error());
        }
    } else {
        events->append({0, diagnostics::Severity::info,
            "FLEX_USER_SETTING_PRESERVED",
            L"FleX remains disabled because the user did not enable it in KF2; no FleX runtime hook was installed",
            L"flex"});
    }

    if (!should_prepare_protected_gameplay_provider(start_mode)) {
        return Result<bool>::success(true);
    }
    const auto control_token = game::generate_adaptive_control_token();
    if (!control_token.has_value()) {
        return Result<bool>::failure(control_token.error());
    }
    adaptive_control_token = control_token.value();
    adaptive_control_sequence = 0;
    adaptive_quality_last_dispatch_ns = 0;
    adaptive_runtime_quality = optimizer_settings.adaptive_maximum_quality;
    const auto telemetry_module = game::install_offline_telemetry_lab({
        .config_root = installation->config_root,
        .state_root = settings_path.parent_path(),
        .module_asset = executable_root / L"Data" / L"Lab" /
            L"KF2OptimizerTelemetry.u",
        .game_running = false});
    if (!telemetry_module.has_value()) {
        return Result<bool>::failure(telemetry_module.error());
    }
    const auto enabled = game::enable_offline_gameplay_logging(
        installation->config_root, true, optimizer_settings.corpse_limit,
        optimizer_settings.target_fps, true,
        optimizer_settings.adaptive_quality_change_budget,
        adaptive_control_token);
    if (!enabled.has_value()) {
        adaptive_control_token.clear();
        return Result<bool>::failure(enabled.error());
    }
    events->append({0, diagnostics::Severity::info,
        "GAMEPLAY_LOG_LAB_READY",
        L"The protected published provider is ready for KF2 started from the optimizer, Steam or a shortcut and exposes verified AI, wave, corpse, physics, LOD and ragdoll capabilities",
        L"game"});
    return Result<bool>::success(true);
}

Result<bool> UiRuntime::prepare_automatic_external_launch_profile() {
    if (start_mode != StartMode::normal || !installation) {
        return Result<bool>::success(false);
    }
    if (session_config_snapshot) {
        return Result<bool>::success(
            session_config_waiting_for_launch &&
            session_config_launch_deadline_ns == 0);
    }
    if (game::find_running_game_process(
            installation->executable).has_value()) {
        events->append({0, diagnostics::Severity::warning,
            "ADAPTIVE_EXTERNAL_LAUNCH_TOO_LATE",
            L"KF2 was already running before the protected Adaptive profile could be prepared; restart KF2 while the optimizer remains open to apply Target FPS",
            L"optimizer"});
        return Result<bool>::success(false);
    }

    auto captured = config::capture_session_config(
        installation->config_root, settings_path.parent_path());
    if (!captured.has_value()) {
        return Result<bool>::failure(captured.error());
    }
    session_config_snapshot = std::move(captured.value());
    events->append({0, diagnostics::Severity::info,
        "SESSION_CONFIG_CAPTURED",
        L"The exact pre-game KF2 INI state was captured before automatic external-launch preparation",
        L"config"});

    const auto applied = apply_adaptive_launch_profile();
    if (!applied.has_value()) {
        const auto error = applied.error();
        static_cast<void>(restore_protected_session_config(
            L"Automatic external-launch preparation failed"));
        return Result<bool>::failure(error);
    }
    const auto capabilities =
        prepare_automatic_protected_launch_capabilities();
    if (!capabilities.has_value()) {
        const auto error = capabilities.error();
        static_cast<void>(restore_protected_session_config(
            L"Automatic external-launch capability preparation failed"));
        return Result<bool>::failure(error);
    }

    // A zero deadline deliberately means that the verified profile remains
    // staged while the optimizer is open. The snapshot is restored on app
    // shutdown, or after the next observed KF2 process exits, while the fixed
    // temporal-AA safety override remains disabled.
    session_config_waiting_for_launch = true;
    session_config_launch_deadline_ns = 0;
    telemetry_failure = L"Adaptive profile ready; waiting for KF2";
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_EXTERNAL_LAUNCH_READY",
        L"The protected Adaptive profile, telemetry provider and user-authorized FleX state are ready for KF2 started from the optimizer, Steam or a shortcut",
        L"optimizer"});
    return Result<bool>::success(true);
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
        refresh_video_presentation();
        show_notice(ui::NoticeSeverity::info, L"GRAPHICS_STAGED",
                    L"Film grain is staged. Select Apply graphics to save it.");
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
    auto status = model.status();
    status.target_fps = optimizer_settings.target_fps;
    status.corpse_limit = optimizer_settings.corpse_limit;
    status.overlay_scale_percent = optimizer_settings.overlay_scale_percent;
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
