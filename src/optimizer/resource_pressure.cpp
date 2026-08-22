#include "kf2/optimizer/resource_pressure.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::optimizer {
namespace {

constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;

bool valid_nonnegative(const std::optional<double>& value) noexcept {
    return value && std::isfinite(*value) && *value >= 0.0;
}

double normalized(double value, double healthy, double critical) noexcept {
    if (!std::isfinite(value) || critical <= healthy) return 0.0;
    return std::clamp((value - healthy) / (critical - healthy), 0.0, 1.0);
}

double occupancy(const std::optional<double>& used,
                 const std::optional<double>& budget) noexcept {
    if (!valid_nonnegative(used) || !valid_nonnegative(budget) ||
        *budget <= 0.0) {
        return 0.0;
    }
    return std::clamp(*used / *budget, 0.0, 1.0);
}

double confidence_for(std::initializer_list<bool> signals) noexcept {
    const auto present = std::count(signals.begin(), signals.end(), true);
    return std::clamp(0.35 + static_cast<double>(present) * 0.20,
                      0.0, 0.95);
}

PressureTrend trend_for(double previous, double current) noexcept {
    constexpr double kTrendDeadband = 0.025;
    if (current > previous + kTrendDeadband) return PressureTrend::rising;
    if (current < previous - kTrendDeadband) return PressureTrend::falling;
    return PressureTrend::stable;
}

}  // namespace

