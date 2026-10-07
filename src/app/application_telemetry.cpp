#include "application_runtime.hpp"
#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "features/telemetry/telemetry_collection_stage.hpp"
#include "features/telemetry/telemetry_flex_stage.hpp"
#include "features/telemetry/telemetry_pipeline.hpp"
#include "features/telemetry/telemetry_presentation_stage.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"

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
        telemetry_tick();
        return;
    }
    if (overlay_window && overlay_presentation) {
        static_cast<void>(overlay_window->update(*overlay_presentation));
    }
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
    RuntimeTelemetryPipeline pipeline{*this};
    // Advance user FPS receipts even with Adaptive off or no DXGI samples.
    // In the normal idle case this is only an empty optional check.
    poll_live_frame_rate();
    static_cast<void>(
        telemetry_pipeline::run_ordered_telemetry_pipeline(pipeline));
}

}  // namespace kf2::app
