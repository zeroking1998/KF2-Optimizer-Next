#include <cmath>
#include <cstdlib>
#include <iostream>

#include "kf2/optimizer/resource_pressure.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

using namespace kf2::optimizer;

ResourcePressureInput healthy(std::uint64_t now) {
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    return {
        .timestamp_ns = now,
        .target_fps = 60,
        .frame_time_ms = 15.5,
        .p95_frame_time_ms = 16.0,
        .process_cpu_percent = 35.0,
        .system_cpu_percent = 42.0,
        .critical_thread_percent = 45.0,
        .effective_core_usage = 3.0,
        .affinity_logical_processors = 16,
        .process_gpu_percent = 42.0,
        .adapter_gpu_percent = 48.0,
        .vram_used_bytes = 4.0 * gib,
        .vram_budget_bytes = 12.0 * gib,
        .ram_used_bytes = 12.0 * gib,
        .ram_budget_bytes = 32.0 * gib,
        .commit_used_bytes = 18.0 * gib,
        .commit_budget_bytes = 48.0 * gib,
        .process_private_bytes = 4.0 * gib,
        .paging_pressure = 0.0,
    };
}

}  // namespace

int main() {
    using namespace kf2::optimizer;
    constexpr std::uint64_t start = 10'000'000'000ULL;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;

    ResourcePressureEstimator stable_estimator;
    const auto stable = stable_estimator.evaluate(healthy(start));
    CHECK(stable.primary == ResourceKind::unknown);
    CHECK(stable.total < 0.20);
    CHECK(stable.headroom > 0.80);
    CHECK(stable.frame_budget_deficit_ms == 0.0);
    CHECK(stable.recovery_safe);

    ResourcePressureEstimator cpu_estimator;
    auto cpu_input = healthy(start);
    cpu_input.critical_thread_percent = 98.0;
    const auto cpu = cpu_estimator.evaluate(cpu_input);
    CHECK(cpu.primary == ResourceKind::cpu);
    CHECK(cpu.cpu.raw > 0.95);
    CHECK(cpu.primary_confidence >= 0.90);
    CHECK(!cpu.recovery_safe);

    ResourcePressureEstimator external_gpu_estimator;
    auto external_gpu_input = healthy(start);
    external_gpu_input.process_gpu_percent = 18.0;
    external_gpu_input.adapter_gpu_percent = 99.0;
    const auto external_gpu = external_gpu_estimator.evaluate(
        external_gpu_input);
    CHECK(external_gpu.gpu.raw <= 0.65);
    CHECK(external_gpu.primary != ResourceKind::gpu);

    ResourcePressureEstimator shared_gpu_estimator;
    auto shared_gpu_input = external_gpu_input;
    shared_gpu_input.p95_frame_time_ms = 24.0;
    const auto shared_gpu = shared_gpu_estimator.evaluate(shared_gpu_input);
    CHECK(shared_gpu.primary == ResourceKind::gpu);
    CHECK(shared_gpu.gpu.raw > 0.95);

    ResourcePressureEstimator external_cpu_estimator;
    auto external_cpu_input = healthy(start);
    external_cpu_input.system_cpu_percent = 98.0;
    const auto external_cpu = external_cpu_estimator.evaluate(
        external_cpu_input);
    CHECK(external_cpu.cpu.raw <= 0.65);
    CHECK(external_cpu.primary != ResourceKind::cpu);

    ResourcePressureEstimator shared_cpu_estimator;
    auto shared_cpu_input = external_cpu_input;
    shared_cpu_input.p95_frame_time_ms = 24.0;
    const auto shared_cpu = shared_cpu_estimator.evaluate(shared_cpu_input);
    CHECK(shared_cpu.primary == ResourceKind::cpu);
    CHECK(shared_cpu.cpu.raw > 0.95);

    ResourcePressureEstimator game_gpu_estimator;
    auto game_gpu_input = healthy(start);
    game_gpu_input.process_gpu_percent = 98.0;
    game_gpu_input.adapter_gpu_percent = 99.0;
    const auto game_gpu = game_gpu_estimator.evaluate(game_gpu_input);
    CHECK(game_gpu.primary == ResourceKind::gpu);
    CHECK(game_gpu.gpu.raw > 0.95);

    ResourcePressureEstimator vram_estimator;
    auto vram_input = healthy(start);
    vram_input.vram_used_bytes = 11.7 * gib;
    const auto vram = vram_estimator.evaluate(vram_input);
    CHECK(vram.primary == ResourceKind::vram);
    CHECK(vram.vram.raw >= 0.90);
    CHECK(vram.vram.reserve_bytes.has_value());
    CHECK(*vram.vram.reserve_bytes < 0.5 * gib);

    ResourcePressureEstimator over_budget_estimator;
    auto over_budget_input = healthy(start);
    over_budget_input.vram_used_bytes = 13.0 * gib;
    const auto over_budget = over_budget_estimator.evaluate(over_budget_input);
    CHECK(over_budget.vram.raw == 1.0);
    CHECK(over_budget.primary == ResourceKind::vram);

    ResourcePressureEstimator ram_estimator;
    auto ram_input = healthy(start);
    ram_input.ram_used_bytes = 31.5 * gib;
    const auto ram = ram_estimator.evaluate(ram_input);
    CHECK(ram.primary == ResourceKind::ram);
    CHECK(ram.ram.raw >= 0.90);

    ResourcePressureEstimator commit_estimator;
    auto commit_input = healthy(start);
    commit_input.commit_used_bytes = 47.5 * gib;
    const auto commit = commit_estimator.evaluate(commit_input);
    CHECK(commit.primary == ResourceKind::ram);
    CHECK(commit.ram.raw >= 0.90);
    CHECK(commit.ram.reserve_bytes.has_value());
    CHECK(*commit.ram.reserve_bytes < 1.0 * gib);

    ResourcePressureEstimator trend_estimator;
    auto first = healthy(start);
    first.process_gpu_percent = 70.0;
    static_cast<void>(trend_estimator.evaluate(first));
    auto rising = first;
    rising.timestamp_ns += 1'000'000'000ULL;
    rising.process_gpu_percent = 92.0;
    rising.p95_frame_time_ms = 19.0;
    const auto rising_result = trend_estimator.evaluate(rising);
    CHECK(rising_result.gpu.trend == PressureTrend::rising);
    CHECK(rising_result.predicted_deficit_ms >
          rising_result.frame_budget_deficit_ms);

    auto reset = rising;
    reset.timestamp_ns += 1'000'000'000ULL;
    reset.discontinuity = true;
    const auto reset_result = trend_estimator.evaluate(reset);
    CHECK(reset_result.gpu.trend == PressureTrend::unknown);

    ResourcePressureEstimator missing_estimator;
    auto missing = healthy(start);
    missing.process_gpu_percent.reset();
    missing.adapter_gpu_percent.reset();
    const auto incomplete = missing_estimator.evaluate(missing);
    CHECK(incomplete.recovery_safe);

    ResourcePressureEstimator insufficient_estimator;
    auto insufficient = healthy(start);
    insufficient.process_gpu_percent.reset();
    insufficient.adapter_gpu_percent.reset();
    insufficient.vram_used_bytes.reset();
    insufficient.vram_budget_bytes.reset();
    insufficient.ram_used_bytes.reset();
    insufficient.ram_budget_bytes.reset();
    insufficient.commit_used_bytes.reset();
    insufficient.commit_budget_bytes.reset();
    insufficient.process_private_bytes.reset();
    insufficient.paging_pressure.reset();
    CHECK(!insufficient_estimator.evaluate(insufficient).recovery_safe);

    CHECK(std::string_view{resource_kind_name(ResourceKind::vram)} == "vram");
    CHECK(std::string_view{pressure_trend_name(PressureTrend::rising)} ==
          "rising");
    return EXIT_SUCCESS;
}
