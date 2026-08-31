#include "features/telemetry/telemetry_session_stage.hpp"

#include "kf2/game/game_log_locator.hpp"

#include "app/application_runtime.hpp"

namespace kf2::telemetry_pipeline {

void attach_session_sources(app::UiRuntime& runtime) {
    runtime.try_attach_telemetry();
}

void refresh_session_gate(app::UiRuntime& runtime) {
    // Continue consuming KF2's own read-only session log after PresentMon
    // attached; map transitions happen long after the startup gate.
    if (runtime.game_process) runtime.update_overlay_scene_gate();
}

void revalidate_bound_process(app::UiRuntime& runtime) {
    if (!runtime.game_process || !runtime.installation) {
        return;
    }
    const auto previous_process = *runtime.game_process;
    const auto running = game::bind_game_process(
        previous_process.pid, runtime.installation->executable);
    const bool same_process = running.has_value() &&
        running.value().pid == previous_process.pid &&
        running.value().process_start_id ==
            previous_process.process_start_id;
    if (!same_process) {
        runtime.begin_game_restart_handoff(previous_process);
    }
}

SessionStageResult inspect_bound_session(app::UiRuntime& runtime) {
    if (!runtime.game_process || !runtime.present_source) {
        const std::wstring detail = runtime.telemetry_failure.empty()
            ? L"Telemetry waiting for KF2" : runtime.telemetry_failure;
        if (runtime.model.status().telemetry != detail ||
            runtime.model.status().live_fps ||
            runtime.model.status().live_frame_time_ms ||
            runtime.model.status().live_cpu_percent ||
            runtime.model.status().live_gpu_percent ||
            runtime.model.status().game_gpu_name ||
            runtime.model.status().live_active_corpses ||
            runtime.model.status().live_sleeping_corpses) {
            auto status = runtime.model.status();
            status.telemetry = detail;
            status.live_fps.reset();
            status.live_frame_time_ms.reset();
            status.live_cpu_percent.reset();
            status.live_gpu_percent.reset();
            status.game_gpu_name.reset();
            status.live_active_corpses.reset();
            status.live_sleeping_corpses.reset();
            status.adaptive_runtime_corpse_limit.reset();
            status.adaptive_corpse_capability = L"UNAVAILABLE";
            status.adaptive_corpse_action_status = L"NONE";
            status.adaptive_flex_requested_substeps.reset();
            status.adaptive_flex_effective_substeps.reset();
            status.adaptive_flex_action_status = L"NONE";
            status.adaptive_flex_capability = L"UNAVAILABLE";
            status.adaptive_particle_capability = L"UNAVAILABLE";
            runtime.model.set_status(std::move(status));
            runtime.invalidate();
        }
        if (runtime.overlay_window) {
            overlay::OverlayPresentation hidden;
            runtime.overlay_presentation = hidden;
            static_cast<void>(runtime.overlay_window->update(hidden));
        }
        const SessionGateInput gate{
            .process_bound = runtime.game_process.has_value(),
            .window_ready = runtime.game_window != nullptr,
            .scene_ready = runtime.overlay_scene_ready,
            .present_source_bound = runtime.present_source != nullptr};
        return {classify_session_gate(gate), std::nullopt};
    }

    auto inspected = game::inspect_game_window(
        *runtime.game_process, runtime.game_window);
    if (inspected.has_value()) {
        const SessionGateInput gate{
            .process_bound = true,
            .window_ready = true,
            .scene_ready = runtime.overlay_scene_ready,
            .present_source_bound = true};
        return {classify_session_gate(gate), inspected.value()};
    }

    const auto previous_process = *runtime.game_process;
    const auto still_running = runtime.installation
        ? game::bind_game_process(previous_process.pid,
              runtime.installation->executable)
        : Result<game::GameProcessIdentity>::failure(
              {ErrorCode::not_found, L"KF2 installation unavailable", 0});
    const bool same_process_running = still_running.has_value() &&
        still_running.value().pid == previous_process.pid &&
        still_running.value().process_start_id ==
            previous_process.process_start_id;
    const auto process_transition = classify_bound_process_transition(
        same_process_running, still_running.has_value());
    if (process_transition == BoundProcessTransition::same_process) {
        runtime.game_window = nullptr;
        runtime.telemetry_failure = L"Waiting for KF2 window";
        if (runtime.overlay_window) {
            overlay::OverlayPresentation hidden;
            runtime.overlay_presentation = hidden;
            static_cast<void>(runtime.overlay_window->update(hidden));
        }
        const SessionGateInput gate{
            .process_bound = true,
            .same_process_running = true,
            .window_ready = false,
            .scene_ready = runtime.overlay_scene_ready,
            .present_source_bound = true};
        return {classify_session_gate(gate), std::nullopt};
    }

    runtime.begin_game_restart_handoff(previous_process);
    const SessionGateInput gate{
        .process_bound = true,
        .same_process_running = false,
        .window_ready = false,
        .scene_ready = false,
        .present_source_bound = false};
    return {classify_session_gate(gate), std::nullopt};
}

}  // namespace kf2::telemetry_pipeline

