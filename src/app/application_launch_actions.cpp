#include "application_runtime.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "kf2/config/startup_movies.hpp"
#include "kf2/optimizer/startup_gpu_profile.hpp"

#include <algorithm>

namespace kf2::app {
namespace {

void upsert_startup_change(
    std::vector<config::RequestedChange>& changes, config::SettingId id,
    config::SettingValue value, std::wstring_view reason) {
    const auto existing = std::find_if(
        changes.begin(), changes.end(), [id](const auto& change) {
            return change.id == id;
        });
    const config::RequestedChange required{
        id, std::move(value), config::ChangeSource::explicit_user,
        std::wstring{reason}};
    if (existing == changes.end()) {
        changes.push_back(required);
    } else {
        *existing = required;
    }
}

std::optional<std::wstring_view> persisted_physical_gpu_key(
    const std::vector<telemetry::GpuAdapter>& adapters,
    const config::Settings& settings) {
    const auto stored = settings.extras.find("confirmed_gpu_physical_key");
    if (stored == settings.extras.end() || stored->second.empty()) {
        return std::nullopt;
    }
    for (const auto& adapter : adapters) {
        const auto encoded = path_utf8(
            std::filesystem::path{adapter.physical_device_key});
        if (encoded && *encoded == stored->second) {
            return adapter.physical_device_key;
        }
    }
    return std::nullopt;
}

}  // namespace

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

void enforce_async_physics_enabled(
    std::vector<config::RequestedChange>& changes) noexcept {
    upsert_startup_change(
        changes, config::SettingId::physics_async_scene, true,
        L"Enable the standard asynchronous physics scene at KF2 startup");
    upsert_startup_change(
        changes, config::SettingId::enable_async_scene, true,
        L"Enable asynchronous scene processing at KF2 startup");
}

void enforce_one_frame_thread_lag(
    std::vector<config::RequestedChange>& changes) noexcept {
    upsert_startup_change(
        changes, config::SettingId::one_frame_thread_lag, true,
        L"Enable KF2's native one-frame render-thread pipeline at startup");
}

void enforce_startup_memory_profile(
    std::vector<config::RequestedChange>& changes,
    const optimizer::StartupMemoryProfile& profile) noexcept {
    upsert_startup_change(
        changes, config::SettingId::texture_pool_size,
        profile.texture_pool_size_mb,
        L"Size KF2's native texture pool from detected dedicated VRAM");
    upsert_startup_change(
        changes, config::SettingId::texture_streaming_memory_margin,
        profile.memory_margin_mb,
        L"Reserve a bounded native texture-streaming memory margin");
    upsert_startup_change(
        changes, config::SettingId::texture_streaming_hysteresis_limit,
        profile.streaming_hysteresis_limit,
        L"Use bounded native texture-streaming hysteresis");
}

Result<config::ApplyResult> UiRuntime::apply_adaptive_launch_profile() {
    const auto apply_launch_preview = [this]() -> Result<config::ApplyResult> {
        if (!preview) {
            return Result<config::ApplyResult>::failure(
                {ErrorCode::internal_failure,
                 L"The protected launch preview is unavailable", 0});
        }
        const auto logos = config::stage_startup_logo_skip(*preview);
        if (!logos.has_value()) {
            return Result<config::ApplyResult>::failure(logos.error());
        }
        auto applied = apply({.game_running = false});
        if (applied.has_value() && logos.value().removed_logos != 0) {
            events->append({0, diagnostics::Severity::info,
                "STARTUP_LOGOS_SKIPPED",
                L"KF2's four startup logos were skipped for this protected session; the main-menu background and map-loading movies remain unchanged",
                L"game"});
        }
        return applied;
    };
    if (!optimizer_settings.adaptive_optimization_enabled) {
        auto prepared = prepare({{
            config::SettingId::corpse_limit,
            optimizer_settings.corpse_limit,
            config::ChangeSource::explicit_user,
            L"Keep the user-selected maximum corpse count while Adaptive optimization is off"}},
            L"Fixed user goals with Adaptive optimization off");
        if (!prepared.has_value()) {
            return Result<config::ApplyResult>::failure(prepared.error());
        }
        auto applied = apply_launch_preview();
        if (applied.has_value()) {
            last_backup_id = applied.value().backup.id;
            events->append({0, diagnostics::Severity::info,
                "ADAPTIVE_LAUNCH_SKIPPED",
                L"Adaptive launch changes were skipped; only the selected maximum corpse count and independent native FPS cap remain active",
                L"optimizer"});
        }
        return applied;
    }
    if (!adaptive_locks_valid) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::access_denied,
            L"Adaptive locks are invalid; automatic launch fails closed",
            0});
    }
    // The protected launch only stages runtime capabilities. It must never
    // replace the graphics selected by the user with a named startup profile.
    // Adaptive may request individual runtime changes later, after validated
    // gameplay telemetry confirms pressure.
    std::vector<config::RequestedChange> changes{{
        config::SettingId::corpse_limit,
        optimizer_settings.corpse_limit,
        config::ChangeSource::explicit_user,
        L"Keep the user-selected maximum corpse count; the protected runtime "
        L"provider only reduces it during confirmed pressure"}};
    enforce_temporal_aa_disabled(changes);
    enforce_async_physics_enabled(changes);
    enforce_one_frame_thread_lag(changes);
    std::optional<optimizer::StartupMemoryProfile> startup_memory_profile;
    std::wstring startup_memory_adapter_name;
    std::wstring startup_memory_source;
    const auto adapters = telemetry::enumerate_gpu_adapters();
    if (adapters.has_value()) {
        const auto physical = telemetry::unique_physical_gpu_adapters(
            adapters.value());
        std::wstring configured_key_storage;
        std::optional<std::wstring_view> configured_key;
        std::optional<telemetry::ProcessGpuPreference> current_preference;
        if (installation) {
            const auto configured =
                telemetry::configured_gpu_adapter_for_process(
                    installation->executable);
            if (configured.has_value()) {
                current_preference = configured.value().preference;
                if (configured.value().adapter_luid) {
                    const auto configured_adapter =
                        telemetry::find_hardware_gpu_adapter_by_luid(
                            adapters.value(),
                            *configured.value().adapter_luid);
                    if (configured_adapter &&
                        !configured_adapter->physical_device_key.empty()) {
                        configured_key_storage =
                            configured_adapter->physical_device_key;
                        configured_key = configured_key_storage;
                    }
                }
            }
        }
        const auto previous_key = persisted_physical_gpu_key(
            physical, optimizer_settings);
        const auto stored_preference = optimizer_settings.extras.find(
            "confirmed_gpu_preference");
        const bool preference_matches =
            current_preference &&
            stored_preference != optimizer_settings.extras.end() &&
            stored_preference->second ==
                telemetry::process_gpu_preference_token(*current_preference);
        const auto resolved = optimizer::resolve_startup_gpu_profile(
            physical, configured_key, previous_key, preference_matches);
        if (resolved) {
            startup_memory_profile = resolved->profile;
            startup_memory_source = std::wstring{
                optimizer::startup_gpu_profile_source_label(resolved->source)};
            startup_memory_adapter_name = resolved->adapter
                ? resolved->adapter->name : L"all detected adapters";
        }
    }
    if (startup_memory_profile) {
        enforce_startup_memory_profile(changes, *startup_memory_profile);
    }
    if (changes.empty()) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::access_denied,
            L"Every verified Adaptive launch setting has an explicit safety lock",
            0});
    }
    auto prepared = prepare(
        changes,
        L"Protected Adaptive runtime capabilities preserving the user's "
        L"graphics and FleX settings");
    if (!prepared.has_value()) {
        return Result<config::ApplyResult>::failure(prepared.error());
    }
    auto applied = apply_launch_preview();
    if (!applied.has_value()) return applied;
    last_backup_id = applied.value().backup.id;
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_LAUNCH_SETTINGS_APPLIED",
        L"General Adaptive preserved the user's graphics and FleX settings "
        L"while staging runtime capabilities; the selected maximum corpse "
        L"count and exact pre-game snapshot remain protected",
        L"optimizer"});
    if (startup_memory_profile) {
        events->append({0, diagnostics::Severity::info,
            "STARTUP_MEMORY_PROFILE_APPLIED",
            L"KF2 startup memory uses the " + startup_memory_source + L" " +
                startup_memory_adapter_name + L": texture pool " +
                std::to_wstring(
                    startup_memory_profile->texture_pool_size_mb) +
                L" MB, memory margin " +
                std::to_wstring(startup_memory_profile->memory_margin_mb) +
                L" MB, streaming hysteresis " +
                std::to_wstring(
                    startup_memory_profile->streaming_hysteresis_limit),
            L"optimizer"});
    } else {
        events->append({0, diagnostics::Severity::info,
            "STARTUP_MEMORY_PROFILE_PRESERVED",
            L"KF2 startup memory values were preserved because no safe adapter memory budget was available",
            L"optimizer"});
    }
    return applied;
}

