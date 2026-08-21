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

}  // namespace

std::uint64_t UiRuntime::monotonic_ns() const {
    LARGE_INTEGER counter{}, frequency{};
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) return 0;
    return static_cast<std::uint64_t>(
        (static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L) /
        frequency.QuadPart);
}



void UiRuntime::runtime_tick() {
    const auto now = monotonic_ns();
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
                    L"Windows resumed; process, window, PresentMon, PDH and FleX observation bindings will be verified again",
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
