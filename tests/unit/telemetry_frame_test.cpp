#include <cmath>
#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_frame.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

bool approximately_equal(double left, double right) {
    return std::abs(left - right) < 0.0001;
}

kf2::telemetry_pipeline::TelemetryFrameInput complete_input() {
    using namespace kf2;
    telemetry_pipeline::TelemetryFrameInput input;
    input.identity = {4242, 9001};
    input.observed_at_ns = 8'000'000'000ULL;
    input.window.process = {4242, 9001, L"C:\\KF2\\KFGame.exe"};
    input.window.visible = true;
    input.window.foreground = true;
    input.frames.fps = 61.5;
    input.frames.average_fps = 60.0;
    input.frames.frame_time_ms = 16.26;
    input.frames.p95_ms = 18.5;
    input.frames.p99_ms = 22.0;
    input.frames.one_percent_low_fps = 48.0;
    input.frames.quality = telemetry::SampleQuality::good;
    input.frames.reason = telemetry::UnavailableReason::none;

    telemetry::ProcessMetrics process;
    process.cpu_percent = 24.0;
    process.system_cpu_percent = 67.0;
    process.critical_core_percent = 82.0;
    process.effective_core_usage = 3.25;
    process.dominant_thread_share_percent = 44.0;
    process.active_cpu_threads = 7;
    process.affinity_logical_processors = 16;
    process.affinity_physical_cores = 8;
    process.system_logical_processors = 24;
    process.working_set_bytes = 4ULL << 30U;
    input.process = process;

    telemetry::GpuMetrics gpu;
    gpu.gpu_percent = 28.0;
    gpu.adapter_gpu_percent = 31.0;
    gpu.dedicated_bytes = 6ULL << 30U;
    gpu.shared_bytes = 1ULL << 30U;
    gpu.adapter_local_usage_bytes = 7ULL << 30U;
    gpu.adapter_local_budget_bytes = 10ULL << 30U;
    gpu.quality = telemetry::SampleQuality::good;
    gpu.reason = telemetry::UnavailableReason::none;
    input.adapter_gpu = gpu;
    input.driver_gpu_percent = 36.0;
    input.adapter_luid = 77;
    input.adapter_vram_budget_bytes = 12ULL << 30U;

    telemetry::SystemMemoryMetrics memory;
    memory.total_physical_bytes = 32ULL << 30U;
    memory.available_physical_bytes = 8ULL << 30U;
    memory.commit_limit_bytes = 48ULL << 30U;
    memory.available_commit_bytes = 16ULL << 30U;
    memory.used_percent = 75.0;
    input.system_memory = memory;

    game::GameLogSession session;
    session.map = "KF-BioticsLab";
    session.net_mode = "NM_Standalone";
    session.phase = game::GameLogPhase::map_loaded;
    session.telemetry_corpse_awake = 9;
    session.telemetry_corpse_sleeping = 14;
    session.telemetry_observed_ns = 7'900'000'000ULL;
    input.gameplay = session;

    flex::ObservationSnapshot flex;
    flex.fresh = true;
    flex.aggregate_particles_fresh = true;
    flex.aggregate_active_particles = 120;
    flex.particle_capacity = 400;
    input.flex = flex;
    return input;
}

}  // namespace