namespace kf2::app {

bool UiRuntime::restore_live_adaptive_quality(std::wstring_view reason) {
    if (!installation ||
        !game::valid_adaptive_control_token(adaptive_control_token) ||
        !game::find_running_game_process(
             installation->executable).has_value()) {
        return true;
    }
    std::optional<std::uint16_t> port;
    if (last_report_gameplay_session &&
        last_report_gameplay_session->telemetry_control_port) {
        port = last_report_gameplay_session->telemetry_control_port;
    } else if (game_log_session_parser.current() &&
               game_log_session_parser.current()->telemetry_control_port) {
        port = game_log_session_parser.current()->telemetry_control_port;
    }
    if (!port) {
        events->append({0, diagnostics::Severity::warning,
            "ADAPTIVE_RUNTIME_RESTORE_UNAVAILABLE",
            std::wstring{reason} +
                L"; KF2 is running but its authenticated restore endpoint is unavailable",
            L"optimizer"});
        return false;
    }
    const auto next_sequence =
        adaptive_control_sequence == std::numeric_limits<std::uint64_t>::max()
            ? 1 : adaptive_control_sequence + 1;
    const auto restored = game::send_adaptive_control({
        .port = *port,
        .token = adaptive_control_token,
        .sequence = next_sequence,
        .resource = game::AdaptiveResourceControl::recover,
        .quality = 100,
        .timeout_ms = 500});
    adaptive_control_sequence = next_sequence;
    if (!restored.has_value()) {
        events->append({0, diagnostics::Severity::error,
            "ADAPTIVE_RUNTIME_RESTORE_FAILED",
            std::wstring{reason} +
                L"; KF2 did not confirm the authenticated full-quality restore",
            L"optimizer"});
        return false;
    }
    adaptive_control_pending.reset();
    adaptive_resource_quality.reset(100);
    events->append({0, diagnostics::Severity::info,
        "ADAPTIVE_RUNTIME_RESTORED",
        std::wstring{reason} +
            L"; KF2 confirmed the full-quality restore with an exact APPLIED readback",
        L"optimizer"});
    return true;
}

void UiRuntime::detach_telemetry(bool restore_live_quality) {
    if (restore_live_quality) {
        static_cast<void>(restore_live_adaptive_quality(
            L"Adaptive telemetry detached"));
    }
    if (last_flex_observation && last_flex_observation->update_calls > 0) {
        const auto& observed = *last_flex_observation;
        const bool saved = save_flex_report(observed);
        events->append({0,
            saved && observed.pass_through_healthy
                ? diagnostics::Severity::info : diagnostics::Severity::warning,
            saved ? "FLEX_SESSION_SUMMARY" : "FLEX_SESSION_SAVE_FAILED",
            saved
                ? L"FleX session completed: " + std::to_wstring(observed.update_calls) +
                      L" successfully relayed solver updates, " +
                      std::to_wstring(observed.constrained_updates) +
                      L" materially changed; original substeps " +
                      std::to_wstring(observed.min_substeps) + L"-" +
                      std::to_wstring(observed.max_substeps) +
                      L", forwarded substeps " +
                      std::to_wstring(observed.min_forwarded_substeps) + L"-" +
                      std::to_wstring(observed.max_forwarded_substeps) +
                      L"; local report saved"
                : L"The atomic FleX session report could not be saved",
            L"flex"});
    }
    present_session.reset();
    present_session_started_ns = 0;
    present_session_restart_count = 0;
    if (present_source) static_cast<void>(present_source->stop());
    present_source.reset();
    process_metrics.reset();
    gpu_metrics.reset();
    nvidia_gpu_metrics.reset();
    gpu_utilization_filter.reset();
    adaptive_adapter_luid.reset();
    optimizer_evidence = {};
    adaptive_governor.reset();
    adaptive_actuation.disable(monotonic_ns());
    adaptive_actuation.rebase({}, monotonic_ns());
    adaptive_control_pending.reset();
    adaptive_control_sequence = 0;
    adaptive_quality_last_dispatch_ns = 0;
    adaptive_resource_quality.reset(
        optimizer_settings.adaptive_maximum_quality);
    adaptive_session_policy.reset();
    adaptive_profile_gate.reset();
    adaptive_gameplay_active = false;
    adaptive_provider_confirmed = false;
    adaptive_overhead_breaches = 0;
    adaptive_overhead_frozen = false;
    adaptive_decision = {};
    adaptive_map.clear();
    adaptive_map_generation = 0;
    adaptive_telemetry_sample = 0;
    last_adaptive_state = optimizer::AdaptiveControllerState::disabled;
    last_adaptive_disposition = optimizer::AdaptiveDisposition::none;
    last_adaptive_bottleneck = optimizer::AdaptiveBottleneck::unknown;
    last_adaptive_decision_log_ns = 0;
    last_frame_metrics = {};
    last_report_gameplay_session.reset();
    adapter_vram_budget.reset();
    last_flex_observation_calls = 0;
    flex_observation_announced = false;
    last_flex_observation.reset();
    last_flex_report_tick = 0;
    flex_adaptive_policy.reset();
    flex_adaptive_constrained = false;
    game_process.reset();
    game_log_path.clear();
    game_log_offset = 0;
    game_log_volume_serial = 0;
    game_log_file_index = 0;
    game_log_process_start_id = 0;
    game_log_bound_to_process = false;
    game_log_startup_exited = false;
    game_log_startup_exit_announced = false;
    game_log_new_settings_restart_requested = false;
    game_log_marker_tail.clear();
    game_log_session_parser.reset();
    auto status = model.status();
    status.game_session.clear();
    status.flex_telemetry = L"FleX telemetry not observed";
    status.live_fps.reset();
    status.live_frame_time_ms.reset();
    status.live_cpu_percent.reset();
    status.live_gpu_percent.reset();
    status.game_gpu_name.reset();
    status.live_active_corpses.reset();
    status.live_sleeping_corpses.reset();
    status.active_target_fps.reset();
    status.active_corpse_limit.reset();
    const auto launch_profile = optimizer::bound_adaptive_profile(
        stored_adaptive_profile(optimizer_settings),
        optimizer_settings.adaptive_minimum_quality,
        optimizer_settings.adaptive_maximum_quality);
    status.recommended_profile = launch_profile
        ? std::wstring{optimizer::adaptive_profile_label(*launch_profile)}
        : L"not available";
    status.recommendation_reason = launch_profile
        ? L"Saved automatic profile is ready for the next protected launch"
        : L"No verified named profile fits the selected quality limits";
    model.set_status(std::move(status));
    overlay_scene_ready = false;
    game_window = nullptr;
    if (overlay_window) {
        overlay::OverlayPresentation hidden;
        overlay_presentation = hidden;
        static_cast<void>(overlay_window->update(hidden));
    }
}

void UiRuntime::begin_game_restart_handoff(
    const game::GameProcessIdentity& previous_process) {
    if (game_restart_handoff_previous_process) return;
    // Consume the process' final native log lines before clearing the old
    // binding. KF2 writes its settings-restart marker immediately before exit.
    update_overlay_scene_gate();
    const bool new_settings_restart =
        game_log_new_settings_restart_requested;
    detach_telemetry(false);
    game_restart_handoff_previous_process = previous_process;
    game_restart_handoff_new_settings = new_settings_restart;
    const auto now = monotonic_ns();
    const auto timeout_ns =
        telemetry_pipeline::game_restart_handoff_timeout_ns(
            new_settings_restart);
    game_restart_handoff_deadline_ns =
        now > std::numeric_limits<std::uint64_t>::max() -
                  timeout_ns
            ? std::numeric_limits<std::uint64_t>::max()
            : now + timeout_ns;
    telemetry_failure = new_settings_restart
        ? L"KF2 is applying new settings; waiting for its replacement process"
        : L"KF2 process ended; checking briefly for a replacement process";
    events->append({0, diagnostics::Severity::info,
        "KF2_SESSION_RESTART_WAIT",
        new_settings_restart
            ? L"KF2 confirmed a settings restart; protected INIs and the telemetry module are retained for up to five minutes"
            : L"The bound KF2 process ended; checking briefly for a verified replacement process",
        L"game"});
    invalidate();
}

void UiRuntime::finalize_ended_game_session() {
    game_restart_handoff_previous_process.reset();
    game_restart_handoff_deadline_ns = 0;
    game_restart_handoff_new_settings = false;
    bool session_restored = true;
    if (session_config_snapshot) {
        session_restored = restore_protected_session_config(L"KF2 closed");
    } else if (installation) {
        const auto capped = synchronize_frame_rate_cap();
        if (!capped.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "TARGET_FPS_PERSIST_FAILED", capped.error().message,
                L"config"});
        } else if (capped.value().changed) {
            events->append({0, diagnostics::Severity::info,
                "TARGET_FPS_PERSISTED",
                L"KF2 closed; the native startup cap was updated to " +
                    std::to_wstring(capped.value().target_fps) + L" FPS",
                L"config"});
        }
    }
    telemetry_failure = L"KF2 session ended";
    model.set_notice(
        {ui::NoticeSeverity::info, L"KF2_SESSION_ENDED",
         L"KF2 closed; telemetry and protected INIs were finalized.", L""});
    events->append(
        {0, diagnostics::Severity::info, "KF2_SESSION_ENDED",
         L"No verified replacement process appeared; session telemetry was finalized",
         L"game"});
    if (session_restored) {
        static_cast<void>(rearm_automatic_external_launch_profile());
    }
    invalidate();
}

