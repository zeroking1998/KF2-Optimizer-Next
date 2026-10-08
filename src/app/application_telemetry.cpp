#include "application_runtime.hpp"
#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "features/telemetry/telemetry_collection_stage.hpp"
#include "features/telemetry/telemetry_flex_stage.hpp"
#include "features/telemetry/telemetry_pipeline.hpp"
#include "features/telemetry/telemetry_presentation_stage.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"
#include <cwchar>

namespace kf2::app {
namespace {

constexpr std::uint64_t kMapPrewarmRetryDelayNs = 15'000'000'000ULL;
constexpr std::uint64_t kPrewarmDiagnosticsPublishIntervalNs =
    1'000'000'000ULL;

std::wstring_view prewarm_state_text(
    game::StartupPrewarmState state) noexcept {
    switch (state) {
        case game::StartupPrewarmState::idle: return L"idle";
        case game::StartupPrewarmState::waiting: return L"waiting";
        case game::StartupPrewarmState::running: return L"running";
        case game::StartupPrewarmState::complete: return L"complete";
        case game::StartupPrewarmState::cancelled: return L"cancelled";
        case game::StartupPrewarmState::skipped_unknown_storage:
            return L"unknown storage";
        case game::StartupPrewarmState::skipped_low_memory:
            return L"low memory";
        case game::StartupPrewarmState::skipped_no_files:
            return L"no files";
        case game::StartupPrewarmState::failed: return L"failed";
        case game::StartupPrewarmState::incomplete: return L"incomplete";
    }
    return L"unknown";
}

std::wstring_view storage_kind_text(game::StorageKind storage) noexcept {
    switch (storage) {
        case game::StorageKind::solid_state: return L"SSD";
        case game::StorageKind::rotational: return L"HDD";
        case game::StorageKind::unknown: return L"unknown storage";
    }
    return L"unknown storage";
}

class RuntimeTelemetryPipeline final {
public:
    explicit RuntimeTelemetryPipeline(UiRuntime& runtime)
        : runtime_{runtime} {}

    void attach_and_revalidate() {
        telemetry_pipeline::attach_session_sources(runtime_);
    }

    void refresh_session_gate() {
        resources_ = telemetry_pipeline::refresh_session_gate(runtime_);
    }

    void observe_flex() {
        telemetry_pipeline::observe_flex_source(runtime_);
    }

    [[nodiscard]] bool inspect_window() {
        // Inspection includes process liveness after FleX observation.
        session_ = telemetry_pipeline::inspect_bound_session(runtime_);
        if (runtime_.self_overhead_collecting) {
            runtime_.self_overhead_game_window = session_->window;
        }
        return session_->disposition ==
            telemetry_pipeline::SessionDisposition::ready;
    }

    [[nodiscard]] bool drain_present() {
        observed_at_ns_ = runtime_.monotonic_ns();
        drain_ = telemetry_pipeline::drain_present_stage(
            runtime_, observed_at_ns_);
        if (drain_->disposition() ==
            telemetry_pipeline::PresentDrainDisposition::reconnecting) {
            return false;
        }
        if (drain_->disposition() !=
                telemetry_pipeline::PresentDrainDisposition::frames_ready ||
            !drain_->frames()) {
            reject_frame(drain_->error());
            return false;
        }
        return true;
    }

    [[nodiscard]] bool capture_frame() {
        auto captured = telemetry_pipeline::capture_telemetry_frame(
            runtime_, *session_->window, observed_at_ns_, *drain_->frames(),
            resources_.get());
        if (!captured.has_value()) {
            reject_frame(captured.error());
            return false;
        }
        frame_ = std::move(captured.value());
        // Read-only snapshots for existing on-demand diagnostics and previews;
        // none may feed a subsequent telemetry tick.
        runtime_.optimizer_evidence = frame_->evidence;
        runtime_.last_frame_metrics = frame_->frames;
        runtime_.last_report_gameplay_session = frame_->gameplay;
        return true;
    }

    void control_flex() {
        telemetry_pipeline::run_flex_control_stage(runtime_, *frame_);
    }

    void evaluate_adaptive() {
        telemetry_pipeline::run_adaptive_stage(runtime_, *frame_);
    }

    void derive_presentation() {
        presentation_ =
            telemetry_pipeline::derive_telemetry_presentation(
                runtime_, *frame_);
    }