int main() {
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;

    auto input = complete_input();
    const auto built = build_telemetry_frame(input);
    CHECK(built.has_value());
    const auto& frame = built.value();
    CHECK(frame.identity == input.identity);
    CHECK(frame.observed_at_ns == input.observed_at_ns);
    CHECK(frame.window.process.pid == input.identity.pid);
    CHECK(frame.frames.fps == input.frames.fps);
    CHECK(frame.frames.average_fps == input.frames.average_fps);
    CHECK(frame.frames.frame_time_ms == input.frames.frame_time_ms);
    CHECK(frame.frames.p95_ms == input.frames.p95_ms);
    CHECK(frame.frames.p99_ms == input.frames.p99_ms);
    CHECK(frame.frames.one_percent_low_fps ==
          input.frames.one_percent_low_fps);
    CHECK(frame.frames.quality == input.frames.quality);
    CHECK(frame.frames.reason == input.frames.reason);
    CHECK(frame.process.has_value());
    CHECK(frame.adapter_gpu.has_value());
    CHECK(frame.system_memory.has_value());
    CHECK(frame.gameplay.has_value());
    CHECK(frame.flex.has_value());
    CHECK(frame.active_gameplay);
    CHECK(frame.offline_gameplay);
    CHECK(frame.evidence.fresh);
    CHECK(frame.evidence.fps == input.frames.fps);
    CHECK(frame.evidence.p95_frame_time_ms == input.frames.p95_ms);
    CHECK(frame.evidence.cpu_percent == input.process->cpu_percent);
    CHECK(frame.evidence.system_cpu_percent ==
          input.process->system_cpu_percent);
    CHECK(frame.evidence.critical_core_percent ==
          input.process->critical_core_percent);
    CHECK(frame.evidence.effective_core_usage ==
          input.process->effective_core_usage);
    CHECK(frame.evidence.dominant_thread_share_percent ==
          input.process->dominant_thread_share_percent);
    CHECK(frame.evidence.active_cpu_threads ==
          input.process->active_cpu_threads);
    CHECK(frame.evidence.affinity_logical_processors ==
          input.process->affinity_logical_processors);
    CHECK(frame.evidence.affinity_physical_cores ==
          input.process->affinity_physical_cores);
    CHECK(frame.evidence.system_logical_processors ==
          input.process->system_logical_processors);
    CHECK(frame.evidence.process_gpu_percent ==
          input.adapter_gpu->gpu_percent);
    CHECK(frame.evidence.gpu_percent.has_value());
    CHECK(approximately_equal(*frame.evidence.gpu_percent, 36.0));
    CHECK(frame.evidence.dedicated_vram_bytes ==
          input.adapter_gpu->dedicated_bytes);
    CHECK(frame.evidence.dedicated_vram_budget_bytes ==
          input.adapter_vram_budget_bytes);
    CHECK(frame.evidence.adapter_vram_used_bytes ==
          input.adapter_gpu->adapter_local_usage_bytes);
    CHECK(frame.evidence.adapter_vram_budget_bytes ==
          input.adapter_gpu->adapter_local_budget_bytes);
    CHECK(frame.evidence.system_ram_budget_bytes ==
          input.system_memory->total_physical_bytes);
    CHECK(frame.evidence.system_ram_used_bytes.has_value());
    CHECK(*frame.evidence.system_ram_used_bytes == 24ULL << 30U);
    CHECK(frame.evidence.system_commit_budget_bytes == 48ULL << 30U);
    CHECK(frame.evidence.system_commit_used_bytes == 32ULL << 30U);
    CHECK(frame.evidence.process_private_bytes ==
          input.process->private_bytes);

    // Frame construction is observational and does not mutate its input.
    CHECK(input.driver_gpu_percent == 36.0);
    CHECK(input.gameplay->map == "KF-BioticsLab");
    CHECK(input.flex->aggregate_active_particles == 120);

    auto adapter_fallback = complete_input();
    adapter_fallback.driver_gpu_percent.reset();
    const auto adapter_frame = build_telemetry_frame(adapter_fallback);
    CHECK(adapter_frame.has_value());
    CHECK(adapter_frame.value().evidence.gpu_percent.has_value());
    CHECK(approximately_equal(
        *adapter_frame.value().evidence.gpu_percent, 31.0));

    auto driver_only = complete_input();
    driver_only.adapter_gpu.reset();
    const auto driver_frame = build_telemetry_frame(driver_only);
    CHECK(driver_frame.has_value());
    CHECK(driver_frame.value().evidence.gpu_percent.has_value());
    CHECK(approximately_equal(
        *driver_frame.value().evidence.gpu_percent, 36.0));
    CHECK(!driver_frame.value().evidence.dedicated_vram_bytes.has_value());
    CHECK(!driver_frame.value().evidence.dedicated_vram_budget_bytes.has_value());

    auto absent = complete_input();
    absent.frames = {};
    absent.process.reset();
    absent.adapter_gpu.reset();
    absent.driver_gpu_percent.reset();
    absent.system_memory.reset();
    absent.gameplay.reset();
    absent.flex.reset();
    absent.adapter_luid.reset();
    absent.adapter_vram_budget_bytes.reset();
    const auto absent_frame = build_telemetry_frame(absent);
    CHECK(absent_frame.has_value());
    CHECK(!absent_frame.value().active_gameplay);
    CHECK(!absent_frame.value().offline_gameplay);
    CHECK(!absent_frame.value().evidence.fresh);
    CHECK(!absent_frame.value().evidence.fps.has_value());
    CHECK(!absent_frame.value().evidence.cpu_percent.has_value());
    CHECK(!absent_frame.value().evidence.system_cpu_percent.has_value());
    CHECK(!absent_frame.value().evidence.gpu_percent.has_value());
    CHECK(!absent_frame.value().evidence.system_ram_used_bytes.has_value());
    CHECK(!absent_frame.value().evidence.dedicated_vram_bytes.has_value());

    auto degraded = complete_input();
    degraded.frames.quality = telemetry::SampleQuality::degraded;
    degraded.frames.reason = telemetry::UnavailableReason::discontinuity;
    const auto degraded_frame = build_telemetry_frame(degraded);
    CHECK(degraded_frame.has_value());
    CHECK(degraded_frame.value().evidence.fresh);

    auto unavailable = complete_input();
    unavailable.frames.quality = telemetry::SampleQuality::unavailable;
    unavailable.frames.reason = telemetry::UnavailableReason::source_failure;
    const auto unavailable_frame = build_telemetry_frame(unavailable);
    CHECK(unavailable_frame.has_value());
    CHECK(!unavailable_frame.value().evidence.fresh);
    CHECK(unavailable_frame.value().evidence.fps.has_value());

    auto online = complete_input();
    online.gameplay->net_mode = "NM_Client";
    const auto online_frame = build_telemetry_frame(online);
    CHECK(online_frame.has_value());
    CHECK(online_frame.value().active_gameplay);
    CHECK(!online_frame.value().offline_gameplay);

    auto menu = complete_input();
    menu.gameplay->main_menu = true;
    menu.gameplay->phase = game::GameLogPhase::main_menu;
    const auto menu_frame = build_telemetry_frame(menu);
    CHECK(menu_frame.has_value());
    CHECK(!menu_frame.value().active_gameplay);
    CHECK(!menu_frame.value().offline_gameplay);
    CHECK(!menu_frame.value().evidence.fresh);

    auto mismatch = complete_input();
    mismatch.window.process.process_start_id++;
    const auto rejected = build_telemetry_frame(mismatch);
    CHECK(!rejected.has_value());
    CHECK(rejected.error().code == ErrorCode::stale_data);
    return EXIT_SUCCESS;
}