void UiRuntime::update_overlay_scene_gate() {
    if (!installation || !game_process) return;
    const auto selected_log = game::find_active_game_log(
        installation->config_root.parent_path() / L"Logs",
        game_process->process_start_id);
    if (!selected_log.has_value() || !selected_log.value()) return;
    game_log_path = selected_log.value()->path;
    HANDLE log_file = CreateFileW(game_log_path.c_str(),
        FILE_READ_ATTRIBUTES | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool inspected = GetFileInformationByHandle(log_file, &information) &&
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    CloseHandle(log_file);
    if (!inspected) return;
    const auto size =
        (static_cast<std::uintmax_t>(information.nFileSizeHigh) << 32U) |
        information.nFileSizeLow;
    const auto last_write =
        (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32U) |
        information.ftLastWriteTime.dwLowDateTime;
    const auto creation_time =
        (static_cast<std::uint64_t>(information.ftCreationTime.dwHighDateTime) << 32U) |
        information.ftCreationTime.dwLowDateTime;
    if (!game::game_log_belongs_to_process(
            last_write, game_process->process_start_id)) {
        return;
    }
    const auto file_index =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
        information.nFileIndexLow;
    const bool identity_changed = game_log_bound_to_process &&
        (game_log_volume_serial != information.dwVolumeSerialNumber ||
         game_log_file_index != file_index ||
         game_log_process_start_id != game_process->process_start_id);
    if (!game_log_bound_to_process || identity_changed) {
        game_log_volume_serial = information.dwVolumeSerialNumber;
        game_log_file_index = file_index;
        game_log_process_start_id = game_process->process_start_id;
        game_log_bound_to_process = true;
        game_log_offset = 0;
        game_log_marker_tail.clear();
        overlay_scene_ready = false;
        game_log_startup_exited = false;
        game_log_startup_exit_announced = false;
        game_log_new_settings_restart_requested = false;
        game_log_session_parser.reset();
    }
    if (size < game_log_offset) {
        game_log_offset = 0;
        game_log_marker_tail.clear();
        overlay_scene_ready = false;
        game_log_startup_exited = false;
        game_log_startup_exit_announced = false;
        game_log_new_settings_restart_requested = false;
        game_log_session_parser.reset();
    }
    if (size == game_log_offset) {
        if (const auto expired =
                game_log_session_parser.expire_observations(monotonic_ns())) {
            auto status = model.status();
            status.game_session = game::describe_game_log_session(*expired);
            model.set_status(std::move(status));
        }
        return;
    }
    std::ifstream input(game_log_path, std::ios::binary);
    if (!input) return;
    input.seekg(static_cast<std::streamoff>(game_log_offset));
    constexpr std::uintmax_t kMaximumLogChunkBytes = 32 * 1024;
    const auto requested = static_cast<std::size_t>(
        std::min<std::uintmax_t>(size - game_log_offset,
                                kMaximumLogChunkBytes));
    std::string appended(requested, '\0');
    input.read(appended.data(), static_cast<std::streamsize>(requested));
    const auto received = static_cast<std::size_t>(input.gcount());
    if (received == 0) return;
    appended.resize(received);
    game_log_offset += received;

    // This is a one-shot startup gate. Once KF2 reaches its main menu the
    // overlay remains eligible during later map loads and Steam overlays.
    const std::string marker_input = game_log_marker_tail + appended;
    if (!game_log_new_settings_restart_requested &&
        game::game_log_requests_settings_restart(marker_input)) {
        game_log_new_settings_restart_requested = true;
        events->append({0, diagnostics::Severity::info,
            "KF2_NEW_SETTINGS_RESTART_REQUESTED",
            L"KF2's native log confirmed that the game requested a settings restart",
            L"game"});
    }
    const bool menu_ready =
        marker_input.find("WidgetInitialized - WidgetName:  StartMenu") !=
        std::string::npos;
    if (menu_ready) {
        overlay_scene_ready = true;
        game_log_startup_exited = false;
    } else if (!overlay_scene_ready &&
               game::game_log_belongs_to_process(
                   creation_time, game_process->process_start_id) &&
               game::game_log_reports_engine_exit(marker_input)) {
        game_log_startup_exited = true;
        if (!game_log_startup_exit_announced) {
            game_log_startup_exit_announced = true;
            events->append({0, diagnostics::Severity::warning,
                "KF2_STARTUP_EXITED_BEFORE_MENU",
                L"KF2's engine exited before reaching the main menu; "
                L"waiting for the remaining process to close",
                L"telemetry"});
            invalidate();
        }
    }
    constexpr std::size_t kMarkerTailBytes = 96;
    game_log_marker_tail = marker_input.substr(
        marker_input.size() > kMarkerTailBytes
            ? marker_input.size() - kMarkerTailBytes : 0);
    const auto previous_session = game_log_session_parser.current();
    if (const auto session = game_log_session_parser.feed(
            appended, monotonic_ns())) {
        auto status = model.status();
        status.game_session = game::describe_game_log_session(*session);
        model.set_status(std::move(status));
        const bool context_changed = !previous_session ||
            previous_session->map != session->map ||
            previous_session->game_class != session->game_class ||
            previous_session->difficulty != session->difficulty ||
            previous_session->game_length != session->game_length ||
            previous_session->net_mode != session->net_mode ||
            previous_session->phase != session->phase ||
            previous_session->main_menu != session->main_menu;
        // Per-kill counter updates stay visible in the UI but do not flood
        // the bounded diagnostic event log.
        if (context_changed) {
            const char* event_code = session->main_menu
                ? "GAME_SESSION_MENU"
                : session->phase == game::GameLogPhase::match_ended
                    ? "GAME_SESSION_ENDED" : "GAME_SESSION_CONTEXT";
            events->append({0, diagnostics::Severity::info,
                event_code,
                game::describe_game_log_session(*session), L"game"});
        }
        invalidate();
    }
}

void UiRuntime::try_attach_telemetry() {
    if (!installation || present_source) return;
    bool confirmed_settings_restart_replacement = false;
    // Revalidate the executable-bound process on every pre-attach tick.
    // A cached PID without a window can otherwise survive a failed launch
    // forever and prevent the protected INI snapshot from being restored.
    auto process = game::find_running_game_process(installation->executable);
    if (game_process) {
        const auto previous_process = *game_process;
        const auto bound_process = game::bind_game_process(
            previous_process.pid, installation->executable);
        const bool same_process = bound_process.has_value() &&
            bound_process.value().process_start_id ==
                previous_process.process_start_id;
        if (same_process) {
            // Process enumeration can briefly return a replacement process
            // while the old bootstrap process is still shutting down. Keep
            // the established identity until that exact process has ended.
            process = bound_process;
        } else {
            begin_game_restart_handoff(previous_process);
        }
    }
    const auto now = monotonic_ns();
    if (game_restart_handoff_previous_process) {
        const auto& previous = *game_restart_handoff_previous_process;
        const bool same_process = process.has_value() &&
            process.value().pid == previous.pid &&
            process.value().process_start_id == previous.process_start_id;
        const auto handoff = telemetry_pipeline::classify_restart_handoff({
            .pending = true,
            .verified_process_found = process.has_value(),
            .same_process_identity = same_process,
            .deadline_ns = game_restart_handoff_deadline_ns,
            .now_ns = now});
        if (handoff ==
            telemetry_pipeline::RestartHandoffDisposition::waiting) {
            telemetry_failure = game_restart_handoff_new_settings
                ? L"KF2 is applying new settings; waiting for its replacement process"
                : L"KF2 process ended; checking briefly for a replacement process";
            return;
        }
        if (handoff ==
            telemetry_pipeline::RestartHandoffDisposition::expired) {
            finalize_ended_game_session();
            return;
        }
        const bool replacement = handoff ==
            telemetry_pipeline::RestartHandoffDisposition::replacement_found;
        const bool new_settings_restart =
            game_restart_handoff_new_settings;
        confirmed_settings_restart_replacement =
            replacement && new_settings_restart;
        game_restart_handoff_previous_process.reset();
        game_restart_handoff_deadline_ns = 0;
        game_restart_handoff_new_settings = false;
        events->append({0, diagnostics::Severity::info,
            replacement ? "KF2_SESSION_RESTART_HANDOFF"
                        : "KF2_SESSION_PROCESS_REVALIDATED",
            replacement
                ? (new_settings_restart
                    ? L"KF2's new-settings replacement process was bound from the verified game executable without restoring the protected session in between"
                    : L"A replacement KF2 process from the verified game executable was bound without restoring the protected session in between")
                : L"The original KF2 process identity was found again; telemetry will be rebound",
            L"game"});
    }
    if (!process.has_value()) {
        const bool launch_wait_expired = session_config_waiting_for_launch &&
            session_config_launch_deadline_ns != 0 &&
            now >= session_config_launch_deadline_ns;
        if (session_config_snapshot &&
            (!session_config_waiting_for_launch || launch_wait_expired)) {
            static_cast<void>(restore_protected_session_config(
                launch_wait_expired
                    ? L"KF2 did not start before the safety timeout"
                    : L"KF2 closed"));
        }
        telemetry_failure = session_config_waiting_for_launch
            ? (session_config_launch_deadline_ns == 0
                   ? L"Adaptive profile ready; waiting for KF2 process"
                   : L"Waiting for app-started KF2 process")
            : L"Waiting for KF2 process";
        return;
    }
    const bool new_process = !game_process ||
        game_process->pid != process.value().pid ||
        game_process->process_start_id != process.value().process_start_id;
    if (new_process) {
        refresh_game_configuration_for_process_start(
            confirmed_settings_restart_replacement);
        game::OfflineAdaptiveSessionPolicy active_policy{
            optimizer_settings.corpse_limit,
            optimizer_settings.target_fps,
            optimizer_settings.adaptive_quality_change_budget};
        const auto observed = game::read_offline_adaptive_session_policy(
            installation->config_root);
        if (observed.has_value() && observed.value()) {
            active_policy = *observed.value();
        } else if (!observed.has_value()) {
            events->append({0, diagnostics::Severity::warning,
                "ADAPTIVE_SESSION_POLICY_FALLBACK",
                L"The protected provider policy could not be read; Adaptive bound the current saved values for this process: " +
                    observed.error().message,
                L"optimizer"});
        }
        active_policy.target_fps = optimizer_settings.target_fps;
        adaptive_session_policy = active_policy;
        auto status = model.status();
        status.active_target_fps = active_policy.target_fps;
        status.active_corpse_limit = active_policy.corpse_maximum;
        model.set_status(std::move(status));
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_SESSION_POLICY_BOUND",
            L"KF2 process policy bound: target " +
                std::to_wstring(active_policy.target_fps) +
                L" FPS, maximum corpses " +
                std::to_wstring(active_policy.corpse_maximum) +
                L", quality-change budget " +
                std::to_wstring(active_policy.quality_change_budget),
            L"optimizer"});
        invalidate();
    }
    // Bind the process before looking for a window. FleX exposes a
    // process-local channel and must work during splash, fullscreen and
    // other periods in which KF2 has no inspectable top-level window yet.
    game_process = process.value();
    auto found_window = game::find_game_window(process.value());
    if (!found_window.has_value()) {
        telemetry_failure = L"Waiting for visible KF2 window";
        return;
    }
    if (optimizer_settings.restore_config_after_game &&
        !session_config_snapshot && start_mode == StartMode::normal) {
        auto captured = config::capture_session_config(
            installation->config_root, settings_path.parent_path());
        if (captured.has_value()) {
            session_config_snapshot = std::move(captured.value());
            events->append({0, diagnostics::Severity::info,
                "SESSION_CONFIG_CAPTURED",
                L"KF2 INI session snapshot captured before monitoring", L"config"});
        } else {
            events->append({0, diagnostics::Severity::error,
                "SESSION_CONFIG_CAPTURE_FAILED", captured.error().message, L"config"});
        }
    }
    game_window = found_window.value();
    // Steam may briefly expose a KFGame process before the actual game owns a
    // window. Keep the launch guard through that bootstrap process; only a
    // process-bound KF2 window confirms that startup has committed.
    session_config_waiting_for_launch = false;
    session_config_launch_deadline_ns = 0;
    // Starting the ETW consumer during KF2's splash/loading phase can bind
    // before the DX11 presentation path is active and then remain silent
    // for the lifetime of the process.  Wait for KF2's own main-menu
    // marker first.  This also prevents the overlay from briefly appearing
    // and disappearing during startup. The complete current process-bound
    // Launch log is scanned, so starting the optimizer after KF2 reached the
    // menu works as well.
    update_overlay_scene_gate();
    if (!overlay_scene_ready) {
        telemetry_failure = game_log_startup_exited
            ? L"KF2 engine exited before the main menu; waiting for process cleanup"
            : L"Waiting for KF2 main menu";
        return;
    }
    const telemetry::SampleIdentity identity{
        game_process->pid, game_process->process_start_id};
    present_source = std::make_unique<telemetry::PresentSource>(identity, 2400);
    static_cast<void>(present_source->start());
    auto presentmon = platform::windows::PresentMonSession::start(
        identity, *present_source);
    if (presentmon.has_value()) {
        present_session = std::move(presentmon.value());
        present_session_started_ns = monotonic_ns();
        present_session_restart_count = 0;
        telemetry_failure.clear();
    } else {
        telemetry_failure = L"PresentMon unavailable: " +
                            presentmon.error().message;
        events->append({0, diagnostics::Severity::warning,
                        "PRESENTMON_UNAVAILABLE",
                        presentmon.error().message, L"telemetry"});
    }
    process_metrics = std::make_unique<telemetry::ProcessMetricSampler>(*game_process);
    const auto window_luid = telemetry::adapter_luid_for_window(game_window);
    const auto adapters = telemetry::enumerate_gpu_adapters();
    std::optional<telemetry::GpuAdapter> measured_adapter;
    if (adapters.has_value()) {
        const auto physical = telemetry::unique_physical_gpu_adapters(
            adapters.value());
        if (window_luid.has_value()) {
            for (const auto& adapter : physical) {
                if (adapter.luid == window_luid.value()) {
                    measured_adapter = adapter;
                    break;
                }
            }
        }
        // A local virtual-display adapter can own the window while the
        // only physical GPU still performs the rendering. In that
        // unambiguous case, bind the physical device instead of the
        // display projection.
        if (!measured_adapter && physical.size() == 1) {
            measured_adapter = physical.front();
        }
    }
    if (measured_adapter) {
        adaptive_adapter_luid = measured_adapter->luid;
        adapter_vram_budget = measured_adapter->dedicated_memory_bytes;
        auto gpu = telemetry::PdhGpuSampler::create(
            game_process->pid, measured_adapter->luid);
        if (gpu.has_value()) gpu_metrics.emplace(std::move(gpu.value()));
        if (measured_adapter->vendor_id == 0x10DE) {
            auto nvidia = telemetry::NvidiaGpuSampler::create(
                measured_adapter->name);
            if (nvidia.has_value()) {
                const bool afterburner_compatible =
                    nvidia.value().source() ==
                    telemetry::NvidiaGpuSource::nvapi_dynamic_pstates;
                nvidia_gpu_metrics.emplace(std::move(nvidia.value()));
                events->append({0, diagnostics::Severity::info,
                    "NVIDIA_TOTAL_GPU_TELEMETRY_ACTIVE",
                    afterburner_compatible
                        ? L"GPU usage uses the installed NVIDIA driver's dynamic P-state utilization domain, matching MSI Afterburner semantics"
                        : L"GPU usage uses the installed NVIDIA driver's local NVML whole-device fallback",
                    L"telemetry"});
            } else {
                events->append({0, diagnostics::Severity::warning,
                    "NVIDIA_TOTAL_GPU_TELEMETRY_FALLBACK",
                    L"NVIDIA driver utilization is unavailable; adapter-wide Windows GPU telemetry is used",
                    L"telemetry"});
            }
        }
    } else if (window_luid.has_value()) {
        adaptive_adapter_luid = window_luid.value();
        auto gpu = telemetry::PdhGpuSampler::create(
            game_process->pid, window_luid.value());
        if (gpu.has_value()) gpu_metrics.emplace(std::move(gpu.value()));
    }
}