    void publish() {
        telemetry_pipeline::publish_telemetry_presentation(
            runtime_, std::move(*presentation_));
    }

private:
    void reject_frame(const std::optional<Error>& error) {
        runtime_.telemetry_failure = error
            ? error->message : L"Telemetry frame unavailable";
        runtime_.events->append(
            {0, diagnostics::Severity::warning,
             "TELEMETRY_FRAME_REJECTED", runtime_.telemetry_failure,
             L"telemetry"});
        runtime_.detach_telemetry();
    }

    void reject_frame(const Error& error) {
        reject_frame(std::optional<Error>{error});
    }

    UiRuntime& runtime_;
    std::uint64_t observed_at_ns_{0};
    std::optional<telemetry_pipeline::SessionStageResult> session_;
    std::optional<telemetry_pipeline::PresentDrainResult> drain_;
    std::optional<telemetry_pipeline::TelemetryFrame> frame_;
    std::shared_ptr<const telemetry::ResourceTelemetrySnapshot> resources_;
    std::optional<telemetry_pipeline::TelemetryPresentation> presentation_;
};

int prewarm_percent(const game::StartupPrewarmSnapshot& snapshot) noexcept {
    if (snapshot.bytes_planned == 0) return 0;
    return static_cast<int>(std::min<std::uint64_t>(
        100, snapshot.bytes_read * 100 / snapshot.bytes_planned));
}

}  // namespace

std::uint64_t UiRuntime::monotonic_ns() const {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) ? value.QuadPart : 0LL;
    }();
    LARGE_INTEGER counter{};
    if (frequency <= 0 || !QueryPerformanceCounter(&counter)) return 0;
    return static_cast<std::uint64_t>(
        (static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L) /
        frequency);
}



