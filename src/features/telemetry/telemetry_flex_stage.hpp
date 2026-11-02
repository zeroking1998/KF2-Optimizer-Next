#pragma once

#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

void observe_flex_source(app::UiRuntime& runtime);
void run_flex_diagnostics_stage(app::UiRuntime& runtime,
                                const TelemetryFrame& frame);

}  // namespace kf2::telemetry_pipeline
