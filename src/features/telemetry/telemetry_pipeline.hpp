#pragma once

namespace kf2::telemetry_pipeline {

enum class PipelineStep {
    attach_and_revalidate,
    refresh_session_gate,
    observe_flex,
    inspect_window,
    drain_present,
    capture_frame,
    control_flex,
    evaluate_adaptive,
    derive_presentation,
    publish,
};

enum class PipelineOutcome {
    completed,
    session_not_ready,
    present_not_ready,
    frame_not_ready,
};

template <typename Pipeline>
[[nodiscard]] PipelineOutcome run_ordered_telemetry_pipeline(
    Pipeline& pipeline) {
    pipeline.attach_and_revalidate();
    pipeline.refresh_session_gate();
    // FleX observation intentionally precedes all window/Present early exits.
    pipeline.observe_flex();
    if (!pipeline.inspect_window()) {
        return PipelineOutcome::session_not_ready;
    }
    if (!pipeline.drain_present()) {
        return PipelineOutcome::present_not_ready;
    }
    if (!pipeline.capture_frame()) {
        return PipelineOutcome::frame_not_ready;
    }
    pipeline.control_flex();
    pipeline.evaluate_adaptive();
    pipeline.derive_presentation();
    pipeline.publish();
    return PipelineOutcome::completed;
}

}  // namespace kf2::telemetry_pipeline