void UiRuntime::runtime_tick() {
    poll_startup_prewarm();
    poll_auto_package_repair();
    poll_update_check();
    poll_update_install();
    const auto now = monotonic_ns();
    sample_self_overhead(now);
    poll_map_prewarm();
    if ((model.selected() == ui::Destination::graphics ||
         (session_config_snapshot && session_video_runtime)) &&
        (last_video_config_poll_ns == 0 || now < last_video_config_poll_ns ||
         now - last_video_config_poll_ns >= 1'000'000'000ULL)) {
        last_video_config_poll_ns = now;
        static_cast<void>(synchronize_video_settings_from_game());
    }
    constexpr std::uint64_t kTelemetryIntervalNs = 120'000'000ULL;
    if (last_telemetry_tick_ns == 0 || now < last_telemetry_tick_ns ||
        now - last_telemetry_tick_ns >= kTelemetryIntervalNs) {
        last_telemetry_tick_ns = now;
        const auto work_started = self_overhead_collecting ? monotonic_ns() : 0;
        const auto adaptive_before = self_adaptive_work_ns;
        const auto overlay_before = self_overhead_collecting && overlay_window
            ? overlay_window->diagnostics().total_render_us : 0;
        telemetry_tick();
        if (self_overhead_collecting) {
            const auto work_finished = monotonic_ns();
            const auto overlay_after = overlay_window
                ? overlay_window->diagnostics().total_render_us : 0;
            const auto nested = self_adaptive_work_ns - adaptive_before +
                (overlay_after >= overlay_before
                    ? (overlay_after - overlay_before) * 1000ULL : 0);
            if (work_finished >= work_started &&
                work_finished - work_started >= nested) {
                self_telemetry_work_ns += work_finished - work_started - nested;
            }
        }
        return;
    }
    if (overlay_window && overlay_presentation) {
        static_cast<void>(overlay_window->update(*overlay_presentation));
    }
}

void UiRuntime::sample_self_overhead(std::uint64_t now_ns) {
    const bool visible = model.status().self_overhead_enabled;
    if (!visible && !self_overhead_collecting) return;
    if (!visible) {
        self_overhead_collecting = false;
        resource_telemetry_worker.set_own_gpu_enabled(false);
        self_overhead_gpu_memory.reset();
        self_overhead_sample_ns = 0;
        self_overhead_game_window.reset();
        self_script_work_ms_per_second.reset();
        if (game_process) static_cast<void>(flex_observation_reader.read(*game_process));
        static_cast<void>(self_overhead_dispatcher.poll());
        if (overlay_window) overlay_window->set_diagnostics_enabled(
            optimizer_settings.debug_runtime_diagnostics);
        if (self_overhead_window) self_overhead_window->set_diagnostics_enabled(false);
        return;
    }
    if (!self_overhead_collecting) resource_telemetry_worker.set_own_gpu_enabled(true);
    self_overhead_collecting = true;
    if (self_overhead_sample_ns && now_ns >= self_overhead_sample_ns &&
        now_ns - self_overhead_sample_ns < 1'000'000'000ULL) return;
    if (overlay_window) overlay_window->set_diagnostics_enabled(true);
    const auto elapsed = self_overhead_sample_ns && now_ns > self_overhead_sample_ns
        ? now_ns - self_overhead_sample_ns : 0;
    if (self_overhead_window) self_overhead_window->set_diagnostics_enabled(true);
    const auto process_start = game_process ? game_process->process_start_id : 0;
    const auto* provider = last_report_gameplay_session &&
            last_report_gameplay_session->telemetry_control_port
        ? &*last_report_gameplay_session : game_log_session
            ? &*game_log_session : nullptr;
    const auto port = provider ? provider->telemetry_control_port : std::nullopt;
    if (port != self_overhead_pending_port) {
        self_script_work_ms_per_second.reset();
        self_overhead_reply_ns = 0;
    }
    if (process_start != self_overhead_process_start_id || elapsed == 0) {
        self_script_work_ms_per_second.reset();
        self_overhead_reply_ns = 0;
        static_cast<void>(self_overhead_dispatcher.poll());
        self_overhead_process_start_id = process_start;
        // A new worker clock must establish a baseline even if its cumulative
        // CPU time happens to exceed that of the previous DXGI session.
        self_overhead_clocks = {};
    } else if (auto reply = self_overhead_dispatcher.poll()) {
        if (reply->has_value() &&
            self_overhead_pending_process_start_id == process_start &&
            self_overhead_pending_port == port) {
            const auto& receipt = reply->value();
            self_script_work_ms_per_second = diagnostics::work_ms_per_second(
                0, receipt.script_work_ns, receipt.script_elapsed_ns.value_or(0));
            self_overhead_reply_ns = now_ns;
        } else {
            self_script_work_ms_per_second.reset();
        }
    }
    if (self_overhead_reply_ns == 0 || now_ns < self_overhead_reply_ns ||
        now_ns - self_overhead_reply_ns > 2'000'000'000ULL) {
        self_script_work_ms_per_second.reset();
    }
    if (game_process && port &&
        game::valid_adaptive_control_token(adaptive_control_token) &&
        !self_overhead_dispatcher.busy() && self_overhead_sequence < 2'147'483'647) {
        self_overhead_pending_process_start_id = process_start;
        self_overhead_pending_port = port;
        static_cast<void>(self_overhead_dispatcher.start({
            .port = *port,
            .token = adaptive_control_token, .sequence = ++self_overhead_sequence,
            .resource = game::AdaptiveResourceControl::overhead,
            .quality = 100, .timeout_ms = 250}));
    }
    const auto current = telemetry::query_own_process_counters();
    const auto worker = resource_telemetry_worker.cpu_work_ns();
    const auto overlay_work = ((overlay_window
        ? overlay_window->diagnostics().total_render_us : 0) +
        (self_overhead_window
            ? self_overhead_window->diagnostics().total_render_us : 0)) * 1000ULL;
    if (self_overhead_dxgi_started_ns != present_session_started_ns) {
        self_overhead_clocks[2].reset();
        self_overhead_dxgi_started_ns = present_session_started_ns;
    }
    const auto flex = game_process
        ? flex_observation_reader.read(*game_process, true) : std::nullopt;
    const auto trace_cpu = present_session ? present_session->cpu_work_ns() : std::nullopt;
    const auto drain_cpu = present_source ? present_source->cpu_work_ns() : std::nullopt;
    const std::array<std::optional<std::uint64_t>, 6> clocks{
        worker, self_telemetry_work_ns,
        trace_cpu && drain_cpu ? std::optional{*trace_cpu + *drain_cpu} : std::nullopt,
        self_adaptive_work_ns, overlay_work, flex ? flex->own_work_ns : std::nullopt};
    auto& presentation = self_overhead_readings;
    presentation.values.fill(L"—");
    wchar_t buffer[64]{};
    const auto format = [&](std::size_t index, std::optional<double> value,
                            const wchar_t* pattern) {
        if (!value) return;
        swprintf_s(buffer, pattern, *value);
        presentation.values[index].assign(buffer);
    };
    const auto cpu_work = diagnostics::work_ms_per_second(
        self_overhead_previous.cpu_ns, current.cpu_ns, elapsed);
    const auto processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    format(0, cpu_work && processors
        ? std::optional{*cpu_work / (10.0 * processors)} : std::nullopt, L"%.1f %%");
    format(1, current.ram_bytes
        ? std::optional{static_cast<double>(*current.ram_bytes) / 1'000'000.0}
        : std::nullopt, L"%.0f MB");
    const auto io = diagnostics::work_ms_per_second(
        self_overhead_previous.io_bytes, current.io_bytes, elapsed);
    // Convert the byte rate from MB/s to decimal KB/s.
    format(2, io ? std::optional{*io * 1000.0} : std::nullopt, L"%.1f KB/s");
    if (current.threads) presentation.values[3] = std::to_wstring(*current.threads);
    for (std::size_t index = 0; index < 5; ++index) {
        format(index + 4, diagnostics::work_ms_per_second(
            self_overhead_clocks[index], clocks[index], elapsed), L"%.2f ms/s");
    }
    // The script clock resolves only whole milliseconds. Do not suggest
    // hundredth-millisecond precision or replace an absent producer with zero.
    format(9, self_script_work_ms_per_second, L"%.0f ms/s");
    const auto native = diagnostics::work_ms_per_second(
        self_overhead_clocks[5], clocks[5], elapsed);
    format(10, native, L"%.2f ms/s");
    // Windows measures all app threads, including unlisted work. Wall-clock
    // scopes in KF2 cannot produce an exact cross-process CPU total.
    format(11, cpu_work, L"%.2f ms/s");
    const auto gpu = resource_telemetry_worker.latest();
    if (game_process && gpu && gpu->identity.pid == game_process->pid &&
        gpu->identity.process_start_id == game_process->process_start_id &&
        gpu->gpu_sampled_at_ns && now_ns >= gpu->gpu_sampled_at_ns &&
        now_ns - gpu->gpu_sampled_at_ns <= 2'000'000'000ULL) {
        format(12, gpu->own_gpu_percent, L"%.2f %%");
    }
    const auto gpu_memory = self_overhead_gpu_memory.sample();
    const auto megabytes = [](std::optional<std::uint64_t> bytes) -> std::optional<double> {
        return bytes ? std::optional{static_cast<double>(*bytes) / 1'000'000.0}
                     : std::nullopt;
    };
    format(13, megabytes(gpu_memory.local_bytes), L"%.1f MB");
    format(14, megabytes(gpu_memory.nonlocal_bytes), L"%.1f MB");
    format(15, megabytes(current.private_bytes), L"%.1f MB");
    if (!game_process) {
        presentation.values[9] = L"Inactive";
        presentation.values[10] = L"Inactive";
    }
    self_overhead_previous = current;
    self_overhead_clocks = clocks;
    self_overhead_sample_ns = now_ns;
    if (presentation != model.status().self_overhead) {
        model.set_self_overhead(presentation);
        if (model.selected() == ui::Destination::debug) invalidate();
    }
    self_overhead_overlay.self_overhead = model.status().self_overhead;
    update_self_overhead_overlay();
}

void UiRuntime::update_self_overhead_overlay() {
    if (!self_overhead_window) return;
    if (!model.status().self_overhead_enabled && !self_overhead_overlay.visible) return;
    self_overhead_overlay.visible = false;
    self_overhead_overlay.target_window = nullptr;
    self_overhead_overlay.animations_enabled = false;
    self_overhead_overlay.show_fps = false;
    self_overhead_overlay.show_frame_time = false;
    self_overhead_overlay.show_cpu = false;
    self_overhead_overlay.show_gpu = false;
    self_overhead_overlay.show_memory = false;
    if (model.status().self_overhead_enabled && game_process &&
        self_overhead_game_window &&
        self_overhead_game_window->process.process_start_id == game_process->process_start_id) {
        const auto& game = *self_overhead_game_window;
        self_overhead_overlay.target_window = game.window;
        if (game.visible && !game.minimized && !game.cloaked && game.foreground) {
            // Fixed Debug panel, constrained to KF2's client.
            const auto& bounds = game.client_bounds;
            const LONG width = std::min<LONG>(300, bounds.right - bounds.left - 20);
            const LONG height = std::min<LONG>(400, bounds.bottom - bounds.top - 20);
            if (width >= 220 && height >= 360) {
                self_overhead_overlay.bounds = {bounds.left + 10, bounds.top + 10,
                    bounds.left + 10 + width, bounds.top + 10 + height};
                self_overhead_overlay.visible = true;
            }
        }
    }
    static_cast<void>(self_overhead_window->update(self_overhead_overlay));
}

void UiRuntime::start_startup_prewarm() {
    if (!installation || game::game_process_may_be_running(
            installation->executable)) {
        return;
    }
    startup_prewarm_announced = false;
    prewarm_diagnostics_last_published_ns = 0;
    game::StartupPrewarmOptions options;
    options.collect_diagnostics =
        optimizer_settings.debug_runtime_diagnostics;
    startup_prewarmer.start(installation->install_root, std::move(options));
}

void UiRuntime::publish_prewarm_diagnostics(
    const game::StartupPrewarmSnapshot& snapshot,
    std::wstring_view context, bool force) {
    if (!optimizer_settings.debug_runtime_diagnostics) return;
    const auto now_ns = monotonic_ns();
    if (!force && prewarm_diagnostics_last_published_ns != 0 &&
        now_ns >= prewarm_diagnostics_last_published_ns &&
        now_ns - prewarm_diagnostics_last_published_ns <
            kPrewarmDiagnosticsPublishIntervalNs) {
        return;
    }
    prewarm_diagnostics_last_published_ns = now_ns;
    std::wstring summary{context};
    summary += L": ";
    summary += prewarm_state_text(snapshot.state);
    if (snapshot.diagnostics) {
        summary += L", ";
        summary += storage_kind_text(snapshot.diagnostics->storage);
        summary += L", cache " +
            std::to_wstring(snapshot.bytes_read / (1024ULL * 1024ULL)) +
            L"/" +
            std::to_wstring(snapshot.bytes_planned / (1024ULL * 1024ULL)) +
            L" MiB, files " + std::to_wstring(snapshot.files_read) +
            L"/" +
            std::to_wstring(snapshot.diagnostics->files_planned) +
            L" read, " +
            std::to_wstring(snapshot.diagnostics->files_attempted) +
            L" attempted, open/read failures " +
            std::to_wstring(snapshot.diagnostics->file_open_failures) +
            L"/" +
            std::to_wstring(snapshot.diagnostics->file_read_failures);
    } else {
        summary +=
            L", detailed file/cache counters were not enabled when this job "
            L"started";
    }
    if (model.status().prewarm_diagnostics == summary) return;
    model.set_prewarm_diagnostics(std::move(summary));
    invalidate();
}

void UiRuntime::poll_startup_prewarm() {
    const auto current = startup_prewarmer.snapshot();
    if (!game_process && (current.state == game::StartupPrewarmState::waiting ||
                          current.state == game::StartupPrewarmState::running)) {
        const auto& status = model.status();
        const int percent = prewarm_percent(current);
        if (!status.prewarm_active || status.prewarm_percent != percent ||
            !status.prewarm_map.empty()) {
            model.set_prewarm_progress(true, percent, L"");
            invalidate();
        }
        publish_prewarm_diagnostics(current, L"Startup");
    }
    if (startup_prewarm_announced) return;
    switch (current.state) {
        case game::StartupPrewarmState::complete:
        case game::StartupPrewarmState::incomplete:
            startup_prewarm_announced = true;
            publish_prewarm_diagnostics(current, L"Startup", true);
            model.set_prewarm_progress(false, prewarm_percent(current), L"");
            if (current.state == game::StartupPrewarmState::incomplete) {
                events->append({0, diagnostics::Severity::warning,
                    "STARTUP_PREWARM_INCOMPLETE",
                    L"Startup preparation stopped with " +
                        std::to_wstring(current.bytes_read) + L"/" +
                        std::to_wstring(current.bytes_planned) +
                        L" bytes read; KF2 launch remains available",
                    L"performance"});
            } else if (optimizer_settings.debug_runtime_diagnostics &&
                current.diagnostics) {
                events->append({0, diagnostics::Severity::info,
                    "STARTUP_PREWARM_COMPLETED",
                    L"Prepared " + std::to_wstring(current.files_read) +
                        L" known KF2 startup files (" +
                        std::to_wstring(
                            current.bytes_read / (1024ULL * 1024ULL)) +
                        L" MiB) in the Windows file cache",
                    L"performance"});
            }
            break;
        case game::StartupPrewarmState::cancelled:
            startup_prewarm_announced = true;
            publish_prewarm_diagnostics(current, L"Startup", true);
            model.set_prewarm_progress(
                false, model.status().prewarm_percent, L"");
            if (optimizer_settings.debug_runtime_diagnostics &&
                current.diagnostics && current.bytes_read != 0) {
                events->append({0, diagnostics::Severity::info,
                    "STARTUP_PREWARM_CANCELLED",
                    L"Stopped startup preparation before KF2 launch after " +
                        std::to_wstring(
                            current.bytes_read / (1024ULL * 1024ULL)) +
                        L" MiB; no launch wait was introduced",
                    L"performance"});
            }
            break;
        case game::StartupPrewarmState::skipped_unknown_storage:
        case game::StartupPrewarmState::skipped_low_memory:
        case game::StartupPrewarmState::skipped_no_files:
            startup_prewarm_announced = true;
            model.set_prewarm_progress(false, 0, L"");
            invalidate();
            publish_prewarm_diagnostics(current, L"Startup", true);
            break;
        case game::StartupPrewarmState::failed:
            startup_prewarm_announced = true;
            publish_prewarm_diagnostics(current, L"Startup", true);
            model.set_prewarm_progress(false, 0, L"");
            events->append({0, diagnostics::Severity::warning,
                "STARTUP_PREWARM_FAILED",
                L"Startup preparation failed safely; KF2 launch remains "
                L"available and preparation can be retried",
                L"performance"});
            break;
        default:
            break;
    }
}

void UiRuntime::poll_map_prewarm() {
    const bool menu_window = installation && game_process &&
        game_log_session && game_log_session->main_menu;
    const bool offline_rotation_window = installation && game_process &&
        game_log_session && !game_log_session->main_menu &&
        game_log_session->phase == game::GameLogPhase::match_ended &&
        game_log_session->net_mode == "NM_Standalone" &&
        game_log_session->game_class ==
            "KFGameContent.KFGameInfo_Survival";
    if (!menu_window && !offline_rotation_window) {
        if (!map_prewarm_active.empty() || !map_prewarm_pending.empty() ||
            !map_prewarm_last_attempted.empty() ||
            !map_prewarm_observed.empty() ||
            map_prewarm_retry_not_before_ns != 0) {
            stop_map_prewarm_for_load();
        }
        return;
    }

    if (!map_prewarm_observed.empty() &&
        map_prewarm_observed != map_prewarm_last_attempted &&
        map_prewarm_observed != map_prewarm_active &&
        map_prewarm_observed != map_prewarm_pending) {
        map_prewarm_pending = map_prewarm_observed;
        map_prewarm_retry_not_before_ns = 0;
        if (!map_prewarm_active.empty()) map_prewarmer.request_stop();
    }

    const auto current = map_prewarmer.snapshot();
    const bool worker_available =
        current.state == game::StartupPrewarmState::idle;
    const bool terminal =
        current.state == game::StartupPrewarmState::complete ||
        current.state == game::StartupPrewarmState::incomplete ||
        current.state == game::StartupPrewarmState::cancelled ||
        game::startup_prewarm_retryable(current.state);
    if (!map_prewarm_active.empty() && terminal) {
        const auto completed_map = std::exchange(map_prewarm_active, {});
        if (optimizer_settings.debug_runtime_diagnostics) {
            publish_prewarm_diagnostics(
                current, L"Map " + completed_map, true);
        }
        model.set_prewarm_progress(false,
            current.state == game::StartupPrewarmState::complete ||
            current.state == game::StartupPrewarmState::incomplete
                ? prewarm_percent(current) : 0, L"");
        if (current.state == game::StartupPrewarmState::complete) {
            map_prewarm_retry_not_before_ns = 0;
            if (optimizer_settings.debug_runtime_diagnostics &&
                current.diagnostics) {
                events->append({0, diagnostics::Severity::info,
                    "MAP_PREWARM_COMPLETED",
                    L"Prepared " + completed_map + L" (" +
                        std::to_wstring(
                            current.bytes_read / (1024ULL * 1024ULL)) +
                        L" MiB) in the Windows file cache before map loading",
                    L"performance"});
            }
        } else if (current.state == game::StartupPrewarmState::incomplete) {
            map_prewarm_retry_not_before_ns = 0;
            events->append({0, diagnostics::Severity::warning,
                "MAP_PREWARM_INCOMPLETE",
                L"Map preparation for " + completed_map + L" stopped with " +
                    std::to_wstring(current.bytes_read) + L"/" +
                    std::to_wstring(current.bytes_planned) +
                    L" bytes read; map loading remains available",
                L"performance"});
        } else if (game::startup_prewarm_retryable(current.state) &&
                   completed_map == map_prewarm_observed) {
            map_prewarm_retry_not_before_ns =
                monotonic_ns() + kMapPrewarmRetryDelayNs;
        }
        invalidate();
    } else if (!map_prewarm_active.empty()) {
        if (optimizer_settings.debug_runtime_diagnostics) {
            publish_prewarm_diagnostics(
                current, L"Map " + map_prewarm_active);
        }
        const auto& status = model.status();
        const int percent = prewarm_percent(current);
        if (!status.prewarm_active || status.prewarm_percent != percent ||
            status.prewarm_map != map_prewarm_active) {
            model.set_prewarm_progress(true, percent, map_prewarm_active);
            invalidate();
        }
    }

    if (map_prewarm_active.empty() && map_prewarm_pending.empty() &&
        !map_prewarm_observed.empty() &&
        map_prewarm_observed == map_prewarm_last_attempted &&
        map_prewarm_retry_not_before_ns != 0 &&
        monotonic_ns() >= map_prewarm_retry_not_before_ns) {
        map_prewarm_pending = map_prewarm_observed;
        map_prewarm_retry_not_before_ns = 0;
    }

    if ((worker_available || terminal) && map_prewarm_active.empty() &&
        !map_prewarm_pending.empty()) {
        map_prewarm_active = std::exchange(map_prewarm_pending, {});
        map_prewarm_last_attempted = map_prewarm_active;
        game::StartupPrewarmOptions options;
        options.idle_delay = std::chrono::milliseconds{0};
        options.map_name = map_prewarm_active;
        options.include_common_startup_files = false;
        options.collect_diagnostics =
            optimizer_settings.debug_runtime_diagnostics;
        prewarm_diagnostics_last_published_ns = 0;
        map_prewarmer.start(installation->install_root, std::move(options));
        model.set_prewarm_progress(true, 0, map_prewarm_active);
        invalidate();
    }
}

void UiRuntime::observe_map_prewarm_selection(std::wstring map_name) {
    if (map_name.empty() || map_name == map_prewarm_observed) return;
    map_prewarm_observed = std::move(map_name);
    map_prewarm_retry_not_before_ns = 0;
}

void UiRuntime::stop_map_prewarm_for_load() {
    map_prewarmer.request_stop();
    map_prewarm_pending.clear();
    map_prewarm_active.clear();
    map_prewarm_last_attempted.clear();
    map_prewarm_observed.clear();
    map_prewarm_retry_not_before_ns = 0;
    model.set_prewarm_progress(false, 0, L"");
    invalidate();
}

void UiRuntime::system_resume() {
    // A suspend interval invalidates ETW timing, PDH baselines, window
    // handles and freshness clocks even when Windows reuses the PID.
    // Keep the protected INI snapshot, but rebuild every observation
    // source from the verified executable/process identity.
    detach_telemetry();
    last_telemetry_tick_ns = 0;
    telemetry_failure = L"System resumed; reconnecting KF2 telemetry";
    events->append({0, diagnostics::Severity::info,
                    "SYSTEM_RESUME_REBIND",
                    L"Windows resumed; process, window, DXGI, PDH and FleX observation bindings will be verified again",
                    L"lifecycle"});
    telemetry_tick();
    invalidate();
}





void UiRuntime::telemetry_tick() {
    // Observe health only on the relevant page and redraw only on transition.
    // Never append an event about the event writer's own failure/recovery.
    if (model.selected() == ui::Destination::diagnostics) {
        const bool available = events->persistence_ready();
        if (model.status().event_persistence_available != available) {
            auto status = model.status();
            status.event_persistence_available = available;
            model.set_status(std::move(status));
            invalidate();
        }
    }
    if (self_overhead_collecting) self_overhead_game_window.reset();
    RuntimeTelemetryPipeline pipeline{*this};
    static_cast<void>(
        telemetry_pipeline::run_ordered_telemetry_pipeline(pipeline));
    update_self_overhead_overlay();
}

}  // namespace kf2::app
