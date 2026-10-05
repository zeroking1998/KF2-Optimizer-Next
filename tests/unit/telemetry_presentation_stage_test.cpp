#include <cstdlib>
#include <iostream>
#include <limits>
#include <locale>
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

struct CommaPunctuation final : std::numpunct<wchar_t> {
    wchar_t do_decimal_point() const override { return L','; }
};

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
    frame.gameplay = game::make_game_log_session_snapshot(
        std::move(gameplay));

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
    using telemetry_pipeline::kOverlayDiagnosticsPublishIntervalNs;
    using telemetry_pipeline::kOverlayPlacementCacheIntervalNs;
    CHECK(!telemetry_pipeline::overlay_placement_cache_is_fresh(
        0, 1, true));
    CHECK(telemetry_pipeline::overlay_placement_cache_is_fresh(
        1'000, 1'000 + kOverlayPlacementCacheIntervalNs - 1, true));
    CHECK(!telemetry_pipeline::overlay_placement_cache_is_fresh(
        1'000, 1'000 + kOverlayPlacementCacheIntervalNs, true));
    CHECK(!telemetry_pipeline::overlay_placement_cache_is_fresh(
        2'000, 1'000, true));
    CHECK(!telemetry_pipeline::overlay_placement_cache_is_fresh(
        1'000, 1'001, false));
    CHECK(telemetry_pipeline::overlay_diagnostics_publish_is_due(0, 1));
    CHECK(!telemetry_pipeline::overlay_diagnostics_publish_is_due(
        1'000, 1'000 + kOverlayDiagnosticsPublishIntervalNs - 1));
    CHECK(telemetry_pipeline::overlay_diagnostics_publish_is_due(
        1'000, 1'000 + kOverlayDiagnosticsPublishIntervalNs));
    CHECK(telemetry_pipeline::overlay_diagnostics_publish_is_due(
        2'000, 1'000));

    const auto frame = complete_frame();
    const auto projection = telemetry_pipeline::build_status_projection(
        frame, L"", L"GPU limited", L"balanced", L"Stable evidence");
    CHECK(projection.telemetry ==
          L"61.5 FPS, 16.3 ms, CPU 24.0% (critical thread 82.0%), 3.25 cores"
          L" / 7 active threads, affinity 8C/16T, RAM 4.0 GiB, VRAM 6.0 GiB,"
          L" GPU total 36.00%, system RAM 75%, FleX 4 steps");
    CHECK(projection.performance_analysis ==
          L"Measurement degraded | p95 18.5 ms | p99 22.0 ms | stutters 3"
          L" | lost events 2 | analysis: GPU limited"
          L" | Adaptive: balanced (Stable evidence)");
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

    const std::wstring embedded_failure{L"source\0unavailable", 18};
    for (const auto& failure : {std::wstring{}, std::wstring{L"Unavailable"},
                               embedded_failure}) {
        for (const bool fps_present : {false, true}) {
            for (const bool time_present : {false, true}) {
                telemetry_pipeline::TelemetryFrame boundary;
                if (fps_present) boundary.frames.fps = 0.0;
                if (time_present) boundary.frames.frame_time_ms = 0.0;
                const auto actual = telemetry_pipeline::build_status_projection(
                    boundary, failure, L"Stable", L"User settings", L"Holding");
                const std::wstring expected = fps_present && time_present
                    ? L"0.0 FPS, 0.0 ms"
                    : failure.empty() ? L"Waiting for KF2 frame data" : failure;
                CHECK(actual.telemetry == expected);
                CHECK(actual.live_fps == boundary.frames.fps);
                CHECK(actual.live_frame_time_ms == boundary.frames.frame_time_ms);
            }
        }
    }

    missing.frames.fps = 60.0;
    missing.frames.quality = telemetry::SampleQuality::good;
    const auto partial = telemetry_pipeline::build_status_projection(
        missing, L"", L"Stable", L"quality", L"Holding");
    CHECK(partial.telemetry == L"Waiting for KF2 frame data");
    CHECK(contains(partial.performance_analysis, L"Measurement good"));
    CHECK(!contains(partial.telemetry, L"CPU 0.0%"));
    CHECK(!contains(partial.telemetry, L"GPU total 0.0%"));
    CHECK(partial.performance_analysis ==
          L"Measurement good | stutters 0 | lost events 0 | analysis: Stable "
          L"| Adaptive: quality (Holding)");
    missing.frames.fps = 0.0;
    const auto zero = telemetry_pipeline::build_status_projection(
        missing, L"", L"Stable", L"quality", L"Holding");
    CHECK(zero.performance_analysis == partial.performance_analysis);
    CHECK(zero.live_fps == 0.0 && !zero.live_frame_time_ms);
    missing.frames.fps.reset();
    missing.frames.frame_time_ms = 16.7;
    const auto no_fps = telemetry_pipeline::build_status_projection(
        missing, L"", L"Stable", L"quality", L"Holding");
    CHECK(no_fps.performance_analysis ==
          L"Performance analysis unavailable | Adaptive: quality (Holding)");
    CHECK(!no_fps.live_fps && no_fps.live_frame_time_ms == 16.7);
    const auto saved_locale = std::locale::global(std::locale::classic());
    const std::wstring prefix =
        L"Measurement unavailable | stutters 0 | lost events 0 | analysis: ";
    const std::wstring suffix = L" | Adaptive: User settings (Holding)";
    for (const std::size_t length : {127U, 128U, 129U, 255U, 256U, 257U,
                                   511U, 512U, 513U, 4096U}) {
        std::wstring reason(length - prefix.size() - suffix.size(), L'r');
        reason.replace(1, 3, L"\0\u00e4\u03a9", 3);
        const auto expected = prefix + reason + suffix;
        telemetry_pipeline::TelemetryFrame boundary;
        boundary.frames.fps = 60.0;
        boundary.frames.frame_time_ms = 16.3;
        const auto actual = telemetry_pipeline::build_status_projection(
            boundary, L"", reason, L"User settings", L"Holding");
        reason.assign(L"changed after projection");
        CHECK(actual.telemetry == L"60.0 FPS, 16.3 ms");
        CHECK(actual.performance_analysis == expected);
    }
    CHECK(telemetry_pipeline::format_gib(0) == L"0.0 GiB");
    CHECK(telemetry_pipeline::format_gib(1) == L"0.0 GiB");
    CHECK(telemetry_pipeline::format_gib(53'687'091) == L"0.0 GiB");
    CHECK(telemetry_pipeline::format_gib(53'687'092) == L"0.1 GiB");
    CHECK(telemetry_pipeline::format_gib((1ULL << 30) - 1) == L"1.0 GiB");
    CHECK(telemetry_pipeline::format_gib(1ULL << 30) == L"1.0 GiB");
    CHECK(telemetry_pipeline::format_gib((1ULL << 30) + 1) == L"1.0 GiB");
    CHECK(telemetry_pipeline::format_gib(6ULL << 30) == L"6.0 GiB");
    CHECK(telemetry_pipeline::format_gib(
        std::numeric_limits<std::uint64_t>::max()) == L"17179869184.0 GiB");
    std::locale::global(std::locale{std::locale::classic(), new CommaPunctuation{}});
    const auto comma_projection = telemetry_pipeline::build_status_projection(
        frame, L"", L"GPU limited", L"balanced", L"Stable evidence");
    CHECK(comma_projection.telemetry ==
          L"61,5 FPS, 16,3 ms, CPU 24,0% (critical thread 82,0%), 3,25 cores"
          L" / 7 active threads, affinity 8C/16T, RAM 4,0 GiB, VRAM 6,0 GiB,"
          L" GPU total 36,00%, system RAM 75%, FleX 4 steps");
    CHECK(comma_projection.performance_analysis ==
          L"Measurement degraded | p95 18,5 ms | p99 22,0 ms | stutters 3"
          L" | lost events 2 | analysis: GPU limited"
          L" | Adaptive: balanced (Stable evidence)");
    CHECK(telemetry_pipeline::format_gib(53'687'092) == L"0,1 GiB");
    CHECK(telemetry_pipeline::format_gib(6ULL << 30) == L"6,0 GiB");
    CHECK(telemetry_pipeline::format_gib(
        std::numeric_limits<std::uint64_t>::max()) == L"17179869184,0 GiB");
    std::locale::global(saved_locale);
    return EXIT_SUCCESS;
}