ResourcePressureSnapshot ResourcePressureEstimator::evaluate(
    const ResourcePressureInput& input) noexcept {
    if (input.discontinuity || input.timestamp_ns < previous_timestamp_ns_) {
        reset();
    }

    ResourcePressureSnapshot result;
    const double capacity = static_cast<double>(
        input.affinity_logical_processors.value_or(0));
    const double parallel_percent = capacity > 0.0
        ? input.effective_core_usage.value_or(0.0) * 100.0 / capacity
        : 0.0;
    result.cpu.raw = std::max({
        normalized(input.process_cpu_percent.value_or(0.0), 65.0, 92.0),
        normalized(input.critical_thread_percent.value_or(0.0), 65.0, 96.0),
        normalized(parallel_percent, 55.0, 85.0)});
    result.cpu.confidence = confidence_for({
        input.process_cpu_percent.has_value(),
        input.critical_thread_percent.has_value(),
        input.effective_core_usage.has_value() && capacity > 0.0});

    const double process_gpu = normalized(
        input.process_gpu_percent.value_or(0.0), 70.0, 97.0);
    const double adapter_gpu = normalized(
        input.adapter_gpu_percent.value_or(0.0), 78.0, 99.0);
    // Whole-adapter pressure matters, but cannot by itself prove that KF2 is
    // the cause. It is deliberately discounted when process attribution is
    // available and low.
    result.gpu.raw = input.process_gpu_percent
        ? std::max(process_gpu, adapter_gpu * 0.65)
        : adapter_gpu;
    result.gpu.confidence = confidence_for({
        input.process_gpu_percent.has_value(),
        input.adapter_gpu_percent.has_value()});
    if (!input.process_gpu_percent && input.adapter_gpu_percent) {
        result.gpu.confidence = std::min(result.gpu.confidence, 0.55);
    }

    const double vram_occupancy = occupancy(
        input.vram_used_bytes, input.vram_budget_bytes);
    result.vram.raw = normalized(vram_occupancy, 0.80, 0.97);
    result.vram.confidence = confidence_for({
        input.vram_used_bytes.has_value(),
        input.vram_budget_bytes.has_value()});
    if (valid_nonnegative(input.vram_used_bytes) &&
        valid_nonnegative(input.vram_budget_bytes) &&
        *input.vram_budget_bytes >= *input.vram_used_bytes) {
        result.vram.reserve_bytes =
            *input.vram_budget_bytes - *input.vram_used_bytes;
        if (*result.vram.reserve_bytes < 0.5 * kBytesPerGiB) {
            result.vram.raw = std::max(result.vram.raw, 0.90);
        }
    }

    const double ram_occupancy = occupancy(
        input.ram_used_bytes, input.ram_budget_bytes);
    const double commit_occupancy = occupancy(
        input.commit_used_bytes, input.commit_budget_bytes);
    const double paging = std::clamp(
        input.paging_pressure.value_or(0.0), 0.0, 1.0);
    result.ram.raw = std::max({
        normalized(ram_occupancy, 0.78, 0.96),
        normalized(commit_occupancy, 0.75, 0.95), paging});
    result.ram.confidence = confidence_for({
        input.ram_used_bytes.has_value(), input.ram_budget_bytes.has_value(),
        input.commit_used_bytes.has_value(),
        input.commit_budget_bytes.has_value(),
        input.process_private_bytes.has_value(),
        input.paging_pressure.has_value()});
    if (valid_nonnegative(input.ram_used_bytes) &&
        valid_nonnegative(input.ram_budget_bytes) &&
        *input.ram_budget_bytes >= *input.ram_used_bytes) {
        result.ram.reserve_bytes =
            *input.ram_budget_bytes - *input.ram_used_bytes;
        if (*result.ram.reserve_bytes < 1.0 * kBytesPerGiB) {
            result.ram.raw = std::max(result.ram.raw, 0.90);
        }
    }
    if (valid_nonnegative(input.commit_used_bytes) &&
        valid_nonnegative(input.commit_budget_bytes) &&
        *input.commit_budget_bytes >= *input.commit_used_bytes) {
        const double commit_reserve =
            *input.commit_budget_bytes - *input.commit_used_bytes;
        result.ram.reserve_bytes = result.ram.reserve_bytes
            ? std::min(*result.ram.reserve_bytes, commit_reserve)
            : commit_reserve;
        if (commit_reserve < 1.0 * kBytesPerGiB) {
            result.ram.raw = std::max(result.ram.raw, 0.90);
        }
    }

    std::array<ResourcePressureSignal*, 4> signals{
        &result.cpu, &result.gpu, &result.vram, &result.ram};
    for (std::size_t index = 0; index < signals.size(); ++index) {
        auto& state = states_[index];
        auto& signal = *signals[index];
        if (!state.initialized) {
            state.initialized = true;
            state.smoothed = signal.raw;
            signal.trend = PressureTrend::unknown;
        } else {
            signal.trend = trend_for(state.previous_raw, signal.raw);
            // Fast attack catches emerging pressure; slow release prevents
            // quality oscillation after a short recovery.
            const double alpha = signal.raw > state.smoothed ? 0.55 : 0.18;
            state.smoothed += alpha * (signal.raw - state.smoothed);
        }
        state.previous_raw = signal.raw;
        signal.smoothed = std::clamp(state.smoothed, 0.0, 1.0);
    }

    const std::array<ResourceKind, 4> kinds{
        ResourceKind::cpu, ResourceKind::gpu,
        ResourceKind::vram, ResourceKind::ram};
    double best_evidence = 0.0;
    bool all_core_signals_available = true;
    for (std::size_t index = 0; index < signals.size(); ++index) {
        const auto& signal = *signals[index];
        const double evidence = signal.smoothed * signal.confidence;
        if (evidence > best_evidence) {
            best_evidence = evidence;
            result.primary = kinds[index];
            result.primary_confidence = signal.confidence;
        }
        result.total = std::max(result.total, signal.smoothed);
        if (signal.confidence < 0.55) all_core_signals_available = false;
    }
    // A primary cause is stronger than a warning signal. Requiring combined
    // pressure and confidence prevents a busy adapter owned by another
    // process from being mislabeled as a proven KF2 GPU bottleneck.
    if (best_evidence < 0.50) {
        result.primary = ResourceKind::unknown;
        result.primary_confidence = 0.0;
    }
    result.headroom = std::clamp(1.0 - result.total, 0.0, 1.0);

    const double target_ms = input.target_fps > 0
        ? 1000.0 / static_cast<double>(input.target_fps) : 0.0;
    const double observed_ms = input.p95_frame_time_ms.value_or(
        input.frame_time_ms.value_or(target_ms));
    result.frame_budget_deficit_ms = target_ms > 0.0
        ? std::max(0.0, observed_ms - target_ms) : 0.0;
    result.predicted_deficit_ms = result.frame_budget_deficit_ms;
    if (previous_p95_ms_ && previous_timestamp_ns_ != 0 &&
        input.timestamp_ns > previous_timestamp_ns_ &&
        input.p95_frame_time_ms) {
        const double elapsed_seconds = static_cast<double>(
            input.timestamp_ns - previous_timestamp_ns_) / 1'000'000'000.0;
        if (elapsed_seconds > 0.0 && elapsed_seconds <= 5.0) {
            const double rising_ms_per_second = std::max(
                0.0, (*input.p95_frame_time_ms - *previous_p95_ms_) /
                         elapsed_seconds);
            result.predicted_deficit_ms +=
                std::min(rising_ms_per_second * 2.0, target_ms);
        }
    }
    previous_p95_ms_ = input.p95_frame_time_ms;
    previous_timestamp_ns_ = input.timestamp_ns;
    // Frame stability is evaluated independently by the Governor's target
    // bands. This flag answers only whether all resource reserves are known
    // and comfortably below pressure thresholds.
    result.recovery_safe = all_core_signals_available && result.total <= 0.55;
    return result;
}

void ResourcePressureEstimator::reset() noexcept {
    states_ = {};
    previous_p95_ms_.reset();
    previous_timestamp_ns_ = 0;
}

const char* resource_kind_name(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::cpu: return "cpu";
        case ResourceKind::gpu: return "gpu";
        case ResourceKind::vram: return "vram";
        case ResourceKind::ram: return "ram";
        case ResourceKind::unknown: return "unknown";
    }
    return "unknown";
}

const char* pressure_trend_name(PressureTrend trend) noexcept {
    switch (trend) {
        case PressureTrend::unknown: return "unknown";
        case PressureTrend::falling: return "falling";
        case PressureTrend::stable: return "stable";
        case PressureTrend::rising: return "rising";
    }
    return "unknown";
}

}  // namespace kf2::optimizer
