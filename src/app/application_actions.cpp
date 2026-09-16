#include "application_runtime.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "kf2/config/startup_movies.hpp"
#include "kf2/optimizer/startup_gpu_profile.hpp"
#include "runtime/action_contract.hpp"
#include "runtime/action_router.hpp"
#include "runtime/feature_composition.hpp"

#include <algorithm>

namespace kf2::app {
namespace {

using VideoConfigWriteTimes =
    std::array<std::optional<std::filesystem::file_time_type>, 3>;

std::optional<VideoConfigWriteTimes> read_video_config_write_times(
    const std::filesystem::path& config_root) {
    static constexpr std::array<std::wstring_view, 3> files{
        L"KFSystemSettings.ini", L"KFGame.ini", L"KFEngine.ini"};
    VideoConfigWriteTimes times{};
    for (std::size_t index = 0; index < files.size(); ++index) {
        std::error_code error;
        const auto write_time = std::filesystem::last_write_time(
            config_root / files[index], error);
        if (!error) {
            times[index] = write_time;
        } else if (index != 2 ||
                   error != std::errc::no_such_file_or_directory) {
            return std::nullopt;
        }
    }
    return times;
}

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

void UiRuntime::refresh_video_presentation() {
    auto status = model.status();
    status.graphics_available = video_pending.has_value();
    status.graphics_game_running = installation &&
        game::find_running_game_process(installation->executable).has_value();
    status.graphics_game_menu_readback = status.graphics_game_running &&
        game_menu_graphics_readback.has_value();
    if (video_pending) {
        const auto presented = status.graphics_game_menu_readback
            ? game::present_game_menu_graphics_readback(
                *video_pending, *game_menu_graphics_readback)
            : *video_pending;
        for (std::size_t option = 0; option < game::kVideoOptionCount; ++option) {
            status.graphics_values[option] =
                status.graphics_game_menu_readback &&
                game_menu_graphics_readback->choices[option] == -1 &&
                option != static_cast<std::size_t>(
                    game::VideoOption::overall_quality) &&
                option != static_cast<std::size_t>(
                    game::VideoOption::resolution)
                ? L"INI override"
                : game::video_choice_label(
                    static_cast<game::VideoOption>(option), presented);
        }
        status.graphics_aspect_ratio = game::aspect_ratio_label(presented);
        status.graphics_film_grain_percent = presented.film_grain_percent;
    }
    model.set_status(std::move(status));
}

void UiRuntime::reload_video_settings() {
    if (session_config_snapshot && video_saved) {
        refresh_video_presentation();
        return;
    }
    if (!installation) {
        video_saved.reset();
        video_pending.reset();
        video_config_write_times.reset();
        refresh_video_presentation();
        return;
    }
    const auto write_times_before = read_video_config_write_times(
        installation->config_root);
    const auto graphics_source = session_config_snapshot
        ? session_config_snapshot->snapshot_root / L"files"
        : installation->config_root;
    const auto loaded = game::read_video_settings(graphics_source);
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
    const auto write_times_after = read_video_config_write_times(
        installation->config_root);
    video_config_write_times = write_times_before == write_times_after
        ? write_times_after : std::nullopt;
    refresh_video_presentation();
}

bool UiRuntime::synchronize_video_settings_from_game() {
    if (!installation || !video_saved || !video_pending) return false;
    // During a protected session, compare only with the already observed
    // temporary runtime profile. The original personal graphics remain
    // untouched until the snapshot has been restored.
    if (session_config_snapshot && !session_video_runtime) return false;
    const auto write_times = read_video_config_write_times(
        installation->config_root);
    if (!write_times ||
        (video_config_write_times && *video_config_write_times == *write_times)) {
        return false;
    }
    const auto current = game::read_video_settings(installation->config_root);
    if (!current.has_value()) return false;  // Retry after KF2 finishes writing.
    if (read_video_config_write_times(installation->config_root) != write_times) {
        return false;  // A game write overlapped the read.
    }
    if (session_config_snapshot) {
        const auto rebased = game::rebase_video_changes(
            session_video_native_changes.value_or(*video_saved),
            *session_video_runtime, current.value());
        if (!rebased.has_value()) return false;
        session_video_native_changes = rebased.value();
        session_video_runtime = current.value();
        video_config_write_times = *write_times;
        events->append({0, diagnostics::Severity::info,
            "KF2_RUNTIME_GRAPHICS_INI_CHANGED",
            L"KF2's live graphics INI changed; its confirmed delta will be retained separately from temporary session changes",
            L"graphics"});
        return true;
    }
    const auto rebased = game::rebase_video_changes(
        current.value(), *video_saved, *video_pending);
    if (!rebased.has_value()) return false;
    video_saved = current.value();
    video_pending = rebased.value();
    video_config_write_times = *write_times;
    refresh_video_presentation();
    invalidate();
    return true;
}

void UiRuntime::refresh_game_configuration_for_process_start(
    bool settings_restart) {
    if (!session_config_snapshot) reload_video_settings();
    const auto live = installation
        ? game::read_video_settings(installation->config_root)
        : Result<game::VideoSettings>::failure(
              {ErrorCode::not_found, L"KF2 installation unavailable", 0});
    if (!live.has_value()) {
        events->append({
            0, diagnostics::Severity::warning,
            settings_restart ? "KF2_NEW_SETTINGS_CONFIGURATION_UNAVAILABLE"
                             : "KF2_PROCESS_CONFIGURATION_UNAVAILABLE",
            L"The current KF2 graphics and FleX configuration could not be read for the verified process",
            L"graphics"});
        return;
    }

    // A protected launch stages temporary runtime/session values. Never
    // display those values as the user's saved graphics. Only a confirmed KF2
    // settings restart may contribute a native delta to the personal config.
    if (session_config_snapshot) {
        if (settings_restart && session_video_runtime && video_saved) {
            const auto rebased = game::rebase_video_changes(
                session_video_native_changes.value_or(*video_saved),
                *session_video_runtime, live.value());
            if (rebased.has_value()) {
                session_video_native_changes = rebased.value();
            }
        }
        session_video_runtime = live.value();
        video_config_write_times = read_video_config_write_times(
            installation->config_root);
    } else {
        session_video_runtime.reset();
        session_video_native_changes.reset();
    }

    const auto variable_index = static_cast<std::size_t>(
        game::VideoOption::variable_frame_rate);
    adaptive_variable_frame_rate_enabled =
        live.value().choices[variable_index] != 0;
    adaptive_frame_rate_mode_read_failed = false;
    std::error_code write_time_error;
    const auto game_config_write_time = std::filesystem::last_write_time(
        installation->config_root / L"KFGame.ini", write_time_error);
    if (write_time_error) {
        adaptive_frame_rate_config_write_time.reset();
    } else {
        adaptive_frame_rate_config_write_time = game_config_write_time;
    }

    const auto flex = game::video_choice_label(
        game::VideoOption::nvidia_flex, live.value());
    events->append({
        0, diagnostics::Severity::info,
        settings_restart ? "KF2_NEW_SETTINGS_CONFIGURATION_DETECTED"
                         : "KF2_PROCESS_CONFIGURATION_DETECTED",
        (settings_restart
             ? L"KF2 graphics settings were reloaded after the confirmed settings restart; configured NVIDIA FleX: "
             : L"KF2 graphics settings were loaded for the verified process; configured NVIDIA FleX: ") +
            flex,
        L"graphics"});
}

bool UiRuntime::reset_adaptive_frame_window_for_rate_mode_change(
    std::uint64_t now_ns, bool active_gameplay) {
    if (!installation || !game_process || now_ns == 0) return false;

    const auto game_config = installation->config_root / L"KFGame.ini";
    std::error_code write_time_error;
    const auto write_time = std::filesystem::last_write_time(
        game_config, write_time_error);
    if (write_time_error ||
        (adaptive_frame_rate_config_write_time &&
         *adaptive_frame_rate_config_write_time == write_time)) {
        return false;
    }

    const auto current = game::read_variable_frame_rate_enabled(
        installation->config_root);
    if (!current.has_value()) {
        if (!adaptive_frame_rate_mode_read_failed) {
            adaptive_frame_rate_mode_read_failed = true;
            events->append({
                0, diagnostics::Severity::warning,
                "ADAPTIVE_FRAME_RATE_MODE_UNAVAILABLE",
                L"KF2's current Variable frame rate mode could not be verified; Adaptive preserved its existing frame window",
                L"optimizer"});
        }
        return false;
    }

    adaptive_frame_rate_mode_read_failed = false;
    adaptive_frame_rate_config_write_time = write_time;
    const auto previous = adaptive_variable_frame_rate_enabled;
    adaptive_variable_frame_rate_enabled = current.value();
    if (!previous || *previous == current.value() || !active_gameplay) {
        return false;
    }

    adaptive_frame_not_before_ns = now_ns;
    adaptive_governor.reset();
    adaptive_decision = {};
    quality_response = {};
    last_adaptive_state = optimizer::AdaptiveControllerState::observing;
    last_adaptive_disposition = optimizer::AdaptiveDisposition::hold;
    last_adaptive_bottleneck = optimizer::AdaptiveBottleneck::unknown;
    last_adaptive_decision_log_ns = 0;
    last_frame_metrics = {};
    if (present_source) present_source->reset_statistics();
    events->append({
        0, diagnostics::Severity::info,
        "ADAPTIVE_FRAME_RATE_MODE_CHANGED",
        std::wstring{L"Variable frame rate changed to "} +
            (current.value() ? L"On" : L"Off") +
            L"; Adaptive discarded the mixed capped/uncapped frame evidence and is collecting a fresh gameplay window",
        L"optimizer"});
    return true;
}

void UiRuntime::cycle_video_option(game::VideoOption option) {
    if (start_mode != StartMode::normal) return;
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
    save_video_selection();
}

void UiRuntime::save_video_selection() {
    const auto result = apply_video_settings();
    if (!result.has_value()) {
        if (session_config_snapshot && video_saved) {
            video_pending = video_saved;
            refresh_video_presentation();
        } else {
            reload_video_settings();
        }
    }
    model.set_notice({
        result.has_value() ? ui::NoticeSeverity::info
                           : ui::NoticeSeverity::warning,
        result.has_value() ? L"GRAPHICS_SAVED" : L"GRAPHICS_SAVE_FAILED",
        result.has_value()
            ? L"KF2 graphics saved and verified. A restore backup is available."
            : L"Graphics were not changed: " + result.error().message,
        L""});
    invalidate();
}

void UiRuntime::reset_video_settings() {
    if (start_mode != StartMode::normal) return;
    if (!video_pending) reload_video_settings();
    if (!video_pending) return;
    if (installation && game::find_running_game_process(
            installation->executable).has_value()) {
        model.set_notice({ui::NoticeSeverity::warning, L"GRAPHICS_GAME_RUNNING",
                          L"Close KF2 before changing its video settings.", L""});
        invalidate();
        return;
    }
    video_pending = game::recommended_video_defaults(*video_pending);
    save_video_selection();
}

Result<config::ApplyResult> UiRuntime::apply_video_settings() {
    static_cast<void>(synchronize_video_settings_from_game());
    if (!installation || !video_pending || !video_saved) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::not_found, L"KF2 video settings are unavailable", 0});
    }
    video_pending->choices[static_cast<std::size_t>(
        game::VideoOption::vsync)] = 0;
    video_pending->choices[static_cast<std::size_t>(
        game::VideoOption::variable_frame_rate)] = 0;
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

    const bool rebuild_protected_launch = session_config_snapshot.has_value();
    const auto desired = *video_pending;
    const auto staged_base = *video_saved;
    if (rebuild_protected_launch) {
        if (!restore_protected_session_config(
                L"Explicit graphics settings changed before KF2 start")) {
            return Result<config::ApplyResult>::failure({
                ErrorCode::io_failure,
                L"The prepared KF2 session could not be restored before applying graphics settings",
                0});
        }
        const auto original = game::read_video_settings(
            installation->config_root);
        if (!original.has_value()) {
            static_cast<void>(prepare_automatic_external_launch_profile());
            return Result<config::ApplyResult>::failure(original.error());
        }
        const auto rebased = game::rebase_video_changes(
            original.value(), staged_base, desired);
        if (!rebased.has_value()) {
            static_cast<void>(prepare_automatic_external_launch_profile());
            return Result<config::ApplyResult>::failure(rebased.error());
        }
        video_saved = original.value();
        video_pending = rebased.value();
    }
    auto prepared = game::build_video_preview(
        installation->config_root, *video_pending, &*video_saved);
    if (!prepared.has_value()) {
        if (rebuild_protected_launch) {
            static_cast<void>(prepare_automatic_external_launch_profile());
        }
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
        if (rebuild_protected_launch) {
            const auto rebuilt = prepare_automatic_external_launch_profile();
            if (!rebuilt.has_value()) {
                events->append({0, diagnostics::Severity::error,
                    "GRAPHICS_PROTECTED_LAUNCH_REBUILD_FAILED",
                    L"The graphics settings were saved, but protected launch preparation failed: " +
                        rebuilt.error().message,
                    L"graphics"});
                model.set_notice({ui::NoticeSeverity::warning,
                    L"GRAPHICS_PROTECTED_LAUNCH_REBUILD_FAILED",
                    L"The graphics settings were saved, but the next protected KF2 launch could not be prepared: " +
                        rebuilt.error().message,
                    L"Run Repair before starting KF2."});
                invalidate();
            } else {
                events->append({0, diagnostics::Severity::info,
                    "GRAPHICS_PROTECTED_LAUNCH_REBUILT",
                    L"The protected KF2 launch was rebuilt from the newly saved graphics and FleX settings",
                    L"graphics"});
            }
        }
    } else if (rebuild_protected_launch) {
        reload_video_settings();
        const auto restored = prepare_automatic_external_launch_profile();
        if (!restored.has_value()) {
            model.set_recovery_required(true);
            events->append({0, diagnostics::Severity::error,
                "GRAPHICS_PROTECTED_LAUNCH_ROLLBACK_FAILED",
                restored.error().message, L"graphics"});
        }
    }
    return result;
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
