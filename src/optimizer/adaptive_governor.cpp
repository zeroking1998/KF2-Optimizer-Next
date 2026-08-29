#include "kf2/optimizer/adaptive_governor.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kf2::optimizer {
namespace {

bool finite_positive(const std::optional<double>& value) noexcept {
    return value && std::isfinite(*value) && *value > 0.0;
}

bool finite_nonnegative(const std::optional<double>& value) noexcept {
    return value && std::isfinite(*value) && *value >= 0.0;
}

bool valid_percent(const std::optional<double>& value) noexcept {
    return !value || (std::isfinite(*value) && *value >= 0.0 && *value <= 100.0);
}

bool valid_pressure(const std::optional<double>& value) noexcept {
    return !value || (std::isfinite(*value) && *value >= 0.0 && *value <= 1.0);
}

bool valid_core_usage(const std::optional<double>& value) noexcept {
    return !value || (std::isfinite(*value) && *value >= 0.0 &&
                      *value <= 1024.0);
}

double clamp01(double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

enum class FrameSignalLevel : unsigned int {
    healthy = 0,
    warning = 1,
    corrective = 2,
    emergency = 3,
};

FrameSignalLevel level_for_fps(
    double fps, const AdaptiveStabilityBands& bands) noexcept {
    if (!std::isfinite(fps) || fps <= 0.0) {
        return FrameSignalLevel::healthy;
    }
    constexpr double kBoundaryEpsilonFps = 0.000001;
    if (fps <= 1000.0 / bands.critical_frame_time_ms +
                   kBoundaryEpsilonFps) {
        return FrameSignalLevel::emergency;
    }
    if (fps <= 1000.0 / bands.corrective_frame_time_ms +
                   kBoundaryEpsilonFps) {
        return FrameSignalLevel::corrective;
    }
    if (fps <= 1000.0 / bands.warning_frame_time_ms +
                   kBoundaryEpsilonFps) {
        return FrameSignalLevel::warning;
    }
    return FrameSignalLevel::healthy;
}

FrameSignalLevel level_for_frame_time(
    double frame_time_ms, const AdaptiveStabilityBands& bands) noexcept {
    if (!std::isfinite(frame_time_ms) || frame_time_ms <= 0.0) {
        return FrameSignalLevel::healthy;
    }
    if (frame_time_ms >= bands.critical_frame_time_ms) {
        return FrameSignalLevel::emergency;
    }
    if (frame_time_ms >= bands.corrective_frame_time_ms) {
        return FrameSignalLevel::corrective;
    }
    if (frame_time_ms >= bands.warning_frame_time_ms) {
        return FrameSignalLevel::warning;
    }
    return FrameSignalLevel::healthy;
}

FrameSignalLevel level_for_tail(
    double p95_frame_time_ms, double target_frame_time_ms) noexcept {
    if (!std::isfinite(p95_frame_time_ms) || p95_frame_time_ms <= 0.0 ||
        !std::isfinite(target_frame_time_ms) || target_frame_time_ms <= 0.0) {
        return FrameSignalLevel::healthy;
    }
    const double ratio = p95_frame_time_ms / target_frame_time_ms;
    if (ratio >= 1.30) return FrameSignalLevel::emergency;
    if (ratio >= 1.15) return FrameSignalLevel::corrective;
    if (ratio >= 1.06) return FrameSignalLevel::warning;
    return FrameSignalLevel::healthy;
}

FrameSignalLevel level_for_stutters(std::uint64_t count) noexcept {
    if (count >= 6) return FrameSignalLevel::emergency;
    if (count >= 3) return FrameSignalLevel::corrective;
    if (count >= 1) return FrameSignalLevel::warning;
    return FrameSignalLevel::healthy;
}

FrameSignalLevel maximum(
    FrameSignalLevel left, FrameSignalLevel right) noexcept {
    return static_cast<unsigned int>(left) >=
                   static_cast<unsigned int>(right)
        ? left : right;
}

bool at_least(FrameSignalLevel value, FrameSignalLevel threshold) noexcept {
    return static_cast<unsigned int>(value) >=
           static_cast<unsigned int>(threshold);
}

FrameSignalLevel correction_level(
    FrameSignalLevel live, FrameSignalLevel sustained,
    FrameSignalLevel tail, bool emergency_enabled,
    bool catastrophic_live_drop) noexcept {
    if (emergency_enabled &&
        (catastrophic_live_drop ||
         (at_least(live, FrameSignalLevel::emergency) &&
          (at_least(sustained, FrameSignalLevel::warning) ||
           at_least(tail, FrameSignalLevel::warning))))) {
        return FrameSignalLevel::emergency;
    }
    if ((at_least(live, FrameSignalLevel::corrective) &&
         (at_least(sustained, FrameSignalLevel::corrective) ||
          at_least(tail, FrameSignalLevel::corrective))) ||
        (at_least(tail, FrameSignalLevel::corrective) &&
         (at_least(live, FrameSignalLevel::warning) ||
          at_least(sustained, FrameSignalLevel::warning)))) {
        return FrameSignalLevel::corrective;
    }
    return FrameSignalLevel::healthy;
}

double ratio(const std::optional<double>& used,
             const std::optional<double>& budget) noexcept {
    if (!finite_nonnegative(used) || !finite_positive(budget)) return 0.0;
    return clamp01(*used / *budget);
}

void add_signal(std::array<std::string_view, 8>& signals,
                std::size_t& count, std::string_view value) noexcept {
    if (count < signals.size()) signals[count++] = value;
}

AdaptiveControllerState state_for(AdaptivePressure pressure) noexcept {
    switch (pressure) {
        case AdaptivePressure::observing: return AdaptiveControllerState::observing;
        case AdaptivePressure::healthy: return AdaptiveControllerState::stable;
        case AdaptivePressure::warning: return AdaptiveControllerState::warning;
        case AdaptivePressure::intervention:
            return AdaptiveControllerState::intervention;
        case AdaptivePressure::emergency: return AdaptiveControllerState::emergency;
    }
    return AdaptiveControllerState::observing;
}

AdaptiveStabilityState stability_state_for(
    AdaptivePressure pressure) noexcept {
    switch (pressure) {
        case AdaptivePressure::observing: return AdaptiveStabilityState::hold;
        case AdaptivePressure::healthy: return AdaptiveStabilityState::stable;
        case AdaptivePressure::warning: return AdaptiveStabilityState::watch;
        case AdaptivePressure::intervention:
        case AdaptivePressure::emergency:
            return AdaptiveStabilityState::correcting;
    }
    return AdaptiveStabilityState::hold;
}

Profile profile_for(AdaptivePressure pressure,
                    int quality_change_budget) noexcept {
    switch (pressure) {
        case AdaptivePressure::healthy: return Profile::stability;
        case AdaptivePressure::warning:
        case AdaptivePressure::intervention:
            return quality_change_budget >= 2
                ? Profile::high_performance : Profile::balanced;
        case AdaptivePressure::emergency: return Profile::high_performance;
        case AdaptivePressure::observing: return Profile::balanced;
    }
    return Profile::balanced;
}

std::string_view candidate_setting(AdaptiveBottleneck bottleneck) noexcept {
    switch (bottleneck) {
        case AdaptiveBottleneck::gpu: return "FarParticleLOD";
        case AdaptiveBottleneck::vram: return "TextureMemoryBudget";
        case AdaptiveBottleneck::ram:
        case AdaptiveBottleneck::paging: return "ParticleMemoryBudget";
        case AdaptiveBottleneck::cpu: return "FarAnimationUpdateRate";
        case AdaptiveBottleneck::rendering: return "ShadowQuality";
        case AdaptiveBottleneck::animation: return "FarAnimationLOD";
        case AdaptiveBottleneck::physics: return "FarCosmeticPhysics";
        case AdaptiveBottleneck::ragdoll: return "MaxActiveRagdolls";
        case AdaptiveBottleneck::particles: return "GlobalParticleQuality";
        case AdaptiveBottleneck::gore: return "GoreQuality";
        case AdaptiveBottleneck::flex: return "FlexAdaptiveSubsteps";
        case AdaptiveBottleneck::streaming:
        case AdaptiveBottleneck::io_pressure:
            return "EnemyTextureStreamingPriority";
        case AdaptiveBottleneck::thermal_power:
        case AdaptiveBottleneck::mixed:
        case AdaptiveBottleneck::unknown: return {};
    }
    return {};
}

AdaptiveCapabilityState candidate_capability(
    AdaptiveBottleneck bottleneck,
    const AdaptiveCapabilities& capabilities) noexcept {
    switch (bottleneck) {
        case AdaptiveBottleneck::physics:
        case AdaptiveBottleneck::ragdoll:
            return capabilities.corpse_control;
        case AdaptiveBottleneck::particles:
            if (capabilities.flex_particle_budget_control ==
                AdaptiveCapabilityState::available) {
                return capabilities.flex_particle_budget_control;
            }
            return capabilities.particle_control;
        case AdaptiveBottleneck::gore:
            return capabilities.gore_control;
        case AdaptiveBottleneck::flex:
            if (capabilities.flex_particle_budget_control ==
                AdaptiveCapabilityState::available) {
                return capabilities.flex_particle_budget_control;
            }
            return capabilities.flex_solver_substep_control;
        case AdaptiveBottleneck::cpu:
        case AdaptiveBottleneck::gpu:
        case AdaptiveBottleneck::vram:
        case AdaptiveBottleneck::ram:
        case AdaptiveBottleneck::paging:
        case AdaptiveBottleneck::rendering:
        case AdaptiveBottleneck::animation:
        case AdaptiveBottleneck::streaming:
        case AdaptiveBottleneck::io_pressure:
            return AdaptiveCapabilityState::restart_required;
        case AdaptiveBottleneck::thermal_power:
        case AdaptiveBottleneck::mixed:
        case AdaptiveBottleneck::unknown:
            return AdaptiveCapabilityState::unavailable;
    }
    return AdaptiveCapabilityState::unavailable;
}

bool manual_lock_blocks(std::string_view setting,
                        std::span<const AdaptiveManualLock> locks) noexcept {
    const auto lock = std::ranges::find(locks, setting,
                                       &AdaptiveManualLock::setting);
    return lock != locks.end() && lock->state != ManualLockState::automatic;
}

AdaptiveCpuReport analyze_cpu_parallelism(
    const AdaptiveSample& sample) noexcept {
    AdaptiveCpuReport report;
    report.effective_core_usage = sample.effective_core_usage;
    report.critical_thread_percent = sample.critical_core_percent;
    report.dominant_thread_share_percent =
        sample.dominant_thread_share_percent;
    report.active_threads = sample.active_cpu_threads;
    report.affinity_logical_processors =
        sample.affinity_logical_processors;
    report.affinity_physical_cores = sample.affinity_physical_cores;
    report.affinity_limited = sample.affinity_logical_processors &&
        sample.system_logical_processors &&
        *sample.affinity_logical_processors <
            *sample.system_logical_processors;

    if (!sample.effective_core_usage && !sample.critical_core_percent &&
        !sample.active_cpu_threads) {
        return report;
    }
    const double effective = sample.effective_core_usage.value_or(0.0);
    const double critical = sample.critical_core_percent.value_or(0.0);
    const double dominant_share =
        sample.dominant_thread_share_percent.value_or(0.0);
    const double logical_capacity = static_cast<double>(
        sample.affinity_logical_processors.value_or(0));
    const double capacity_ratio = logical_capacity > 0.0
        ? effective / logical_capacity : 0.0;
    const double main_thread_parallelism_ceiling = logical_capacity > 0.0
        ? std::max(4.0, logical_capacity * 0.50) : 4.0;

    if (critical >= 70.0 && dominant_share >= 35.0 &&
        effective <= main_thread_parallelism_ceiling) {
        report.workload = AdaptiveCpuWorkload::main_thread_dominant;
    } else if (effective >= 2.0 && capacity_ratio >= 0.65) {
        report.workload = AdaptiveCpuWorkload::broadly_parallel;
    } else if (effective >= 1.0 ||
               sample.active_cpu_threads.value_or(0) >= 3) {
        report.workload = AdaptiveCpuWorkload::partially_parallel;
    } else {
        report.workload = AdaptiveCpuWorkload::idle_or_frame_limited;
    }
    return report;
}

AdaptiveBottleneckReport classify_bottleneck(
    const AdaptiveSample& sample, const AdaptiveDataQualityReport& data,
    const AdaptiveCpuReport& cpu, AdaptivePressure pressure,
    const ResourcePressureSnapshot& resources) noexcept {
    AdaptiveBottleneckReport report;
    report.data_quality = data.quality;
    report.sample_age_ns = data.sample_age_ns;
    if (data.quality == AdaptiveDataQuality::not_available ||
        pressure == AdaptivePressure::observing ||
        pressure == AdaptivePressure::healthy) {
        return report;
    }

    add_signal(report.supporting_signals, report.supporting_count,
               "sustained_frame_time_pressure");
    const double vram_ratio = ratio(sample.vram_used_bytes,
                                    sample.vram_budget_bytes);
    const bool current_gpu_attribution =
        resources.shared_gpu_pressure || !sample.process_gpu_percent ||
        sample.process_gpu_percent.value_or(0.0) >= 89.0;
    const bool gpu_high = resources.gpu.smoothed >= 0.70 &&
        resources.primary == ResourceKind::gpu && current_gpu_attribution;
    const bool process_cpu_high = sample.cpu_percent &&
                                  *sample.cpu_percent >= 85.0;
    const bool critical_thread_high = sample.critical_core_percent &&
                                      *sample.critical_core_percent >= 90.0;
    const bool main_thread_bound =
        cpu.workload == AdaptiveCpuWorkload::main_thread_dominant &&
        sample.critical_core_percent.value_or(0.0) >= 70.0 &&
        sample.dominant_thread_share_percent.value_or(0.0) >= 35.0;
    const double affinity_capacity = static_cast<double>(
        sample.affinity_logical_processors.value_or(0));
    const bool parallel_cpu_high = affinity_capacity > 0.0 &&
        sample.effective_core_usage.value_or(0.0) >=
            std::max(2.0, affinity_capacity * 0.70);
    const bool shared_cpu_high = resources.primary == ResourceKind::cpu &&
        resources.shared_cpu_pressure &&
        sample.system_cpu_percent.value_or(0.0) >= 85.0;
    const bool cpu_high = process_cpu_high || critical_thread_high ||
                          main_thread_bound || parallel_cpu_high ||
                          shared_cpu_high;

    if (sample.thermal_power_pressure && *sample.thermal_power_pressure >= 0.80) {
        report.type = AdaptiveBottleneck::thermal_power;
        report.confidence = 0.70;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_thermal_or_power_pressure");
    } else if (resources.primary == ResourceKind::vram &&
               resources.vram.smoothed >= 0.70) {
        report.type = AdaptiveBottleneck::vram;
        report.confidence = std::max(0.70, resources.vram.confidence);
        add_signal(report.supporting_signals, report.supporting_count,
                   "sustained_vram_reserve_pressure");
    } else if (vram_ratio >= 0.95 &&
               (sample.streaming_pressure.value_or(0.0) >= 0.50 ||
                sample.copy_engine_percent.value_or(0.0) >= 70.0)) {
        report.type = AdaptiveBottleneck::vram;
        report.confidence = 0.78;
        add_signal(report.supporting_signals, report.supporting_count,
                   "vram_budget_and_transfer_pressure");
    } else if (resources.primary == ResourceKind::ram &&
               resources.ram.smoothed >= 0.70 &&
               sample.paging_pressure.value_or(0.0) >= 0.35) {
        report.type = AdaptiveBottleneck::paging;
        report.confidence = 0.78;
        add_signal(report.supporting_signals, report.supporting_count,
                   "ram_and_paging_pressure");
    } else if (resources.primary == ResourceKind::ram &&
               resources.ram.smoothed >= 0.70) {
        report.type = AdaptiveBottleneck::ram;
        report.confidence = std::max(0.64, resources.ram.confidence);
        add_signal(report.supporting_signals, report.supporting_count,
                   "physical_ram_budget_pressure");
    } else if (gpu_high && cpu_high) {
        report.type = AdaptiveBottleneck::mixed;
        report.confidence = 0.62;
        add_signal(report.supporting_signals, report.supporting_count,
                   "simultaneous_cpu_gpu_pressure");
    } else if (gpu_high && !cpu_high) {
        report.type = AdaptiveBottleneck::gpu;
        report.confidence = resources.shared_gpu_pressure ? 0.72 : 0.68;
        add_signal(report.supporting_signals, report.supporting_count,
                   resources.shared_gpu_pressure
                       ? "shared_gpu_pressure_with_frame_impact"
                       : "gpu_pressure_with_cpu_reserve");
        if (sample.graphics_engine_percent) {
            add_signal(report.supporting_signals, report.supporting_count,
                       "graphics_engine_counter");
        }
    } else if (cpu_high && sample.gpu_percent.value_or(100.0) < 85.0) {
        report.type = AdaptiveBottleneck::cpu;
        // Total KF2 CPU is diluted by logical-processor count. A measured
        // saturated process thread plus GPU reserve is direct evidence of an
        // engine/main-thread limit; total CPU alone remains lower confidence.
        report.confidence = main_thread_bound ? 0.88
            : parallel_cpu_high ? 0.84
            : critical_thread_high ? 0.82
            : shared_cpu_high ? 0.72 : 0.48;
        add_signal(report.supporting_signals, report.supporting_count,
                   main_thread_bound
                       ? "main_thread_dominant_parallelism_evidence"
                       : parallel_cpu_high
                           ? "broad_parallel_cpu_capacity_pressure"
                       : critical_thread_high
                       ? "critical_thread_pressure_with_gpu_reserve"
                       : shared_cpu_high
                       ? "shared_cpu_pressure_with_frame_impact"
                       : "process_cpu_pressure_with_gpu_reserve");
        if (sample.effective_core_usage) {
            add_signal(report.supporting_signals, report.supporting_count,
                       "effective_core_usage_measured");
        }
        if (sample.active_cpu_threads) {
            add_signal(report.supporting_signals, report.supporting_count,
                       "active_cpu_threads_measured");
        }
        if (cpu.affinity_limited) {
            add_signal(report.supporting_signals, report.supporting_count,
                       "process_affinity_subset_observed");
        }
        if (process_cpu_high && critical_thread_high) {
            add_signal(report.supporting_signals, report.supporting_count,
                       "process_cpu_signal_corrobates_critical_thread");
        } else if (!sample.critical_core_percent) {
            add_signal(report.contradicting_signals,
                       report.contradicting_count,
                       "critical_core_signal_unavailable");
        } else if (!critical_thread_high) {
            add_signal(report.contradicting_signals,
                       report.contradicting_count,
                       "critical_core_signal_does_not_corrobate");
        }
    } else if (sample.io_pressure.value_or(0.0) >= 0.70 &&
               sample.cpu_percent.value_or(100.0) < 75.0 &&
               sample.gpu_percent.value_or(100.0) < 75.0) {
        report.type = AdaptiveBottleneck::io_pressure;
        report.confidence = 0.65;
        add_signal(report.supporting_signals, report.supporting_count,
                   "io_pressure_with_cpu_gpu_reserve");
    } else if (sample.streaming_pressure.value_or(0.0) >= 0.70) {
        report.type = AdaptiveBottleneck::streaming;
        report.confidence = 0.64;
        add_signal(report.supporting_signals, report.supporting_count,
                   "streaming_pressure");
    } else if (sample.gameplay_context_fresh &&
               sample.flex_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::flex;
        report.confidence = 0.66;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_flex_pressure_context");
    } else if (sample.gameplay_context_fresh &&
               sample.particle_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::particles;
        report.confidence = 0.64;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_particle_pressure_context");
    } else if (sample.gameplay_context_fresh &&
               sample.ragdoll_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::ragdoll;
        report.confidence = 0.64;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_ragdoll_pressure_context");
    } else if (sample.gameplay_context_fresh &&
               sample.physics_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::physics;
        report.confidence = 0.62;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_physics_pressure_context");
    } else if (sample.gameplay_context_fresh &&
               sample.animation_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::animation;
        report.confidence = 0.60;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_animation_pressure_context");
    } else if (sample.gameplay_context_fresh &&
               sample.gore_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::gore;
        report.confidence = 0.60;
        add_signal(report.supporting_signals, report.supporting_count,
                   "verified_gore_pressure_context");
    } else if (sample.rendering_pressure.value_or(0.0) >= 0.80) {
        report.type = AdaptiveBottleneck::rendering;
        report.confidence = 0.58;
        add_signal(report.supporting_signals, report.supporting_count,
                   "rendering_pressure_context");
    } else {
        report.type = AdaptiveBottleneck::unknown;
        report.confidence = 0.25;
        add_signal(report.contradicting_signals,
                   report.contradicting_count,
                   "no_single_cause_proven");
    }
    report.confidence = clamp01(report.confidence * data.confidence_factor);
    return report;
}

}  // namespace

AdaptiveDataQualityReport validate_adaptive_sample(
    const AdaptivePolicy& policy, const AdaptiveSample& sample,
    std::uint64_t now_ns) noexcept {
    AdaptiveDataQualityReport report;
    if (!valid_target_fps(policy.target_fps) ||
        policy.minimum_quality < 10 || policy.maximum_quality > 100 ||
        policy.minimum_quality > policy.maximum_quality ||
        policy.quality_change_budget < 1 || policy.quality_change_budget > 5 ||
        !std::isfinite(policy.performance_headroom) ||
        policy.performance_headroom < 0.0 || policy.performance_headroom > 0.50) {
        report.reason = "invalid_controller_policy";
        return report;
    }
    if (sample.pid == 0 || sample.process_start_id == 0 ||
        sample.timestamp_ns == 0 || now_ns < sample.timestamp_ns) {
        report.reason = "invalid_source_identity_or_time";
        return report;
    }
    report.sample_age_ns = now_ns - sample.timestamp_ns;
    if (report.sample_age_ns > policy.freshness_limit_ns) {
        report.reason = "stale_telemetry";
        return report;
    }
    if (sample.discontinuity || sample.session_changed || sample.map_changed) {
        report.quality = AdaptiveDataQuality::degraded;
        report.confidence_factor = 0.35;
        report.reason = "telemetry_boundary_requires_stabilization";
        return report;
    }
    if (!finite_positive(sample.fps) || !finite_positive(sample.frame_time_ms) ||
        *sample.fps > 1000.0 || *sample.frame_time_ms > 1000.0 ||
        !valid_percent(sample.cpu_percent) ||
        !valid_percent(sample.system_cpu_percent) ||
        !valid_percent(sample.critical_core_percent) ||
        !valid_core_usage(sample.effective_core_usage) ||
        !valid_percent(sample.dominant_thread_share_percent) ||
        !valid_percent(sample.process_gpu_percent) ||
        !valid_percent(sample.gpu_percent) ||
        !valid_percent(sample.graphics_engine_percent) ||
        !valid_percent(sample.compute_engine_percent) ||
        !valid_percent(sample.copy_engine_percent) ||
        !valid_pressure(sample.present_queue_pressure) ||
        !valid_pressure(sample.paging_pressure) ||
        !valid_pressure(sample.io_pressure) ||
        !valid_pressure(sample.thermal_power_pressure) ||
        !valid_pressure(sample.rendering_pressure) ||
        !valid_pressure(sample.animation_pressure) ||
        !valid_pressure(sample.physics_pressure) ||
        !valid_pressure(sample.ragdoll_pressure) ||
        !valid_pressure(sample.particle_pressure) ||
        !valid_pressure(sample.gore_pressure) ||
        !valid_pressure(sample.flex_pressure) ||
        !valid_pressure(sample.streaming_pressure)) {
        report.reason = "invalid_or_missing_primary_telemetry";
        return report;
    }
    if ((sample.active_cpu_threads && *sample.active_cpu_threads > 1'000'000) ||
        (sample.affinity_logical_processors &&
         *sample.affinity_logical_processors == 0) ||
        (sample.affinity_physical_cores &&
         *sample.affinity_physical_cores == 0) ||
        (sample.system_logical_processors &&
         *sample.system_logical_processors == 0) ||
        (sample.affinity_physical_cores &&
         sample.affinity_logical_processors &&
         *sample.affinity_physical_cores >
             *sample.affinity_logical_processors) ||
        (sample.affinity_logical_processors &&
         sample.system_logical_processors &&
         *sample.affinity_logical_processors >
             *sample.system_logical_processors) ||
        (sample.effective_core_usage &&
         sample.affinity_logical_processors &&
         *sample.effective_core_usage >
             static_cast<double>(*sample.affinity_logical_processors) + 0.5)) {
        report.reason = "invalid_cpu_parallelism_telemetry";
        return report;
    }
    if ((sample.vram_used_bytes && !finite_nonnegative(sample.vram_used_bytes)) ||
        (sample.vram_budget_bytes && !finite_positive(sample.vram_budget_bytes)) ||
        (sample.ram_used_bytes && !finite_nonnegative(sample.ram_used_bytes)) ||
        (sample.ram_budget_bytes && !finite_positive(sample.ram_budget_bytes)) ||
        (sample.commit_used_bytes &&
         !finite_nonnegative(sample.commit_used_bytes)) ||
        (sample.commit_budget_bytes &&
         !finite_positive(sample.commit_budget_bytes)) ||
        (sample.process_private_bytes &&
         !finite_nonnegative(sample.process_private_bytes))) {
        report.reason = "invalid_memory_telemetry";
        return report;
    }
    if ((sample.average_fps && !finite_positive(sample.average_fps)) ||
        (sample.median_frame_time_ms &&
         !finite_positive(sample.median_frame_time_ms)) ||
        (sample.p95_frame_time_ms &&
         !finite_positive(sample.p95_frame_time_ms)) ||
        (sample.p99_frame_time_ms &&
         !finite_positive(sample.p99_frame_time_ms)) ||
        (sample.sustained_one_percent_low_fps &&
         !finite_positive(sample.sustained_one_percent_low_fps)) ||
        (sample.one_percent_low_fps &&
         !finite_positive(sample.one_percent_low_fps)) ||
        (sample.point_one_percent_low_fps &&
         !finite_positive(sample.point_one_percent_low_fps)) ||
        (sample.frame_time_variance &&
         !finite_nonnegative(sample.frame_time_variance))) {
        report.reason = "invalid_secondary_telemetry";
        return report;
    }
    if ((sample.quality_score &&
         (!std::isfinite(*sample.quality_score) ||
          *sample.quality_score < 0.0 || *sample.quality_score > 100.0)) ||
        (sample.minimum_quality_reached && sample.quality_score &&
         *sample.quality_score >
             static_cast<double>(policy.minimum_quality) + 0.5)) {
        report.reason = "invalid_or_contradictory_quality_state";
        return report;
    }

    const bool adapter_bound_signals = sample.process_gpu_percent ||
        sample.gpu_percent ||
        sample.graphics_engine_percent || sample.compute_engine_percent ||
        sample.copy_engine_percent || sample.vram_used_bytes ||
        sample.vram_budget_bytes;
    bool degraded = sample.sample_loss || sample.duplicate_sample ||
                    !finite_positive(sample.p95_frame_time_ms) ||
                    (adapter_bound_signals && !sample.adapter_luid);
    if (finite_positive(sample.p95_frame_time_ms) &&
        finite_positive(sample.p99_frame_time_ms) &&
        *sample.p99_frame_time_ms < *sample.p95_frame_time_ms) {
        degraded = true;
    }
    const double implied_frame_time = 1000.0 / *sample.fps;
    if (std::abs(implied_frame_time - *sample.frame_time_ms) >
        std::max(2.0, implied_frame_time * 0.30)) {
        degraded = true;
    }
    report.quality = degraded ? AdaptiveDataQuality::degraded
                              : AdaptiveDataQuality::valid;
    report.confidence_factor = degraded ? 0.55 : 1.0;
    report.reason = degraded ? "usable_but_degraded_or_incompletely_bound_telemetry"
                             : "validated_fresh_telemetry";
    return report;
}

AdaptiveDecision AdaptiveGovernor::evaluate(
    const AdaptivePolicy& policy, const AdaptiveSample& sample,
    std::uint64_t now_ns,
    std::span<const AdaptiveManualLock> manual_locks) noexcept {
    AdaptiveDecision decision;
    decision.data = validate_adaptive_sample(policy, sample, now_ns);
    decision.cpu = analyze_cpu_parallelism(sample);
    decision.restore_generation = restore_generation_;
    const auto stability_bands = adaptive_stability_bands(policy.target_fps);
    decision.target_frame_time_ms = stability_bands.target_frame_time_ms;
    decision.warning_frame_time_ms = stability_bands.warning_frame_time_ms;
    decision.corrective_frame_time_ms =
        stability_bands.corrective_frame_time_ms;
    decision.critical_frame_time_ms = stability_bands.critical_frame_time_ms;

    bool target_changed = false;
    if (valid_target_fps(policy.target_fps)) {
        if (target_fps_ == 0) {
            target_fps_ = policy.target_fps;
            settings_generation_ = 1;
        } else if (target_fps_ != policy.target_fps) {
            target_fps_ = policy.target_fps;
            ++settings_generation_;
            target_changed = true;
        }
    }
    decision.settings_generation = settings_generation_;

    if (frozen_ || (last_evaluation_ns_ != 0 && now_ns < last_evaluation_ns_)) {
        frozen_ = true;
        decision.state = AdaptiveControllerState::frozen;
        decision.disposition = AdaptiveDisposition::blocked;
        decision.reason = "watchdog_non_monotonic_or_frozen";
        decision.watchdog_frozen = true;
        return decision;
    }
    last_evaluation_ns_ = now_ns;

    if (decision.data.quality == AdaptiveDataQuality::not_available) {
        low_percentile_pressure_since_ns_ = 0;
        decision.state = AdaptiveControllerState::observing;
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = decision.data.reason;
        return decision;
    }

    const bool boundary = target_changed || sample.discontinuity ||
        sample.session_changed ||
        sample.map_changed ||
        sample.process_start_id != identity_start_id_ ||
        (session_generation_ != 0 &&
         sample.session_generation != session_generation_) ||
        (map_generation_ != 0 && sample.map_generation != map_generation_);
    const bool telemetry_transition = sample.discontinuity ||
        sample.session_changed || sample.map_changed;
    if (boundary) {
        history_size_ = 0;
        history_next_ = 0;
        smoothed_frame_time_ms_.reset();
        smoothed_p95_ms_.reset();
        resource_pressure_estimator_.reset();
        active_pressure_ = AdaptivePressure::observing;
        candidate_pressure_ = AdaptivePressure::observing;
        candidate_since_ns_ = now_ns;
        low_percentile_pressure_since_ns_ = 0;
        held_bottleneck_ = AdaptiveBottleneck::unknown;
        bottleneck_hold_until_ns_ = 0;
        direction_changes_ = 0;
        ++restore_generation_;
        if (telemetry_transition) {
            constexpr std::uint64_t kStabilizationNs = 3'000'000'000ULL;
            stabilization_until_ns_ = now_ns >
                    std::numeric_limits<std::uint64_t>::max() -
                        kStabilizationNs
                ? std::numeric_limits<std::uint64_t>::max()
                : now_ns + kStabilizationNs;
        }
    }
    identity_start_id_ = sample.process_start_id;
    session_generation_ = sample.session_generation;
    map_generation_ = sample.map_generation;
    decision.restore_generation = restore_generation_;
    decision.settings_generation = settings_generation_;

    if (target_changed) {
        decision.state = AdaptiveControllerState::observing;
        decision.stability_state = AdaptiveStabilityState::hold;
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = "target_changed_stabilization_hold";
        return decision;
    }
    if (stabilization_until_ns_ != 0) {
        if (now_ns < stabilization_until_ns_) {
            decision.state = AdaptiveControllerState::observing;
            decision.stability_state = AdaptiveStabilityState::hold;
            decision.disposition = AdaptiveDisposition::hold;
            decision.reason = "telemetry_transition_stabilization_hold";
            return decision;
        }
        stabilization_until_ns_ = 0;
    }
    if (decision.data.quality == AdaptiveDataQuality::degraded && boundary) {
        decision.state = AdaptiveControllerState::observing;
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = "boundary_stabilization_hold";
        return decision;
    }

    const double target_frame_time = stability_bands.target_frame_time_ms;
    const double alpha = policy.aggressiveness == AdaptiveAggressiveness::conservative
        ? 0.12 : policy.aggressiveness == AdaptiveAggressiveness::aggressive
            ? 0.30 : 0.20;
    const auto smooth = [alpha](std::optional<double>& state,
                                double value) noexcept {
        state = state ? *state + alpha * (value - *state) : value;
        return *state;
    };
    const double frame_time = smooth(smoothed_frame_time_ms_,
                                     *sample.frame_time_ms);
    const double p95 = smooth(smoothed_p95_ms_,
        sample.p95_frame_time_ms.value_or(*sample.frame_time_ms));

    history_[history_next_] = {sample.timestamp_ns, frame_time, p95};
    history_next_ = (history_next_ + 1) % history_.size();
    history_size_ = std::min(history_size_ + 1, history_.size());

    double predicted = frame_time;
    double prediction_confidence = 0.0;
    if (history_size_ >= 4) {
        const std::size_t oldest = history_size_ < history_.size()
            ? 0 : history_next_;
        const std::size_t newest = history_next_ == 0
            ? history_.size() - 1 : history_next_ - 1;
        const auto& first = history_[oldest];
        const auto& last = history_[newest];
        if (last.timestamp_ns > first.timestamp_ns) {
            const double seconds = static_cast<double>(
                last.timestamp_ns - first.timestamp_ns) / 1'000'000'000.0;
            const double slope = (last.frame_time_ms - first.frame_time_ms) /
                                 std::max(0.001, seconds);
            const double horizon = policy.aggressiveness ==
                                           AdaptiveAggressiveness::aggressive
                ? 1.2 : policy.aggressiveness ==
                             AdaptiveAggressiveness::conservative
                    ? 0.5 : 0.8;
            predicted = std::max(0.1, frame_time + slope * horizon);
            prediction_confidence = std::min(
                decision.data.confidence_factor,
                std::min(1.0, static_cast<double>(history_size_) / 24.0));
        }
    }
    decision.predicted_frame_time_ms = predicted;
    decision.prediction_confidence = prediction_confidence;
    decision.drop_risk = clamp01(
        (predicted / target_frame_time - 0.98) / 0.30) *
        prediction_confidence;
    decision.resources = resource_pressure_estimator_.evaluate({
        .timestamp_ns = sample.timestamp_ns,
        .target_fps = policy.target_fps,
        .frame_time_ms = frame_time,
        .p95_frame_time_ms = p95,
        .process_cpu_percent = sample.cpu_percent,
        .system_cpu_percent = sample.system_cpu_percent,
        .critical_thread_percent = sample.critical_core_percent,
        .effective_core_usage = sample.effective_core_usage,
        .affinity_logical_processors =
            sample.affinity_logical_processors,
        .process_gpu_percent = sample.process_gpu_percent,
        .adapter_gpu_percent = sample.gpu_percent,
        .vram_used_bytes = sample.vram_used_bytes,
        .vram_budget_bytes = sample.vram_budget_bytes,
        .ram_used_bytes = sample.ram_used_bytes,
        .ram_budget_bytes = sample.ram_budget_bytes,
        .commit_used_bytes = sample.commit_used_bytes,
        .commit_budget_bytes = sample.commit_budget_bytes,
        .process_private_bytes = sample.process_private_bytes,
        .paging_pressure = sample.paging_pressure,
    });
    decision.headroom = decision.resources.headroom;
    const bool attributed_memory_pressure =
        decision.resources.primary_confidence >= 0.55 &&
        ((decision.resources.primary == ResourceKind::vram &&
          decision.resources.vram.smoothed >= 0.75) ||
         (decision.resources.primary == ResourceKind::ram &&
          decision.resources.ram.smoothed >= 0.75));
    const bool critical_memory_pressure =
        attributed_memory_pressure &&
        ((decision.resources.primary == ResourceKind::vram &&
          decision.resources.vram.smoothed >= 0.90) ||
         (decision.resources.primary == ResourceKind::ram &&
          decision.resources.ram.smoothed >= 0.90));
    decision.current_resource_pressure = attributed_memory_pressure;

    const auto live_level = maximum(
        level_for_fps(*sample.fps, stability_bands),
        level_for_frame_time(frame_time, stability_bands));
    const auto average_level = level_for_fps(
        sample.average_fps.value_or(*sample.fps), stability_bands);
    const auto trend_level = prediction_confidence >= 0.50
        ? level_for_frame_time(predicted, stability_bands)
        : FrameSignalLevel::healthy;
    const auto sustained_level = maximum(average_level, trend_level);
    const auto tail_level = maximum(
        level_for_tail(p95, target_frame_time),
        level_for_stutters(sample.stutter_count));
    const auto long_low_level = sample.one_percent_low_fps
        ? level_for_fps(*sample.one_percent_low_fps, stability_bands)
        : FrameSignalLevel::healthy;
    const auto sustained_low_level = sample.sustained_one_percent_low_fps
        ? level_for_fps(
              *sample.sustained_one_percent_low_fps, stability_bands)
        : FrameSignalLevel::healthy;
    const bool long_low_unhealthy =
        long_low_level != FrameSignalLevel::healthy;
    const bool low_percentiles_need_correction =
        decision.data.quality == AdaptiveDataQuality::valid &&
        at_least(sustained_low_level, FrameSignalLevel::corrective) &&
        at_least(long_low_level, FrameSignalLevel::corrective);
    if (low_percentiles_need_correction) {
        if (low_percentile_pressure_since_ns_ == 0) {
            low_percentile_pressure_since_ns_ = now_ns;
        }
    } else {
        low_percentile_pressure_since_ns_ = 0;
    }
    // The short percentile window is three seconds. Requiring the condition
    // to outlive that entire window prevents one bad present from turning the
    // stale ten-second percentile into an adaptive quality reduction.
    constexpr std::uint64_t kLowPercentileConfirmationNs =
        3'500'000'000ULL;
    const bool confirmed_low_percentile_pressure =
        low_percentile_pressure_since_ns_ != 0 &&
        now_ns >= low_percentile_pressure_since_ns_ &&
        now_ns - low_percentile_pressure_since_ns_ >=
            kLowPercentileConfirmationNs;
    const bool catastrophic_live_drop =
        *sample.fps <= static_cast<double>(policy.target_fps) * 0.50 ||
        frame_time >= target_frame_time * 2.0;

    FrameSignalLevel desired_level = correction_level(
        live_level, sustained_level, tail_level,
        policy.emergency_enabled, catastrophic_live_drop);
    if (desired_level == FrameSignalLevel::healthy &&
        (at_least(live_level, FrameSignalLevel::warning) ||
               at_least(sustained_level, FrameSignalLevel::warning) ||
               at_least(tail_level, FrameSignalLevel::warning) ||
               long_low_unhealthy)) {
        desired_level = FrameSignalLevel::warning;
    }
    if (confirmed_low_percentile_pressure &&
        !at_least(desired_level, FrameSignalLevel::corrective)) {
        desired_level = FrameSignalLevel::corrective;
    }
    if (critical_memory_pressure && policy.emergency_enabled) {
        desired_level = FrameSignalLevel::emergency;
    } else if (attributed_memory_pressure &&
               !at_least(desired_level, FrameSignalLevel::corrective)) {
        desired_level = FrameSignalLevel::corrective;
    }

    const AdaptivePressure desired =
        desired_level == FrameSignalLevel::emergency
            ? AdaptivePressure::emergency
            : desired_level == FrameSignalLevel::corrective
                ? AdaptivePressure::intervention
                : desired_level == FrameSignalLevel::warning
                    ? AdaptivePressure::warning
                    : AdaptivePressure::healthy;
    const auto current_live_level = maximum(
        level_for_fps(*sample.fps, stability_bands),
        level_for_frame_time(*sample.frame_time_ms, stability_bands));
    const auto current_sustained_level = level_for_fps(
        sample.average_fps.value_or(*sample.fps), stability_bands);
    const auto current_tail_level = maximum(
        level_for_tail(sample.p95_frame_time_ms.value_or(
                           *sample.frame_time_ms), target_frame_time),
        level_for_stutters(sample.stutter_count));
    decision.current_frame_pressure = at_least(
        correction_level(
            current_live_level, current_sustained_level,
            current_tail_level, policy.emergency_enabled,
            catastrophic_live_drop),
        FrameSignalLevel::corrective);
    decision.current_frame_pressure = decision.current_frame_pressure ||
        confirmed_low_percentile_pressure;

    if (desired != candidate_pressure_) {
        candidate_pressure_ = desired;
        candidate_since_ns_ = now_ns;
    }
    std::uint64_t dwell_ns = desired == AdaptivePressure::healthy
        ? 6'000'000'000ULL
        : desired == AdaptivePressure::warning ? 800'000'000ULL
        : desired == AdaptivePressure::intervention ? 600'000'000ULL
        : catastrophic_live_drop ? 200'000'000ULL : 400'000'000ULL;
    if (desired == AdaptivePressure::healthy) {
        // Threshold bouncing slows release instead of freezing the controller.
        // The score is bounded, so oscillation cannot create hidden debt.
        dwell_ns += static_cast<std::uint64_t>(
            std::min<std::uint32_t>(direction_changes_, 8U)) *
            1'000'000'000ULL;
    }
    if (policy.aggressiveness == AdaptiveAggressiveness::conservative) {
        dwell_ns += dwell_ns / 2;
    } else if (policy.aggressiveness == AdaptiveAggressiveness::aggressive) {
        dwell_ns -= dwell_ns / 4;
    }
    if (desired != active_pressure_ && now_ns >= candidate_since_ns_ &&
        now_ns - candidate_since_ns_ >= dwell_ns) {
        const bool direction_changed =
            (desired == AdaptivePressure::healthy) !=
            (active_pressure_ == AdaptivePressure::healthy);
        if (direction_changed) {
            direction_changes_ = std::min<std::uint32_t>(
                direction_changes_ + 1U, 32U);
            last_direction_change_ns_ = now_ns;
        }
        active_pressure_ = desired;
    }
    decision.pressure = active_pressure_;
    decision.state = state_for(active_pressure_);
    decision.stability_state = stability_state_for(active_pressure_);
    decision.recommended_profile = profile_for(
        active_pressure_, policy.quality_change_budget);
    decision.quality_score = std::clamp(
        sample.quality_score.value_or(
            static_cast<double>(policy.maximum_quality)),
        static_cast<double>(policy.minimum_quality),
        static_cast<double>(policy.maximum_quality));
    decision.quality_recovery_eligible =
        active_pressure_ == AdaptivePressure::healthy &&
        desired_level == FrameSignalLevel::healthy &&
        !long_low_unhealthy &&
        decision.resources.recovery_safe &&
        decision.headroom >= policy.performance_headroom &&
        decision.quality_score < static_cast<double>(policy.maximum_quality);
    if (decision.quality_recovery_eligible) {
        decision.stability_state = AdaptiveStabilityState::recovering;
    }
    decision.bottleneck = classify_bottleneck(
        sample, decision.data, decision.cpu, active_pressure_,
        decision.resources);
    // A busy KF2 main thread naturally moves above and below a point threshold
    // over adjacent 500 ms samples. Once high-confidence CPU pressure is
    // established, retain that explanation briefly while GPU reserve and a
    // still-busy critical thread corroborate it. A different proven cause
    // always replaces the hold immediately.
    constexpr std::uint64_t kBottleneckHoldNs = 2'000'000'000ULL;
    if (decision.bottleneck.type == AdaptiveBottleneck::cpu &&
        decision.bottleneck.confidence >= 0.70 &&
        decision.data.quality == AdaptiveDataQuality::valid) {
        held_bottleneck_ = AdaptiveBottleneck::cpu;
        bottleneck_hold_until_ns_ = now_ns + kBottleneckHoldNs;
    } else if (decision.bottleneck.type == AdaptiveBottleneck::unknown &&
               held_bottleneck_ == AdaptiveBottleneck::cpu &&
               decision.data.quality == AdaptiveDataQuality::valid &&
               active_pressure_ != AdaptivePressure::observing &&
               active_pressure_ != AdaptivePressure::healthy &&
               now_ns <= bottleneck_hold_until_ns_ &&
               (sample.critical_core_percent.value_or(0.0) >= 55.0 ||
                decision.cpu.workload ==
                    AdaptiveCpuWorkload::main_thread_dominant) &&
               sample.gpu_percent.value_or(100.0) < 85.0) {
        decision.bottleneck.type = AdaptiveBottleneck::cpu;
        decision.bottleneck.confidence = 0.68;
        decision.bottleneck.contradicting_count = 0;
        add_signal(decision.bottleneck.supporting_signals,
                   decision.bottleneck.supporting_count,
                   "critical_thread_hysteresis_hold");
    } else if (decision.bottleneck.type != AdaptiveBottleneck::unknown ||
               now_ns > bottleneck_hold_until_ns_) {
        held_bottleneck_ = AdaptiveBottleneck::unknown;
        bottleneck_hold_until_ns_ = 0;
    }

    if (active_pressure_ == AdaptivePressure::observing) {
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = "adaptive_collecting_stable_baseline";
        return decision;
    }
    if (active_pressure_ == AdaptivePressure::healthy) {
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = policy.quality_recovery_enabled &&
                                  decision.quality_recovery_eligible
            ? "stable_headroom_slow_quality_recovery_eligible"
            : "stable_or_reserve_insufficient_hold";
        return decision;
    }
    if (active_pressure_ == AdaptivePressure::warning) {
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = "early_warning_observe_only";
        return decision;
    }
    if (active_pressure_ == AdaptivePressure::emergency &&
        sample.minimum_quality_reached &&
        sample.capabilities.corpse_control !=
            AdaptiveCapabilityState::available &&
        sample.capabilities.flex_solver_substep_control !=
            AdaptiveCapabilityState::available &&
        sample.capabilities.flex_particle_budget_control !=
            AdaptiveCapabilityState::available) {
        decision.state = AdaptiveControllerState::target_unreachable;
        decision.stability_state = AdaptiveStabilityState::target_unreachable;
        decision.target_unreachable = true;
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = "target_unreachable_at_minimum_safe_quality";
        return decision;
    }

    // The protected UnrealScript provider is the sole owner of the
    // high-frequency corpse loop. The app supplies the user ceiling at launch
    // and observes its transient integer runtime limit; it never writes that
    // effective reduction back to the user's preference.
    if (sample.capabilities.corpse_control ==
            AdaptiveCapabilityState::available &&
        sample.live_corpse_burden && sample.user_max_dead_bodies &&
        sample.adaptive_corpse_runtime_limit &&
        *sample.live_corpse_burden >= 0 &&
        *sample.user_max_dead_bodies >= 4 &&
        *sample.user_max_dead_bodies <= 2000 &&
        *sample.adaptive_corpse_runtime_limit >= 4 &&
        *sample.adaptive_corpse_runtime_limit <=
            *sample.user_max_dead_bodies) {
        decision.selected_setting = "AdaptiveCorpseRuntimeLimit";
        decision.old_value = static_cast<double>(
            *sample.adaptive_corpse_runtime_limit);
        decision.effective_value = decision.old_value;
        if (sample.zed_time_protected) {
            decision.disposition = AdaptiveDisposition::hold;
            decision.stability_state = AdaptiveStabilityState::hold;
            decision.reason = "zed_time_corpse_correction_protected";
            return decision;
        }
        const int live_burden = *sample.live_corpse_burden;
        const int current_limit = *sample.adaptive_corpse_runtime_limit;
        const int base_limit = std::min(current_limit, live_burden);
        const int divisor = active_pressure_ == AdaptivePressure::emergency
            ? 12 : 24;
        const int minimum_step = active_pressure_ == AdaptivePressure::emergency
            ? 4 : 2;
        const int maximum_step = active_pressure_ == AdaptivePressure::emergency
            ? 32 : 16;
        const int step = std::clamp(
            live_burden / divisor, minimum_step, maximum_step);
        const int proposed_limit = std::max(4, base_limit - step);
        if (proposed_limit < current_limit) {
            decision.proposed_value = static_cast<double>(proposed_limit);
            decision.disposition = AdaptiveDisposition::pending;
            decision.stability_state = AdaptiveStabilityState::hold;
            decision.reason =
                "corpse_provider_pending_useful_runtime_reduction";
        } else {
            decision.disposition = AdaptiveDisposition::hold;
            decision.reason = "corpse_runtime_limit_at_technical_minimum";
        }
        return decision;
    }

    decision.selected_setting = candidate_setting(decision.bottleneck.type);
    if (decision.selected_setting.empty()) {
        decision.disposition = AdaptiveDisposition::hold;
        decision.reason = decision.bottleneck.type == AdaptiveBottleneck::unknown
            ? "unknown_bottleneck_observe_only"
            : "no_non_conflicting_safe_candidate";
        return decision;
    }
    // Existing explicit locks are absolute user choices. The policy flag may
    // disable lock editing, but it can never bypass a persisted lock.
    if (manual_lock_blocks(decision.selected_setting, manual_locks)) {
        decision.disposition = AdaptiveDisposition::blocked;
        decision.reason = "per_setting_manual_lock";
        return decision;
    }
    if (decision.data.quality != AdaptiveDataQuality::valid) {
        decision.disposition = AdaptiveDisposition::shadow;
        decision.reason = "degraded_telemetry_shadow_only";
        return decision;
    }

    const auto capability = candidate_capability(
        decision.bottleneck.type, sample.capabilities);
    if (capability == AdaptiveCapabilityState::unavailable ||
        capability == AdaptiveCapabilityState::failed) {
        decision.disposition = capability == AdaptiveCapabilityState::failed
            ? AdaptiveDisposition::failed
            : AdaptiveDisposition::skipped_unavailable;
        decision.reason = capability == AdaptiveCapabilityState::failed
            ? "candidate_capability_failed"
            : "candidate_capability_unavailable";
        return decision;
    }
    if (capability == AdaptiveCapabilityState::restart_required) {
        decision.disposition = AdaptiveDisposition::restart_required;
        decision.reason = "candidate_is_startup_only";
        return decision;
    }
    if (capability == AdaptiveCapabilityState::shadow) {
        decision.disposition = AdaptiveDisposition::shadow;
        decision.reason = "candidate_capability_shadow";
        return decision;
    }
    const auto* setting = find_adaptive_setting(decision.selected_setting);
    if (!setting) {
        decision.disposition = AdaptiveDisposition::blocked;
        decision.reason = "setting_missing_from_target_registry";
        return decision;
    }
    decision.rollback_available = setting->rollback_possible;
    const bool active_ready = adaptive_setting_active_ready(
        *setting, capability);

    if (policy.shadow_mode || !active_ready) {
        decision.disposition = AdaptiveDisposition::shadow;
        decision.reason = !active_ready
            ? "candidate_is_lab_shadow_or_test_required"
            : "global_shadow_mode";
    } else {
        // No production action module consumes this result yet. Reaching this
        // branch means the registry, session gate and rollback contract all
        // explicitly approve the candidate; the caller still owns a separate
        // transactional apply/read-back/measure/commit stage.
        decision.disposition = AdaptiveDisposition::proposed;
        decision.reason = "smallest_safe_transactional_canary_proposed";
    }
    return decision;
}

void AdaptiveGovernor::reset() noexcept {
    history_ = {};
    history_size_ = 0;
    history_next_ = 0;
    quality_debt_ = {};
    quality_debt_size_ = 0;
    smoothed_frame_time_ms_.reset();
    smoothed_p95_ms_.reset();
    resource_pressure_estimator_.reset();
    active_pressure_ = AdaptivePressure::observing;
    candidate_pressure_ = AdaptivePressure::observing;
    candidate_since_ns_ = 0;
    low_percentile_pressure_since_ns_ = 0;
    last_direction_change_ns_ = 0;
    last_evaluation_ns_ = 0;
    identity_start_id_ = 0;
    session_generation_ = 0;
    map_generation_ = 0;
    restore_generation_ = 0;
    settings_generation_ = 0;
    stabilization_until_ns_ = 0;
    target_fps_ = 0;
    held_bottleneck_ = AdaptiveBottleneck::unknown;
    bottleneck_hold_until_ns_ = 0;
    direction_changes_ = 0;
    frozen_ = false;
}

std::size_t AdaptiveGovernor::quality_debt_count() const noexcept {
    return quality_debt_size_;
}

std::wstring_view adaptive_controller_state_name(
    AdaptiveControllerState state) noexcept {
    switch (state) {
        case AdaptiveControllerState::disabled: return L"disabled";
        case AdaptiveControllerState::observing: return L"observing";
        case AdaptiveControllerState::stable: return L"stable";
        case AdaptiveControllerState::warning: return L"warning";
        case AdaptiveControllerState::intervention: return L"intervention";
        case AdaptiveControllerState::emergency: return L"emergency";
        case AdaptiveControllerState::target_unreachable:
            return L"target unreachable";
        case AdaptiveControllerState::frozen: return L"frozen";
    }
    return L"disabled";
}

std::wstring_view adaptive_bottleneck_name(
    AdaptiveBottleneck bottleneck) noexcept {
    switch (bottleneck) {
        case AdaptiveBottleneck::cpu: return L"CPU";
        case AdaptiveBottleneck::gpu: return L"GPU";
        case AdaptiveBottleneck::vram: return L"VRAM";
        case AdaptiveBottleneck::ram: return L"RAM";
        case AdaptiveBottleneck::paging: return L"paging";
        case AdaptiveBottleneck::rendering: return L"rendering";
        case AdaptiveBottleneck::animation: return L"animation";
        case AdaptiveBottleneck::physics: return L"physics";
        case AdaptiveBottleneck::ragdoll: return L"ragdoll";
        case AdaptiveBottleneck::particles: return L"particles";
        case AdaptiveBottleneck::gore: return L"gore";
        case AdaptiveBottleneck::flex: return L"FleX";
        case AdaptiveBottleneck::streaming: return L"streaming";
        case AdaptiveBottleneck::io_pressure: return L"I/O";
        case AdaptiveBottleneck::thermal_power: return L"thermal/power";
        case AdaptiveBottleneck::mixed: return L"mixed";
        case AdaptiveBottleneck::unknown: return L"unknown";
    }
    return L"unknown";
}

std::wstring_view adaptive_disposition_name(
    AdaptiveDisposition disposition) noexcept {
    switch (disposition) {
        case AdaptiveDisposition::none: return L"none";
        case AdaptiveDisposition::hold: return L"hold";
        case AdaptiveDisposition::shadow: return L"shadow";
        case AdaptiveDisposition::proposed: return L"proposed";
        case AdaptiveDisposition::keep: return L"keep";
        case AdaptiveDisposition::rollback: return L"rollback";
        case AdaptiveDisposition::blocked: return L"blocked";
        case AdaptiveDisposition::skipped_unavailable:
            return L"skipped unavailable";
        case AdaptiveDisposition::pending: return L"pending";
        case AdaptiveDisposition::applied: return L"applied";
        case AdaptiveDisposition::failed: return L"failed";
        case AdaptiveDisposition::restart_required:
            return L"restart required";
    }
    return L"none";
}

std::wstring_view adaptive_stability_state_name(
    AdaptiveStabilityState state) noexcept {
    switch (state) {
        case AdaptiveStabilityState::stable: return L"stable";
        case AdaptiveStabilityState::watch: return L"watch";
        case AdaptiveStabilityState::correcting: return L"correcting";
        case AdaptiveStabilityState::hold: return L"hold";
        case AdaptiveStabilityState::recovering: return L"recovering";
        case AdaptiveStabilityState::target_unreachable:
            return L"target unreachable";
    }
    return L"hold";
}

std::string_view adaptive_capability_state_name(
    AdaptiveCapabilityState state) noexcept {
    switch (state) {
        case AdaptiveCapabilityState::available: return "AVAILABLE";
        case AdaptiveCapabilityState::unavailable: return "UNAVAILABLE";
        case AdaptiveCapabilityState::shadow: return "SHADOW";
        case AdaptiveCapabilityState::restart_required:
            return "RESTART_REQUIRED";
        case AdaptiveCapabilityState::failed: return "FAILED";
    }
    return "UNAVAILABLE";
}

std::wstring_view adaptive_cpu_workload_name(
    AdaptiveCpuWorkload workload) noexcept {
    switch (workload) {
        case AdaptiveCpuWorkload::unknown: return L"unknown";
        case AdaptiveCpuWorkload::idle_or_frame_limited:
            return L"idle/frame-limited";
        case AdaptiveCpuWorkload::main_thread_dominant:
            return L"main-thread dominant";
        case AdaptiveCpuWorkload::partially_parallel:
            return L"partially parallel";
        case AdaptiveCpuWorkload::broadly_parallel:
            return L"broadly parallel";
    }
    return L"unknown";
}

}  // namespace kf2::optimizer