void UiRuntime::bind_process_gpu_adapter(std::uint64_t adapter_luid) {
    if (!game_process || adapter_luid == 0) return;
    const auto adapters = telemetry::enumerate_gpu_adapters();
    if (!adapters.has_value()) return;
    const auto adapter = telemetry::find_hardware_gpu_adapter_by_luid(
        adapters.value(), adapter_luid);
    if (!adapter) return;

    auto status = model.status();
    const bool identity_changed = !status.game_gpu_name ||
        *status.game_gpu_name != adapter->name;
    if (identity_changed) {
        status.game_gpu_name = adapter->name;
        model.set_status(std::move(status));
        events->append({0, diagnostics::Severity::info,
            "GAME_GPU_CONFIRMED",
            L"KF2 process GPU activity confirmed on " + adapter->name,
            L"telemetry"});
        invalidate();
    }

    if (adaptive_adapter_luid && *adaptive_adapter_luid == adapter_luid) return;
    gpu_utilization_filter.reset();
    adaptive_adapter_luid = adapter_luid;
    adapter_vram_budget = adapter->dedicated_memory_bytes;
    if (auto gpu = telemetry::PdhGpuSampler::create(
            game_process->pid, adapter_luid);
        gpu.has_value()) {
        gpu_metrics.emplace(std::move(gpu.value()));
    }
    nvidia_gpu_metrics.reset();
    if (adapter->vendor_id == 0x10DE) {
        if (auto nvidia = telemetry::NvidiaGpuSampler::create(adapter->name);
            nvidia.has_value()) {
            nvidia_gpu_metrics.emplace(std::move(nvidia.value()));
        }
    }
}

}  // namespace kf2::app
