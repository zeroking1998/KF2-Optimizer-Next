#include "application_runtime.hpp"
#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "features/telemetry/telemetry_collection_stage.hpp"
#include "features/telemetry/telemetry_flex_stage.hpp"
#include "features/telemetry/telemetry_pipeline.hpp"
#include "features/telemetry/telemetry_presentation_stage.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"

namespace kf2::app {
namespace {

class RuntimeTelemetryPipeline final {
public:
    explicit RuntimeTelemetryPipeline(UiRuntime& runtime)
        : runtime_{runtime} {}

    void attach_and_revalidate() {
        telemetry_pipeline::attach_session_sources(runtime_);
    }

    void refresh_session_gate() {
        telemetry_pipeline::refresh_session_gate(runtime_);
    }

    void observe_flex() {
        telemetry_pipeline::observe_flex_source(runtime_);
    }

    [[nodiscard]] bool inspect_window() {
        // Preserve the process revalidation point after FleX observation.
        telemetry_pipeline::revalidate_bound_process(runtime_);
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
            runtime_, *session_->window, observed_at_ns_, *drain_->frames());
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
    std::optional<telemetry_pipeline::TelemetryPresentation> presentation_;
};

std::optional<std::wstring> selected_local_map(
    const std::filesystem::path& config_root) {
    std::ifstream input(config_root / L"KFEngine.ini", std::ios::binary);
    if (!input) return std::nullopt;
    const std::string bytes{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};
    auto parsed = config::IniDocument::parse(bytes);
    if (!parsed.has_value()) return std::nullopt;
    auto map = parsed.value().find(L"URL", L"ServerLocalMap");
    if (!map || map->empty()) return std::nullopt;
    return *map;
}

std::optional<std::wstring> next_rotated_map(
    const std::filesystem::path& config_root,
    std::string_view current_map) {
    std::wstring wide_current;
    wide_current.reserve(current_map.size());
    for (const unsigned char character : current_map) {
        if (character > 0x7f) return std::nullopt;
        wide_current.push_back(static_cast<wchar_t>(character));
    }
    std::ifstream input(config_root / L"KFGame.ini", std::ios::binary);
    if (!input) return std::nullopt;
    const std::string bytes{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};
    return game::next_map_from_game_config(bytes, wide_current);
}

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
    poll_map_prewarm(now);
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
    if (!installation || game::find_running_game_process(
            installation->executable).has_value()) {
        return;
    }
    startup_prewarm_announced = false;
    game::StartupPrewarmOptions options;
    options.map_name = selected_local_map(installation->config_root)
        .value_or(L"");
    startup_prewarmer.start(installation->install_root, std::move(options));
}

void UiRuntime::poll_startup_prewarm() {
    const auto current = startup_prewarmer.snapshot();
    if (!game_process && (current.state == game::StartupPrewarmState::waiting ||
                          current.state == game::StartupPrewarmState::running)) {
        auto status = model.status();
        const int percent = prewarm_percent(current);
        if (!status.prewarm_active || status.prewarm_percent != percent ||
            !status.prewarm_map.empty()) {
            status.prewarm_active = true;
            status.prewarm_percent = percent;
            status.prewarm_map.clear();
            model.set_status(std::move(status));
            invalidate();
        }
    }
    if (startup_prewarm_announced) return;
    switch (current.state) {
        case game::StartupPrewarmState::complete:
            startup_prewarm_announced = true;
            {
                auto status = model.status();
                status.prewarm_active = false;
                status.prewarm_percent = 100;
                status.prewarm_map.clear();
                model.set_status(std::move(status));
            }
            events->append({0, diagnostics::Severity::info,
                "STARTUP_PREWARM_COMPLETED",
                L"Prepared " + std::to_wstring(current.files_read) +
                    L" known KF2 startup files (" +
                    std::to_wstring(current.bytes_read / (1024ULL * 1024ULL)) +
                    L" MiB) in the Windows file cache",
                L"performance"});
            break;
        case game::StartupPrewarmState::cancelled:
            startup_prewarm_announced = true;
            {
                auto status = model.status();
                status.prewarm_active = false;
                status.prewarm_map.clear();
                model.set_status(std::move(status));
            }
            if (current.bytes_read != 0) {
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
            break;
        default:
            break;
    }
}

void UiRuntime::poll_map_prewarm(std::uint64_t now_ns) {
    const bool menu_window = installation && game_process &&
        game_log_session && game_log_session->main_menu;
    const bool offline_rotation_window = installation && game_process &&
        game_log_session && !game_log_session->main_menu &&
        game_log_session->phase == game::GameLogPhase::match_ended &&
        game_log_session->net_mode == "NM_Standalone" &&
        game_log_session->game_class ==
            "KFGameContent.KFGameInfo_Survival";
    if (!menu_window && !offline_rotation_window) {
        if (!map_prewarm_active.empty() || !map_prewarm_pending.empty()) {
            stop_map_prewarm_for_load();
        }
        return;
    }

    constexpr std::uint64_t kMapConfigPollNs = 500'000'000ULL;
    if (last_map_prewarm_config_poll_ns == 0 ||
        now_ns < last_map_prewarm_config_poll_ns ||
        now_ns - last_map_prewarm_config_poll_ns >= kMapConfigPollNs) {
        last_map_prewarm_config_poll_ns = now_ns;
        const auto selected = menu_window
            ? selected_local_map(installation->config_root)
            : next_rotated_map(
                  installation->config_root, game_log_session->map);
        if (selected && *selected != map_prewarm_last_attempted &&
            *selected != map_prewarm_active &&
            *selected != map_prewarm_pending) {
            map_prewarm_pending = *selected;
            if (!map_prewarm_active.empty()) map_prewarmer.request_stop();
        }
    }

    const auto current = map_prewarmer.snapshot();
    const bool terminal = current.state == game::StartupPrewarmState::idle ||
        current.state == game::StartupPrewarmState::complete ||
        current.state == game::StartupPrewarmState::cancelled ||
        current.state == game::StartupPrewarmState::skipped_unknown_storage ||
        current.state == game::StartupPrewarmState::skipped_low_memory ||
        current.state == game::StartupPrewarmState::skipped_no_files;
    if (!map_prewarm_active.empty() && terminal) {
        const auto completed_map = std::exchange(map_prewarm_active, {});
        auto status = model.status();
        status.prewarm_active = false;
        status.prewarm_map.clear();
        status.prewarm_percent = current.state ==
            game::StartupPrewarmState::complete ? 100 : 0;
        model.set_status(std::move(status));
        if (current.state == game::StartupPrewarmState::complete) {
            events->append({0, diagnostics::Severity::info,
                "MAP_PREWARM_COMPLETED",
                L"Prepared " + completed_map + L" (" +
                    std::to_wstring(current.bytes_read / (1024ULL * 1024ULL)) +
                    L" MiB) in the Windows file cache before map loading",
                L"performance"});
        }
        invalidate();
    } else if (!map_prewarm_active.empty()) {
        auto status = model.status();
        const int percent = prewarm_percent(current);
        if (!status.prewarm_active || status.prewarm_percent != percent ||
            status.prewarm_map != map_prewarm_active) {
            status.prewarm_active = true;
            status.prewarm_percent = percent;
            status.prewarm_map = map_prewarm_active;
            model.set_status(std::move(status));
            invalidate();
        }
    }

    if (terminal && map_prewarm_active.empty() &&
        !map_prewarm_pending.empty()) {
        map_prewarm_active = std::exchange(map_prewarm_pending, {});
        map_prewarm_last_attempted = map_prewarm_active;
        game::StartupPrewarmOptions options;
        options.idle_delay = std::chrono::milliseconds{0};
        options.map_name = map_prewarm_active;
        options.include_common_startup_files = false;
        map_prewarmer.start(installation->install_root, std::move(options));
        auto status = model.status();
        status.prewarm_active = true;
        status.prewarm_percent = 0;
        status.prewarm_map = map_prewarm_active;
        model.set_status(std::move(status));
        invalidate();
    }
}

void UiRuntime::stop_map_prewarm_for_load() {
    map_prewarmer.request_stop();
    map_prewarm_pending.clear();
    map_prewarm_active.clear();
    map_prewarm_last_attempted.clear();
    last_map_prewarm_config_poll_ns = 0;
    auto status = model.status();
    status.prewarm_active = false;
    status.prewarm_percent = 0;
    status.prewarm_map.clear();
    model.set_status(std::move(status));
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
    RuntimeTelemetryPipeline pipeline{*this};
    static_cast<void>(
        telemetry_pipeline::run_ordered_telemetry_pipeline(pipeline));
}

}  // namespace kf2::app
