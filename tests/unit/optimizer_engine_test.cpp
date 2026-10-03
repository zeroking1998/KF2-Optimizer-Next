#include <cstdlib>
#include <iostream>

#include "kf2/optimizer/optimizer_engine.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2;
    using namespace kf2::optimizer;

    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    CHECK(!recommended_startup_memory_profile(0).has_value());
    CHECK(recommended_startup_memory_profile(1 * gib)->texture_pool_size_mb ==
          160);
    CHECK(recommended_startup_memory_profile(2 * gib)->texture_pool_size_mb ==
          1000);
    CHECK(recommended_startup_memory_profile(4 * gib)->texture_pool_size_mb ==
          2000);
    CHECK(recommended_startup_memory_profile(6 * gib)->texture_pool_size_mb ==
          3000);
    CHECK(recommended_startup_memory_profile(8 * gib)->texture_pool_size_mb ==
          4000);
    CHECK(recommended_startup_memory_profile(10 * gib)->texture_pool_size_mb ==
          5000);
    const auto large_memory = recommended_startup_memory_profile(24 * gib);
    CHECK(large_memory.has_value());
    CHECK(large_memory->texture_pool_size_mb == 6000);
    CHECK(large_memory->memory_margin_mb == 128);
    CHECK(large_memory->streaming_hysteresis_limit == 40);
    const auto small_memory = recommended_startup_memory_profile(4 * gib);
    CHECK(small_memory->memory_margin_mb == 20);
    CHECK(small_memory->streaming_hysteresis_limit == 20);

    OptimizerInput unavailable;
    const auto no_evidence = evaluate(unavailable);
    CHECK(no_evidence.bottleneck == Bottleneck::unavailable);
    CHECK(no_evidence.confidence == Confidence::unavailable);
    CHECK(no_evidence.reason == L"Fresh complete telemetry is required for Adaptive decisions");

    OptimizerInput gpu_bound{
        .target_fps = 120,
        .evidence = {.fresh = true, .fps = 72.0, .p95_frame_time_ms = 18.0,
                     .cpu_percent = 42.0, .gpu_percent = 97.0},
    };
    const auto gpu = evaluate(gpu_bound);
    CHECK(gpu.bottleneck == Bottleneck::gpu);
    CHECK(gpu.confidence == Confidence::high);
    CHECK(gpu.reason == L"Fresh telemetry indicates a GPU limit");

    OptimizerInput capped = gpu_bound;
    capped.target_fps = 60;
    capped.evidence = {.fresh = true, .fps = 59.8, .p95_frame_time_ms = 16.9,
                       .cpu_percent = 30.0, .gpu_percent = 45.0};
    auto cap = evaluate(capped);
    CHECK(cap.bottleneck == Bottleneck::frame_cap);

    OptimizerInput cpu_bound = gpu_bound;
    cpu_bound.evidence = {.fresh = true, .fps = 70.0, .p95_frame_time_ms = 22.0,
                          .cpu_percent = 92.0, .gpu_percent = 55.0};
    CHECK(evaluate(cpu_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput critical_thread_bound = gpu_bound;
    critical_thread_bound.evidence = {
        .fresh = true, .fps = 45.0, .p95_frame_time_ms = 28.0,
        .cpu_percent = 5.0, .critical_core_percent = 98.0,
        .gpu_percent = 16.0};
    CHECK(evaluate(critical_thread_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput measured_main_thread_bound = gpu_bound;
    measured_main_thread_bound.evidence = {
        .fresh = true, .fps = 45.0, .p95_frame_time_ms = 28.0,
        .cpu_percent = 5.0, .critical_core_percent = 80.0,
        .effective_core_usage = 1.5,
        .dominant_thread_share_percent = 54.0,
        .active_cpu_threads = 7,
        .affinity_logical_processors = 16,
        .affinity_physical_cores = 8,
        .system_logical_processors = 32,
        .gpu_percent = 16.0};
    CHECK(evaluate(measured_main_thread_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput vram_bound = gpu_bound;
    vram_bound.evidence = {.fresh = true, .fps = 70.0, .p95_frame_time_ms = 25.0,
                           .cpu_percent = 55.0, .gpu_percent = 85.0,
                           .dedicated_vram_bytes = 7'900,
                           .dedicated_vram_budget_bytes = 8'000};
    CHECK(evaluate(vram_bound).bottleneck == Bottleneck::vram_streaming);

    OptimizerInput ram_bound = gpu_bound;
    ram_bound.evidence = {.fresh = true, .fps = 70.0,
                          .p95_frame_time_ms = 25.0,
                          .cpu_percent = 40.0, .gpu_percent = 60.0,
                          .system_ram_used_bytes = 7'500,
                          .system_ram_budget_bytes = 8'000};
    CHECK(evaluate(ram_bound).bottleneck == Bottleneck::ram_pressure);

    auto incomplete = gpu_bound;
    incomplete.evidence.fresh = false;
    CHECK(evaluate(incomplete).bottleneck == Bottleneck::unavailable);
    incomplete = gpu_bound;
    incomplete.evidence.cpu_percent.reset();
    CHECK(evaluate(incomplete).bottleneck == Bottleneck::unavailable);
    incomplete = gpu_bound;
    incomplete.evidence.gpu_percent.reset();
    CHECK(evaluate(incomplete).bottleneck == Bottleneck::unavailable);
    incomplete = gpu_bound;
    incomplete.evidence.fps.reset();
    CHECK(evaluate(incomplete).bottleneck == Bottleneck::unavailable);

    auto balanced = gpu_bound;
    balanced.evidence.gpu_percent = 65.0;
    CHECK(evaluate(balanced).bottleneck == Bottleneck::balanced);
    CHECK(evaluate(balanced).confidence == Confidence::medium);

    for (const int target : {10, 241}) {
        gpu_bound.target_fps = target;
        CHECK(evaluate(gpu_bound).bottleneck == Bottleneck::unavailable);
        CHECK(evaluate(gpu_bound).confidence == Confidence::unavailable);
    }
    for (const int target : {30, 240}) {
        gpu_bound.target_fps = target;
        CHECK(evaluate(gpu_bound).bottleneck == Bottleneck::gpu);
    }
    return EXIT_SUCCESS;
}
