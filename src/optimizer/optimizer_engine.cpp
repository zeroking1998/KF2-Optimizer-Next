#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::optimizer {
namespace {

Bottleneck classify(const OptimizerInput& input) {
    const auto& evidence = input.evidence;
    if (!evidence.fresh || !evidence.fps || !evidence.cpu_percent ||
        !evidence.gpu_percent || !valid_target_fps(input.target_fps)) {
        return Bottleneck::unavailable;
    }
    if (evidence.dedicated_vram_bytes && evidence.dedicated_vram_budget_bytes &&
        *evidence.dedicated_vram_budget_bytes > 0 &&
        static_cast<long double>(*evidence.dedicated_vram_bytes) /
                static_cast<long double>(*evidence.dedicated_vram_budget_bytes) >=
            0.95L) {
        return Bottleneck::vram_streaming;
    }
    if (evidence.system_ram_used_bytes && evidence.system_ram_budget_bytes &&
        *evidence.system_ram_budget_bytes > 0 &&
        static_cast<long double>(*evidence.system_ram_used_bytes) /
                static_cast<long double>(*evidence.system_ram_budget_bytes) >=
            0.92L) {
        return Bottleneck::ram_pressure;
    }
    const double target = static_cast<double>(input.target_fps);
    const bool critical_thread_high = evidence.critical_core_percent &&
                                      *evidence.critical_core_percent >= 90.0;
    const bool main_thread_dominant = evidence.critical_core_percent &&
        evidence.dominant_thread_share_percent &&
        *evidence.critical_core_percent >= 70.0 &&
        *evidence.dominant_thread_share_percent >= 35.0;
    const double affinity_capacity = static_cast<double>(
        evidence.affinity_logical_processors.value_or(0));
    const bool parallel_cpu_high = affinity_capacity > 0.0 &&
        evidence.effective_core_usage.value_or(0.0) >=
            std::max(2.0, affinity_capacity * 0.70);
    const bool cpu_high = *evidence.cpu_percent >= 85.0 ||
                          critical_thread_high || main_thread_dominant ||
                          parallel_cpu_high;
    if (std::abs(*evidence.fps - target) <= std::max(1.0, target * 0.02) &&
        *evidence.cpu_percent < 80.0 && !critical_thread_high &&
        !main_thread_dominant && !parallel_cpu_high &&
        *evidence.gpu_percent < 80.0) {
        return Bottleneck::frame_cap;
    }
    if (*evidence.gpu_percent >= 92.0 && !cpu_high) {
        return Bottleneck::gpu;
    }
    if (cpu_high && *evidence.gpu_percent < 90.0) {
        return Bottleneck::cpu;
    }
    return Bottleneck::balanced;
}

std::wstring_view bottleneck_reason(Bottleneck bottleneck) {
    switch (bottleneck) {
        case Bottleneck::gpu: return L"Fresh telemetry indicates a GPU limit";
        case Bottleneck::cpu:
            return L"Fresh telemetry indicates a saturated KF2 process, dominant engine thread or broad parallel CPU pressure with GPU reserve";
        case Bottleneck::ram_pressure:
            return L"Fresh system telemetry indicates physical RAM pressure";
        case Bottleneck::vram_streaming:
            return L"Fresh telemetry indicates dedicated VRAM pressure";
        case Bottleneck::frame_cap:
            return L"Frame rate is close to the selected cap without saturation";
        case Bottleneck::balanced: return L"No single limiting component is proven";
        case Bottleneck::unavailable:
            return L"Fresh complete telemetry is required for Adaptive decisions";
    }
    return L"Adaptive decision unavailable";
}

}  // namespace

std::optional<StartupMemoryProfile> recommended_startup_memory_profile(
    std::uint64_t dedicated_vram_bytes) noexcept {
    if (dedicated_vram_bytes == 0) return std::nullopt;
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    const int texture_pool_size_mb = dedicated_vram_bytes >= 16 * gib ? 6000
        : dedicated_vram_bytes >= 10 * gib ? 5000
        : dedicated_vram_bytes >= 8 * gib ? 4000
        : dedicated_vram_bytes >= 6 * gib ? 3000
        : dedicated_vram_bytes >= 4 * gib ? 2000
        : dedicated_vram_bytes >= 2 * gib ? 1000
        : 160;
    const bool large_streaming_budget = dedicated_vram_bytes >= 6 * gib;
    return StartupMemoryProfile{
        .texture_pool_size_mb = texture_pool_size_mb,
        .memory_margin_mb = large_streaming_budget ? 128 : 20,
        .streaming_hysteresis_limit = large_streaming_budget ? 40 : 20};
}

OptimizerDecision evaluate(const OptimizerInput& input) {
    OptimizerDecision decision;
    decision.bottleneck = classify(input);
    decision.reason = bottleneck_reason(decision.bottleneck);
    switch (decision.bottleneck) {
        case Bottleneck::gpu:
        case Bottleneck::cpu:
        case Bottleneck::ram_pressure:
        case Bottleneck::vram_streaming:
        case Bottleneck::frame_cap: decision.confidence = Confidence::high; break;
        case Bottleneck::balanced: decision.confidence = Confidence::medium; break;
        case Bottleneck::unavailable: decision.confidence = Confidence::unavailable; break;
    }

    return decision;
}

}  // namespace kf2::optimizer
