#include "application_runtime.hpp"

#include <array>

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

}  // namespace

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

}  // namespace kf2::app
