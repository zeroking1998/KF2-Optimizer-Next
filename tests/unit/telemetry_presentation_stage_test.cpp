#include <cstdlib>
#include <iostream>
#include <string>

#include "features/telemetry/telemetry_presentation_stage.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

kf2::telemetry_pipeline::TelemetryFrame complete_frame() {
    using namespace kf2;
    telemetry_pipeline::TelemetryFrame frame;
    frame.identity = {42, 84};
    frame.observed_at_ns = 9'000'000'000ULL;
    frame.window.process = {42, 84, L"C:\\KF2\\KFGame.exe"};
    frame.frames.fps = 61.5;
    frame.frames.frame_time_ms = 16.3;
    frame.frames.p95_ms = 18.5;
    frame.frames.p99_ms = 22.0;
    frame.frames.stutter_count = 3;
    frame.frames.loss_count = 2;
    frame.frames.quality = telemetry::SampleQuality::degraded;

    telemetry::ProcessMetrics process;
    process.cpu_percent = 24.0;
    process.critical_core_percent = 82.0;
    process.effective_core_usage = 3.25;
    process.active_cpu_threads = 7;
    process.affinity_logical_processors = 16;
    process.affinity_physical_cores = 8;
    process.working_set_bytes = 4ULL << 30U;
    frame.process = process;

    telemetry::GpuMetrics gpu;
    gpu.dedicated_bytes = 6ULL << 30U;
    frame.adapter_gpu = gpu;
    frame.evidence.cpu_percent = 24.0;
    frame.evidence.gpu_percent = 36.0;

    telemetry::SystemMemoryMetrics memory;
    memory.used_percent = 75.0;
    frame.system_memory = memory;

    game::GameLogSession gameplay;
    gameplay.telemetry_corpse_awake = 9;
    gameplay.telemetry_corpse_sleeping = 14;
    frame.gameplay = gameplay;

    flex::ObservationSnapshot flex;
    flex.last_substeps = 4;
    flex.pass_through_healthy = true;
    frame.flex = flex;
    return frame;
}

bool contains(const std::wstring& text, std::wstring_view needle) {
    return text.find(needle) != std::wstring::npos;
}

}  // namespace

int main() {
    using namespace kf2;
    const auto frame = complete_frame();
    const auto projection = telemetry_pipeline::build_status_projection(
        frame, L"", L"GPU limited", L"balanced", L"Stable evidence");
    CHECK(contains(projection.telemetry, L"61.5 FPS, 16.3 ms"));
    CHECK(contains(projection.telemetry, L"CPU 24.0%"));
    CHECK(contains(projection.telemetry, L"critical thread 82.0%"));
    CHECK(contains(projection.telemetry, L"3.25 cores"));
    CHECK(contains(projection.telemetry, L"7 active threads"));
    CHECK(contains(projection.telemetry, L"affinity 8C/16T"));
    CHECK(contains(projection.telemetry, L"RAM 4.0 GiB"));
    CHECK(contains(projection.telemetry, L"VRAM 6.0 GiB"));
    CHECK(contains(projection.telemetry, L"GPU total 36.00%"));
    CHECK(contains(projection.telemetry, L"system RAM 75%"));
    CHECK(contains(projection.telemetry, L"FleX 4 steps"));
    CHECK(contains(projection.performance_analysis, L"Measurement degraded"));
    CHECK(contains(projection.performance_analysis, L"p95 18.5 ms"));
    CHECK(contains(projection.performance_analysis, L"p99 22.0 ms"));
    CHECK(contains(projection.performance_analysis, L"stutters 3"));
    CHECK(contains(projection.performance_analysis, L"lost events 2"));
    CHECK(contains(projection.performance_analysis, L"analysis: GPU limited"));
    CHECK(contains(projection.performance_analysis,
                   L"Adaptive: balanced (Stable evidence)"));
    CHECK(projection.live_fps == frame.frames.fps);
    CHECK(projection.live_frame_time_ms == frame.frames.frame_time_ms);
    CHECK(projection.live_cpu_percent == frame.evidence.cpu_percent);
    CHECK(projection.live_gpu_percent == frame.evidence.gpu_percent);
    CHECK(projection.live_active_corpses == 9);
    CHECK(projection.live_sleeping_corpses == 14);

    telemetry_pipeline::TelemetryFrame missing;
    const auto missing_projection =
        telemetry_pipeline::build_status_projection(
            missing, L"Present source unavailable", L"ignored",
            L"waiting", L"Fresh telemetry required");
    CHECK(missing_projection.telemetry == L"Present source unavailable");
    CHECK(missing_projection.performance_analysis ==
          L"Performance analysis unavailable | Adaptive: waiting "
          L"(Fresh telemetry required)");
    CHECK(!missing_projection.live_fps);
    CHECK(!missing_projection.live_cpu_percent);
    CHECK(!missing_projection.live_active_corpses);
    CHECK(!contains(missing_projection.telemetry, L"0.0"));

    missing.frames.fps = 60.0;
    missing.frames.quality = telemetry::SampleQuality::good;
    const auto partial = telemetry_pipeline::build_status_projection(
        missing, L"", L"Stable", L"quality", L"Holding");
    CHECK(partial.telemetry == L"Waiting for KF2 frame data");
    CHECK(contains(partial.performance_analysis, L"Measurement good"));
    CHECK(!contains(partial.telemetry, L"CPU 0.0%"));
    CHECK(!contains(partial.telemetry, L"GPU total 0.0%"));
    return EXIT_SUCCESS;
}
