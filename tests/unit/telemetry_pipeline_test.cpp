#include <cstdlib>
#include <iostream>
#include <vector>

#include "features/telemetry/telemetry_pipeline.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

using kf2::telemetry_pipeline::PipelineStep;

struct RecordingPipeline final {
    bool session_ready{true};
    bool present_ready{true};
    bool frame_ready{true};
    std::vector<PipelineStep> steps;

    void attach_and_revalidate() {
        steps.push_back(PipelineStep::attach_and_revalidate);
    }
    void refresh_session_gate() {
        steps.push_back(PipelineStep::refresh_session_gate);
    }
    void observe_flex() { steps.push_back(PipelineStep::observe_flex); }
    bool inspect_window() {
        steps.push_back(PipelineStep::inspect_window);
        return session_ready;
    }
    bool drain_present() {
        steps.push_back(PipelineStep::drain_present);
        return present_ready;
    }
    bool capture_frame() {
        steps.push_back(PipelineStep::capture_frame);
        return frame_ready;
    }
    void control_flex() { steps.push_back(PipelineStep::control_flex); }
    void evaluate_adaptive() {
        steps.push_back(PipelineStep::evaluate_adaptive);
    }
    void derive_presentation() {
        steps.push_back(PipelineStep::derive_presentation);
    }
    void publish() { steps.push_back(PipelineStep::publish); }
};

bool equals(const std::vector<PipelineStep>& actual,
            std::initializer_list<PipelineStep> expected) {
    return actual == std::vector<PipelineStep>{expected};
}

}  // namespace

int main() {
    using namespace kf2::telemetry_pipeline;
    RecordingPipeline complete;
    CHECK(run_ordered_telemetry_pipeline(complete) ==
          PipelineOutcome::completed);
    CHECK(equals(complete.steps,
                 {PipelineStep::attach_and_revalidate,
                  PipelineStep::refresh_session_gate,
                  PipelineStep::observe_flex,
                  PipelineStep::inspect_window,
                  PipelineStep::drain_present,
                  PipelineStep::capture_frame,
                  PipelineStep::control_flex,
                  PipelineStep::evaluate_adaptive,
                  PipelineStep::derive_presentation,
                  PipelineStep::publish}));

    RecordingPipeline waiting_window;
    waiting_window.session_ready = false;
    CHECK(run_ordered_telemetry_pipeline(waiting_window) ==
          PipelineOutcome::session_not_ready);
    CHECK(equals(waiting_window.steps,
                 {PipelineStep::attach_and_revalidate,
                  PipelineStep::refresh_session_gate,
                  PipelineStep::observe_flex,
                  PipelineStep::inspect_window}));
    CHECK(waiting_window.steps[2] == PipelineStep::observe_flex);

    RecordingPipeline reconnecting;
    reconnecting.present_ready = false;
    CHECK(run_ordered_telemetry_pipeline(reconnecting) ==
          PipelineOutcome::present_not_ready);
    CHECK(equals(reconnecting.steps,
                 {PipelineStep::attach_and_revalidate,
                  PipelineStep::refresh_session_gate,
                  PipelineStep::observe_flex,
                  PipelineStep::inspect_window,
                  PipelineStep::drain_present}));

    RecordingPipeline rejected;
    rejected.frame_ready = false;
    CHECK(run_ordered_telemetry_pipeline(rejected) ==
          PipelineOutcome::frame_not_ready);
    CHECK(equals(rejected.steps,
                 {PipelineStep::attach_and_revalidate,
                  PipelineStep::refresh_session_gate,
                  PipelineStep::observe_flex,
                  PipelineStep::inspect_window,
                  PipelineStep::drain_present,
                  PipelineStep::capture_frame}));
    return EXIT_SUCCESS;
}
