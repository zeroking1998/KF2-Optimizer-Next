#include "application_runtime.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include "kf2/security/sha256.hpp"

namespace kf2::app {
namespace {

using VideoConfigWriteTimes =
    std::array<std::optional<std::filesystem::file_time_type>, 3>;

constexpr std::uint64_t kAdaptiveFrameRateConfigPollIntervalNs =
    1'000'000'000ULL;

Result<game::VideoSettings> parse_graphics_replay(
    std::string_view bytes, const config::SessionConfigSnapshot& snapshot) {
    const auto separator = bytes.find('\n');
    const auto payload = separator == std::string_view::npos
        ? std::string_view{} : bytes.substr(separator + 1);
    const auto hash = security::sha256_hex(payload);
    int schema{};
    std::uint64_t volume{}, file{};
    game::VideoSettings settings;
    game::Resolution resolution;
    std::istringstream fields{std::string{payload}};
    bool valid = bytes.size() <= 4096 && hash.has_value() &&
        bytes.substr(0, separator) == hash.value() &&
        bool(fields >> schema >> volume >> file) && schema == 1 &&
        volume == snapshot.root_volume && file == snapshot.root_file &&
        (volume != 0 || file != 0);
    for (auto& choice : settings.choices) valid = bool(fields >> choice) && valid;
    valid = bool(fields >> resolution.width >> resolution.height >>
        settings.film_grain_percent >> settings.flex_level) && valid;
    fields >> std::ws;
    settings.resolutions = {resolution};
    valid = valid && fields.eof() && resolution.width >= 640 &&
        resolution.width <= 16384 && resolution.height >= 480 &&
        resolution.height <= 16384 && settings.film_grain_percent >= 0 &&
        settings.film_grain_percent <= 100 && settings.flex_level >= 0 &&
        settings.flex_level <= 2;
    for (std::size_t index = 0; index < game::kVideoOptionCount; ++index) {
        const auto option = static_cast<game::VideoOption>(index);
        valid = valid && settings.choices[index] >=
            (option == game::VideoOption::resolution ? 0 : -1) &&
            settings.choices[index] < game::video_choice_count(option, settings) +
                (option == game::VideoOption::overall_quality ? 1 : 0);
    }
    if (!valid) return Result<game::VideoSettings>::failure({
        ErrorCode::recovery_required,
        L"Retained graphics changes failed schema, hash or root verification", 0});
    return Result<game::VideoSettings>::success(std::move(settings));
}

Result<std::string> serialize_graphics_replay(
    const game::VideoSettings& settings,
    const config::SessionConfigSnapshot& snapshot) {
    const auto selected = settings.choices[static_cast<std::size_t>(
        game::VideoOption::resolution)];
    if (selected < 0 || selected >= static_cast<int>(settings.resolutions.size())) {
        return Result<std::string>::failure({ErrorCode::invalid_argument,
            L"Confirmed graphics resolution is invalid", 0});
    }
    std::string payload = "1 " + std::to_string(snapshot.root_volume) + " " +
        std::to_string(snapshot.root_file);
    for (std::size_t index = 0; index < game::kVideoOptionCount; ++index) {
        payload += " " + std::to_string(index == static_cast<std::size_t>(
            game::VideoOption::resolution) ? 0 : settings.choices[index]);
    }
    const auto resolution = settings.resolutions[static_cast<std::size_t>(selected)];
    payload += " " + std::to_string(resolution.width) + " " +
        std::to_string(resolution.height) + " " +
        std::to_string(settings.film_grain_percent) + " " +
        std::to_string(settings.flex_level) + "\n";
    const auto hash = security::sha256_hex(payload);
    if (!hash.has_value()) return Result<std::string>::failure(hash.error());
    auto bytes = hash.value() + "\n" + payload;
    const auto checked = parse_graphics_replay(bytes, snapshot);
    if (!checked.has_value()) return Result<std::string>::failure(checked.error());
    return Result<std::string>::success(std::move(bytes));
}

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

Result<std::size_t> UiRuntime::restore_session_video_settings() {
    if (!session_config_snapshot) return Result<std::size_t>::success(0);
    const auto& snapshot = *session_config_snapshot;
    const auto journal = snapshot.snapshot_root / L"graphics-replay.txt";
    std::error_code error;
    bool pending = std::filesystem::exists(journal, error);
    if (error) return Result<std::size_t>::failure({ErrorCode::io_failure,
        L"Graphics replay status could not be inspected",
        static_cast<std::uint32_t>(error.value())});
    // Validate the root-bound original snapshot before writing recovery data.
    // Ordinary restoration uses its existing validation without a second scan.
    if (pending || session_video_native_changes) {
        const auto resumed = config::resume_session_config(
            snapshot.config_root, settings_path.parent_path());
        if (!resumed.has_value()) return Result<std::size_t>::failure(resumed.error());
        if (!resumed.value() || resumed.value()->snapshot_root != snapshot.snapshot_root ||
            resumed.value()->root_volume != snapshot.root_volume ||
            resumed.value()->root_file != snapshot.root_file) {
            return Result<std::size_t>::failure({ErrorCode::recovery_required,
                L"Protected session identity changed before graphics recovery", 0});
        }
    }
    if (!pending && session_video_native_changes) {
        const auto bytes = serialize_graphics_replay(*session_video_native_changes, snapshot);
        if (!bytes.has_value()) return Result<std::size_t>::failure(bytes.error());
        const auto written = platform::windows::atomic_replace_utf8(journal, bytes.value());
        if (!written.has_value()) return Result<std::size_t>::failure(written.error());
        const auto verified = platform::windows::read_bounded_verified_file(journal, 4096);
        if (!verified.has_value()) return Result<std::size_t>::failure(verified.error());
        if (verified.value() != bytes.value()) return Result<std::size_t>::failure({
            ErrorCode::stale_data, L"Graphics replay journal readback failed", 0});
        pending = true;
    }
    if (pending) {
        const auto bytes = platform::windows::read_bounded_verified_file(journal, 4096);
        if (!bytes.has_value()) return Result<std::size_t>::failure(bytes.error());
        auto desired = parse_graphics_replay(bytes.value(), snapshot);
        if (!desired.has_value()) return Result<std::size_t>::failure(desired.error());
        session_video_native_changes = std::move(desired.value());
    }
    const auto restored = config::restore_session_config(snapshot, pending);
    if (!restored.has_value()) return restored;
    if (pending) {
#if defined(KF2_APPLICATION_VIDEO_TESTING)
        const auto probe = [this](VideoPreapplyStage stage) {
            if (video_preapply_probe_for_testing) video_preapply_probe_for_testing(stage);
        };
        probe(VideoPreapplyStage::replay_read);
#endif
        const auto original = game::read_video_settings(snapshot.config_root);
        if (!original.has_value()) return Result<std::size_t>::failure(original.error());
#if defined(KF2_APPLICATION_VIDEO_TESTING)
        probe(VideoPreapplyStage::replay_preview);
#endif
        const auto prepared = game::build_video_preview(
            snapshot.config_root, *session_video_native_changes, &original.value());
        if (!prepared.has_value()) return Result<std::size_t>::failure(prepared.error());
        const bool changed = std::any_of(prepared.value().files.begin(),
            prepared.value().files.end(), [](const auto& file) {
                return file.original_bytes != file.proposed_bytes;
            });
        if (changed) {
#if defined(KF2_APPLICATION_VIDEO_TESTING)
            probe(VideoPreapplyStage::replay_apply);
#endif
            const auto applied = config::apply_preview(prepared.value(), backups,
                {.game_running = game_process.has_value()});
            if (!applied.has_value()) return Result<std::size_t>::failure(applied.error());
        }
#if defined(KF2_APPLICATION_VIDEO_TESTING)
        probe(VideoPreapplyStage::replay_verify);
#endif
        for (const auto& file : prepared.value().files) {
            const auto bytes = platform::windows::read_bounded_verified_file(
                snapshot.config_root / file.relative_path, 16U * 1024U * 1024U);
            if (!bytes.has_value()) return Result<std::size_t>::failure(bytes.error());
            if (bytes.value() != file.proposed_bytes) return Result<std::size_t>::failure({
                ErrorCode::stale_data, L"Confirmed graphics replay readback failed", 0});
        }
        const auto completed = config::complete_session_config(snapshot);
        if (!completed.has_value()) return Result<std::size_t>::failure(completed.error());
        events->append({0, diagnostics::Severity::info, "KF2_NATIVE_GRAPHICS_PRESERVED",
            L"Confirmed graphics changes were saved and verified; temporary session values were excluded",
            L"graphics"});
    }
    session_config_snapshot.reset();
    session_video_runtime.reset();
    session_video_native_changes.reset();
    session_config_waiting_for_launch = false;
    session_config_launch_deadline_ns = 0;
    if (pending) reload_video_settings();
    return restored;
}

void UiRuntime::refresh_video_presentation() {
    auto status = model.status();
    status.graphics_available = video_pending.has_value();
    status.graphics_game_running = installation &&
        game::game_process_may_be_running(installation->executable);
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

VideoSyncDisposition UiRuntime::synchronize_video_settings_from_game() {
    if (!installation || !video_saved || !video_pending) {
        return VideoSyncDisposition::hard_failure;
    }
    // Stable INI generations are still required for safe teardown. An INI
    // write alone is not user intent: KF2 can save temporary Adaptive values.
    if (session_config_snapshot && !session_video_runtime) {
        return VideoSyncDisposition::unchanged;
    }
    const auto write_times = read_video_config_write_times(
        installation->config_root);
    if (!write_times) return VideoSyncDisposition::hard_failure;
    if (video_config_write_times &&
        *video_config_write_times == *write_times) {
        return VideoSyncDisposition::unchanged;
    }
    const auto current = game::read_video_settings(installation->config_root);
    if (!current.has_value()) {
        return VideoSyncDisposition::retryable_unstable;
    }
#if defined(KF2_APPLICATION_VIDEO_TESTING)
    if (video_sync_before_verification_for_testing) {
        video_sync_before_verification_for_testing();
    }
#endif
    if (read_video_config_write_times(installation->config_root) != write_times) {
        return VideoSyncDisposition::retryable_unstable;
    }
    if (session_config_snapshot) {
        session_video_runtime = current.value();
        video_config_write_times = *write_times;
        events->append({0, diagnostics::Severity::info,
            "KF2_RUNTIME_GRAPHICS_INI_CHANGED",
            L"KF2's live graphics INI changed; only confirmed graphics-menu edits are retained as user settings",
            L"graphics"});
        return VideoSyncDisposition::synchronized;
    }
    const auto rebased = game::rebase_video_changes(
        current.value(), *video_saved, *video_pending);
    if (!rebased.has_value()) return VideoSyncDisposition::hard_failure;
    video_saved = current.value();
    video_pending = rebased.value();
    video_config_write_times = *write_times;
    refresh_video_presentation();
    invalidate();
    return VideoSyncDisposition::synchronized;
}

void UiRuntime::retain_game_menu_graphics_changes(
    const game::GameMenuGraphicsChanges& changes) {
    if (session_config_snapshot && video_saved) {
        session_video_native_changes = game::apply_game_menu_graphics_changes(
            session_video_native_changes.value_or(*video_saved), changes);
    }
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
    // display those values as the user's saved graphics. A settings restart
    // is not proof that every live INI value was chosen by the user either.
    if (session_config_snapshot) {
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

    if (adaptive_frame_rate_config_last_poll_ns != 0 &&
        now_ns >= adaptive_frame_rate_config_last_poll_ns &&
        now_ns - adaptive_frame_rate_config_last_poll_ns <
            kAdaptiveFrameRateConfigPollIntervalNs) {
        return false;
    }
    adaptive_frame_rate_config_last_poll_ns = now_ns;

    const auto game_config = installation->config_root / L"KFGame.ini";
    std::error_code write_time_error;
#if defined(KF2_APPLICATION_VIDEO_TESTING)
    ++adaptive_frame_rate_config_metadata_checks_for_testing;
#endif
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
    if (game::game_process_may_be_running(installation->executable)) {
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
        if (!game::apply_overall_quality_preset(*video_pending, preset)) {
            reload_video_settings();
            return;
        }
        // Overall quality deliberately does not touch NVIDIA FleX. FleX is a
        // separate explicit user choice and Adaptive never enables it.
    }
    save_video_selection();
}

void UiRuntime::save_video_selection() {
    const auto result = apply_video_settings();
    if (!result.has_value() && result.error().code != ErrorCode::stale_data) {
        if (session_config_snapshot && video_saved) {
            video_pending = video_saved;
            refresh_video_presentation();
        } else {
            reload_video_settings();
        }
    }
    const bool recovery_required = !result.has_value() &&
        (result.error().code == ErrorCode::recovery_required ||
         model.recovery_required());
    model.set_notice({
        result.has_value() ? ui::NoticeSeverity::info
                           : ui::NoticeSeverity::warning,
        result.has_value() ? L"GRAPHICS_SAVED"
            : recovery_required ? L"GRAPHICS_PROTECTED_LAUNCH_RESTORE_FAILED"
                                : L"GRAPHICS_SAVE_FAILED",
        result.has_value()
            ? L"KF2 graphics saved and verified. A restore backup is available."
            : recovery_required ? result.error().message
                : L"Graphics were not changed: " + result.error().message,
        recovery_required ? L"Run Repair before starting KF2." : L""});
    invalidate();
}

void UiRuntime::reset_video_settings() {
    if (start_mode != StartMode::normal) return;
    if (!video_pending) reload_video_settings();
    if (!video_pending) return;
    if (installation && game::game_process_may_be_running(
            installation->executable)) {
        model.set_notice({ui::NoticeSeverity::warning, L"GRAPHICS_GAME_RUNNING",
                          L"Close KF2 before changing its video settings.", L""});
        invalidate();
        return;
    }
    video_pending = game::recommended_video_defaults(*video_pending);
    save_video_selection();
}

Result<config::ApplyResult> UiRuntime::apply_video_settings() {
    if (!installation || !video_pending || !video_saved) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::not_found, L"KF2 video settings are unavailable", 0});
    }
    const auto synchronization = synchronize_video_settings_from_game();
    if (synchronization == VideoSyncDisposition::retryable_unstable) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::stale_data,
            L"KF2 graphics are still being written; retry after the files settle",
            0});
    }
    if (synchronization == VideoSyncDisposition::hard_failure) {
        return Result<config::ApplyResult>::failure({
            ErrorCode::stale_data,
            L"KF2 graphics changed but could not be safely synchronized", 0});
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
    const bool running = game::game_process_may_be_running(
        installation->executable);
    if (running) {
        return Result<config::ApplyResult>::failure(
            {ErrorCode::access_denied, L"Close KF2 before applying video settings", 0});
    }

    const bool rebuild_protected_launch = session_config_snapshot.has_value();
    const auto fail_before_apply = [this, rebuild_protected_launch](
                                      const Error& error) {
        if (rebuild_protected_launch) {
            const auto restored = prepare_automatic_external_launch_profile();
            if (!restored.has_value() || !restored.value()) {
                model.set_recovery_required(true);
                return Result<config::ApplyResult>::failure({
                    ErrorCode::recovery_required,
                    L"Graphics preparation failed: " + error.message +
                        L"; the next protected launch could not be prepared: " +
                        (restored.has_value()
                            ? L"Protected launch capabilities are unavailable"
                            : restored.error().message),
                    error.native_code});
            }
        }
        return Result<config::ApplyResult>::failure(error);
    };
    const auto desired = *video_pending;
    const auto staged_base = *video_saved;
    if (rebuild_protected_launch) {
        if (!restore_protected_session_config(
                L"Explicit graphics settings changed before KF2 start")) {
            return Result<config::ApplyResult>::failure({
                ErrorCode::recovery_required,
                L"The prepared KF2 session could not be restored before applying graphics settings",
                0});
        }
#if defined(KF2_APPLICATION_VIDEO_TESTING)
        if (video_preapply_probe_for_testing) video_preapply_probe_for_testing(
            VideoPreapplyStage::restored_read);
#endif
        const auto original = game::read_video_settings(
            installation->config_root);
        if (!original.has_value()) {
            return fail_before_apply(original.error());
        }
        const auto rebased = game::rebase_video_changes(
            original.value(), staged_base, desired);
        if (!rebased.has_value()) {
            return fail_before_apply(rebased.error());
        }
        video_saved = original.value();
        video_pending = rebased.value();
    }
#if defined(KF2_APPLICATION_VIDEO_TESTING)
    if (video_preapply_probe_for_testing) video_preapply_probe_for_testing(
        VideoPreapplyStage::preview_build);
#endif
    auto prepared = game::build_video_preview(
        installation->config_root, *video_pending, &*video_saved);
    if (!prepared.has_value()) {
        return fail_before_apply(prepared.error());
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
#if defined(KF2_APPLICATION_VIDEO_TESTING)
            if (video_before_protected_rebuild_for_testing)
                video_before_protected_rebuild_for_testing();
#endif
            const auto rebuilt = prepare_automatic_external_launch_profile();
            if (!rebuilt.has_value() || !rebuilt.value()) {
                const auto cause = rebuilt.has_value()
                    ? Error{ErrorCode::recovery_required,
                        L"Protected launch capabilities are unavailable"}
                    : rebuilt.error();
                const auto message =
                    L"The graphics settings were saved, but the next protected KF2 launch could not be prepared: " +
                    cause.message;
                model.set_recovery_required(true);
                events->append({0, diagnostics::Severity::error,
                    "GRAPHICS_PROTECTED_LAUNCH_REBUILD_FAILED",
                    message,
                    L"graphics"});
                model.set_notice({ui::NoticeSeverity::warning,
                    L"GRAPHICS_PROTECTED_LAUNCH_REBUILD_FAILED",
                    message,
                    L"Run Repair before starting KF2."});
                invalidate();
                return Result<config::ApplyResult>::failure({
                    ErrorCode::recovery_required, message, cause.native_code});
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