Result<game::FrameRateCapResult> UiRuntime::synchronize_frame_rate_cap() {
    if (!installation) {
        return Result<game::FrameRateCapResult>::failure(
            {ErrorCode::not_found, L"Game not detected", 0});
    }
    return game::persist_frame_rate_cap(
        *installation, optimizer_settings.target_fps);
}

Result<bool> UiRuntime::apply_overlay_compatible_display_mode() {
    if (!overlay_enabled || !installation) {
        return Result<bool>::success(false);
    }
    auto current = game::read_video_settings(installation->config_root);
    if (!current.has_value()) {
        return Result<bool>::failure(current.error());
    }
    const auto display = static_cast<std::size_t>(game::VideoOption::display);
    constexpr int kExclusiveFullscreen = 2;
    constexpr int kBorderlessFullscreen = 1;
    if (current.value().choices[display] != kExclusiveFullscreen) {
        return Result<bool>::success(false);
    }
    current.value().choices[display] = kBorderlessFullscreen;
    auto prepared = game::build_video_preview(
        installation->config_root, current.value());
    if (!prepared.has_value()) {
        return Result<bool>::failure(prepared.error());
    }
    preview = std::move(prepared.value());
    preview_context = L"Temporary overlay-compatible fullscreen mode";
    auto applied = apply({.game_running = false});
    if (!applied.has_value()) {
        return Result<bool>::failure(applied.error());
    }
    last_backup_id = applied.value().backup.id;
    events->append({0, diagnostics::Severity::info,
        "OVERLAY_FULLSCREEN_COMPATIBILITY_APPLIED",
        L"Exclusive fullscreen was changed to borderless fullscreen for this protected session so the desktop overlay remains visible; the original display mode will be restored after KF2 exits",
        L"overlay"});
    return Result<bool>::success(true);
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

    if (should_prepare_fixed_flex_runtime(
            start_mode, *configured_physx_level)) {
        const auto prepared = ensure_fixed_flex_runtime();
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
    adaptive_quality_last_applied_ns = 0;
    adaptive_quality_reduction_floor.reset(
        optimizer_settings.adaptive_minimum_quality);
    adaptive_quality_rollback_target.reset();
    adaptive_quality_rollback_resource.reset();
    adaptive_frame_not_before_ns = 0;
    adaptive_map_ready_ns = 0;
    adaptive_resource_quality.reset(
        optimizer_settings.adaptive_maximum_quality);
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
        optimizer_settings.target_fps,
        optimizer_settings.debug_corpse_markers,
        optimizer_settings.adaptive_quality_change_budget,
        adaptive_control_token, optimizer_settings.debug_zed_markers,
        optimizer_settings.adaptive_optimization_enabled);
    if (!enabled.has_value()) {
        adaptive_control_token.clear();
        return Result<bool>::failure(enabled.error());
    }
    events->append({0, diagnostics::Severity::info,
        "GAMEPLAY_PROVIDER_PREPARED",
        L"The protected Published provider was staged for the next KF2 start; runtime capabilities remain unavailable until KF2 confirms telemetry",
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
            L"KF2 was already running before the protected Adaptive runtime capabilities could be prepared; the native FPS cap remains independent and uses the saved value after restart",
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
    const auto overlay_display = apply_overlay_compatible_display_mode();
    if (!overlay_display.has_value()) {
        const auto error = overlay_display.error();
        static_cast<void>(restore_protected_session_config(
            L"Overlay-compatible fullscreen preparation failed"));
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

    // A zero deadline deliberately means that the verified runtime
    // capabilities remain staged while the optimizer is open. The snapshot is restored on app
    // shutdown, or after the next observed KF2 process exits, while the fixed
    // temporal-AA safety override remains disabled.
    session_config_waiting_for_launch = true;
    session_config_launch_deadline_ns = 0;
    telemetry_failure = L"Adaptive runtime prepared; waiting for KF2 confirmation";
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_EXTERNAL_LAUNCH_PREPARED",
        L"The protected Published provider and Adaptive runtime capabilities were staged for KF2 started from the optimizer, Steam or a shortcut; user graphics were preserved and runtime capabilities require telemetry confirmation",
        L"optimizer"});
    return Result<bool>::success(true);
}

Result<bool> UiRuntime::rearm_automatic_external_launch_profile() {
    if (model.recovery_required() || start_mode != StartMode::normal ||
        !installation) {
        return Result<bool>::success(false);
    }

    const auto prepared = prepare_automatic_external_launch_profile();
    if (!prepared.has_value()) {
        events->append({0, diagnostics::Severity::error,
            "ADAPTIVE_EXTERNAL_LAUNCH_REARM_FAILED",
            prepared.error().message, L"optimizer"});
        model.set_notice({ui::NoticeSeverity::error,
            L"ADAPTIVE_EXTERNAL_LAUNCH_REARM_FAILED",
            L"The next KF2 start could not be prepared safely: " +
                prepared.error().message,
            L"Do not start KF2 again until Repair reports that all protected files are healthy."});
        invalidate();
        return Result<bool>::failure(prepared.error());
    }
    if (!prepared.value()) return prepared;

    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_EXTERNAL_LAUNCH_REARMED",
        L"The protected Adaptive runtime capabilities were prepared again for the next KF2 start without replacing user graphics",
        L"optimizer"});
    return prepared;
}

}  // namespace kf2::app
