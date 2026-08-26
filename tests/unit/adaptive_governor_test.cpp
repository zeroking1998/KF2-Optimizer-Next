#include <cstdlib>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>

#include "kf2/optimizer/adaptive_governor.hpp"
#include "kf2/optimizer/adaptive_session.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

using namespace kf2::optimizer;

bool near(double left, double right) {
    return std::abs(left - right) < 0.0001;
}

AdaptiveSample sample(std::uint64_t now, double fps, double frame_time,
                      double p95, double cpu, double gpu,
                      AdaptiveSessionClass session =
                          AdaptiveSessionClass::verified_offline) {
    return {
        .pid = 42,
        .process_start_id = 9001,
        .timestamp_ns = now,
        .session_generation = 1,
        .map_generation = 1,
        .adapter_luid = 77,
        .session_class = session,
        .capabilities = {
            .frame_timing = AdaptiveCapabilityState::available,
            .cpu_telemetry = AdaptiveCapabilityState::available,
            .gpu_telemetry = AdaptiveCapabilityState::available,
        },
        .fps = fps,
        .average_fps = fps,
        .frame_time_ms = frame_time,
        .median_frame_time_ms = frame_time,
        .p95_frame_time_ms = p95,
        .p99_frame_time_ms = p95 * 1.1,
        .one_percent_low_fps = fps,
        .point_one_percent_low_fps = fps * 0.8,
        .frame_time_variance = 0.4,
        .cpu_percent = cpu,
        .process_gpu_percent = gpu,
        .gpu_percent = gpu,
        .vram_used_bytes = 2.0 * 1024.0 * 1024.0 * 1024.0,
        .vram_budget_bytes = 8.0 * 1024.0 * 1024.0 * 1024.0,
        .ram_used_bytes = 8.0 * 1024.0 * 1024.0 * 1024.0,
        .ram_budget_bytes = 32.0 * 1024.0 * 1024.0 * 1024.0,
    };
}

AdaptiveDecision drive(AdaptiveGovernor& governor, const AdaptivePolicy& policy,
                       AdaptiveSample current, std::uint64_t first,
                       std::uint64_t duration,
                       std::span<const AdaptiveManualLock> locks = {}) {
    AdaptiveDecision result;
    for (std::uint64_t offset = 0; offset <= duration;
         offset += 200'000'000ULL) {
        current.timestamp_ns = first + offset;
        result = governor.evaluate(policy, current, first + offset, locks);
    }
    return result;
}

}  // namespace

int main() {
    using namespace kf2::optimizer;
    constexpr std::uint64_t start = 10'000'000'000ULL;
    AdaptivePolicy adaptive;

    CHECK(effective_adaptive_target_fps(60, std::nullopt) == 60);
    CHECK(effective_adaptive_target_fps(60, 240) == 60);
    CHECK(effective_adaptive_target_fps(10, 240) == 240);
    CHECK(effective_adaptive_target_fps(60, 241) == 60);
    for (int target = kTargetFpsMinimum;
         target <= kTargetFpsMaximum; ++target) {
        const int stale_session_target =
            target == kTargetFpsMaximum ? kTargetFpsMinimum
                                        : kTargetFpsMaximum;
        CHECK(effective_adaptive_target_fps(
                  target, stale_session_target) == target);
    }
    CHECK(effective_adaptive_corpse_limit(20, 2000) == 2000);
    CHECK(effective_adaptive_corpse_limit(20, 2001) == 20);
    CHECK(effective_adaptive_quality_change_budget(2, 5) == 5);
    CHECK(effective_adaptive_quality_change_budget(2, 0) == 2);

    static_assert(kTargetFpsValueCount == 211);
    double previous_target_ms = 0.0;
    for (int target = kTargetFpsMinimum;
         target <= kTargetFpsMaximum; ++target) {
        const auto bands = adaptive_stability_bands(target);
        CHECK(std::isfinite(bands.target_frame_time_ms));
        CHECK(bands.target_frame_time_ms > 0.0);
        CHECK(bands.target_frame_time_ms < bands.warning_frame_time_ms);
        CHECK(bands.warning_frame_time_ms <
              bands.corrective_frame_time_ms);
        CHECK(bands.corrective_frame_time_ms <
              bands.critical_frame_time_ms);
        if (previous_target_ms > 0.0) {
            CHECK(bands.target_frame_time_ms < previous_target_ms);
        }
        previous_target_ms = bands.target_frame_time_ms;
    }

    struct IntermediateTargetCase {
        int target;
        double warning_fps;
        double corrective_fps;
        double emergency_fps;
    };
    constexpr IntermediateTargetCase intermediate_targets[] = {
        {86, 85.0, 84.0, 83.0},
        {122, 120.9833333333, 119.9666666667, 118.95},
        {211, 209.2416666667, 207.4833333333, 205.725},
        {233, 231.0583333333, 229.1166666667, 227.175},
    };
    for (const auto& expected : intermediate_targets) {
        const auto bands = adaptive_stability_bands(expected.target);
        CHECK(near(bands.warning_frame_time_ms,
                   1000.0 / expected.warning_fps));
        CHECK(near(bands.corrective_frame_time_ms,
                   1000.0 / expected.corrective_fps));
        CHECK(near(bands.critical_frame_time_ms,
                   1000.0 / expected.emergency_fps));
    }

    auto valid = sample(start, 60.0, 1000.0 / 60.0, 17.2, 35.0, 55.0);
    valid.vram_used_bytes = 0.0;
    valid.ram_used_bytes = 0.0;
    CHECK(validate_adaptive_sample(adaptive, valid, start).quality ==
          AdaptiveDataQuality::valid);
    CHECK(validate_adaptive_sample(adaptive, valid, start +
          adaptive.freshness_limit_ns + 1).quality ==
          AdaptiveDataQuality::not_available);
    valid.fps = std::numeric_limits<double>::quiet_NaN();
    CHECK(validate_adaptive_sample(adaptive, valid, start).quality ==
          AdaptiveDataQuality::not_available);

    auto invalid_system_cpu = sample(
        start, 60.0, 1000.0 / 60.0, 17.2, 35.0, 55.0);
    invalid_system_cpu.system_cpu_percent = 101.0;
    CHECK(validate_adaptive_sample(adaptive, invalid_system_cpu, start).quality ==
          AdaptiveDataQuality::not_available);

    AdaptiveGovernor corrective_governor;
    const auto corrective = drive(
        corrective_governor, adaptive,
        sample(start, 58.0, 1000.0 / 58.0, 18.0, 35.0, 60.0),
        start, 1'000'000'000ULL);
    CHECK(corrective.state == AdaptiveControllerState::intervention);
    CHECK(corrective.stability_state == AdaptiveStabilityState::correcting);
    CHECK(corrective.corrective_frame_time_ms ==
          adaptive_stability_bands(60).corrective_frame_time_ms);

    AdaptivePolicy stronger_quality = adaptive;
    stronger_quality.quality_change_budget = 2;
    AdaptiveGovernor stronger_quality_governor;
    const auto stronger_quality_decision = drive(
        stronger_quality_governor, stronger_quality,
        sample(start, 58.0, 1000.0 / 58.0, 18.0, 35.0, 60.0),
        start, 1'000'000'000ULL);
    CHECK(stronger_quality_decision.state ==
          AdaptiveControllerState::intervention);
    CHECK(stronger_quality_decision.recommended_profile ==
          Profile::high_performance);

    AdaptiveGovernor warning_governor;
    const auto warning = drive(
        warning_governor, adaptive,
        sample(start, 59.0, 1000.0 / 59.0, 17.2, 35.0, 60.0),
        start, 1'000'000'000ULL);
    CHECK(warning.state == AdaptiveControllerState::warning);
    CHECK(warning.stability_state == AdaptiveStabilityState::watch);

    // A stale 10-second 1% low may warn and block recovery, but it must not
    // independently force correction after live and tail timing recovered.
    auto low_only_sample = sample(
        start, 60.0, 1000.0 / 60.0, 17.0, 35.0, 60.0);
    low_only_sample.one_percent_low_fps = 30.0;
    AdaptiveGovernor low_warning_governor;
    const auto low_warning = drive(
        low_warning_governor, adaptive, low_only_sample,
        start, 1'000'000'000ULL);
    CHECK(low_warning.state == AdaptiveControllerState::warning);
    CHECK(low_warning.disposition == AdaptiveDisposition::hold);
    CHECK(low_warning.reason == "early_warning_observe_only");

    AdaptivePolicy intermediate_target = adaptive;
    intermediate_target.target_fps = 122;
    auto sustained = sample(
        start, 119.8, 1000.0 / 119.8, 8.5, 35.0, 60.0);
    sustained.average_fps = 119.8;
    sustained.one_percent_low_fps = 122.0;
    AdaptiveGovernor sustained_governor;
    const auto sustained_result = drive(
        sustained_governor, intermediate_target, sustained,
        start, 1'000'000'000ULL);
    CHECK(sustained_result.state == AdaptiveControllerState::intervention);

    auto stale_average = sample(
        start, 122.0, 1000.0 / 122.0, 8.4, 35.0, 60.0);
    stale_average.average_fps = 90.0;
    stale_average.one_percent_low_fps = 122.0;
    AdaptiveGovernor stale_average_governor;
    const auto stale_average_result = drive(
        stale_average_governor, intermediate_target, stale_average,
        start, 1'000'000'000ULL);
    CHECK(stale_average_result.state == AdaptiveControllerState::warning);

    auto stutter_burst = sample(
        start, 59.0, 1000.0 / 59.0, 17.0, 35.0, 60.0);
    stutter_burst.average_fps = 59.0;
    stutter_burst.one_percent_low_fps = 60.0;
    stutter_burst.stutter_count = 3;
    AdaptiveGovernor stutter_governor;
    const auto stutter_result = drive(
        stutter_governor, adaptive, stutter_burst,
        start, 1'000'000'000ULL);
    CHECK(stutter_result.state == AdaptiveControllerState::intervention);

    auto map_transition = sample(
        start, 30.0, 1000.0 / 30.0, 40.0, 35.0, 60.0);
    map_transition.average_fps = 30.0;
    map_transition.map_generation = 2;
    map_transition.map_changed = true;
    AdaptiveGovernor map_governor;
    const auto transition = map_governor.evaluate(
        adaptive, map_transition, start);
    CHECK(transition.state == AdaptiveControllerState::observing);
    map_transition.map_changed = false;
    AdaptiveDecision settling;
    for (std::uint64_t offset = 200'000'000ULL;
         offset <= 2'800'000'000ULL; offset += 200'000'000ULL) {
        map_transition.timestamp_ns = start + offset;
        settling = map_governor.evaluate(
            adaptive, map_transition, start + offset);
    }
    CHECK(settling.state == AdaptiveControllerState::observing);
    CHECK(settling.reason == "telemetry_transition_stabilization_hold");
    const auto after_settling = drive(
        map_governor, adaptive, map_transition,
        start + 3'000'000'000ULL, 1'000'000'000ULL);
    CHECK(after_settling.state == AdaptiveControllerState::emergency);

    AdaptiveGovernor recovery_governor;
    auto recovery_sample =
        sample(start, 60.0, 1000.0 / 60.0, 17.0, 30.0, 45.0);
    recovery_sample.quality_score = 80.0;
    const auto recovery = drive(
        recovery_governor, adaptive, recovery_sample,
        start, 6'200'000'000ULL);
    CHECK(recovery.state == AdaptiveControllerState::stable);
    CHECK(recovery.stability_state == AdaptiveStabilityState::recovering);
    CHECK(recovery.quality_recovery_eligible);
    CHECK(recovery.reason ==
          "stable_headroom_slow_quality_recovery_eligible");

    AdaptiveGovernor gpu_governor;
    const auto gpu = drive(
        gpu_governor, adaptive,
        sample(start, 30.0, 33.33, 42.0, 35.0, 98.0),
        start, 1'000'000'000ULL);
    CHECK(gpu.state == AdaptiveControllerState::emergency);
    CHECK(gpu.bottleneck.type == AdaptiveBottleneck::gpu);
    CHECK(gpu.selected_setting == "FarParticleLOD");
    CHECK(gpu.disposition == AdaptiveDisposition::restart_required);
    // This target-model knob is deliberately still LAB/SHADOW. No mutation is
    // permitted and therefore no fabricated rollback capability is claimed.
    CHECK(!gpu.rollback_available);

    // High whole-adapter load is not attributed to KF2 while its frame
    // budget remains healthy. The same measured load becomes actionable
    // shared GPU pressure only after frame timing also degrades.
    auto stable_external_gpu = sample(
        start, 60.0, 1000.0 / 60.0, 16.9, 18.0, 18.0);
    stable_external_gpu.gpu_percent = 99.0;
    AdaptiveGovernor stable_external_gpu_governor;
    const auto stable_gpu = drive(
        stable_external_gpu_governor, adaptive, stable_external_gpu,
        start, 1'000'000'000ULL);
    CHECK(stable_gpu.state != AdaptiveControllerState::intervention);
    CHECK(stable_gpu.state != AdaptiveControllerState::emergency);
    CHECK(!stable_gpu.resources.shared_gpu_pressure);
    CHECK(stable_gpu.bottleneck.type == AdaptiveBottleneck::unknown);

    // Near-exhausted VRAM is allowed to act before the first visible frame
    // collapse. Unlike generic GPU utilization, memory exhaustion predicts a
    // costly streaming or paging event and has direct budget attribution.
    auto stable_vram_pressure = sample(
        start, 60.0, 1000.0 / 60.0, 16.9, 30.0, 55.0);
    stable_vram_pressure.vram_used_bytes =
        7.5 * 1024.0 * 1024.0 * 1024.0;
    AdaptiveGovernor stable_vram_governor;
    const auto proactive_vram = drive(
        stable_vram_governor, adaptive, stable_vram_pressure,
        start, 1'000'000'000ULL);
    CHECK(proactive_vram.state == AdaptiveControllerState::intervention);
    CHECK(!proactive_vram.current_frame_pressure);
    CHECK(proactive_vram.current_resource_pressure);
    CHECK(proactive_vram.resources.primary == ResourceKind::vram);
    CHECK(proactive_vram.bottleneck.type == AdaptiveBottleneck::vram);

    auto stable_ram_pressure = sample(
        start, 60.0, 1000.0 / 60.0, 16.9, 30.0, 55.0);
    stable_ram_pressure.ram_used_bytes =
        30.0 * 1024.0 * 1024.0 * 1024.0;
    AdaptiveGovernor stable_ram_governor;
    const auto proactive_ram = drive(
        stable_ram_governor, adaptive, stable_ram_pressure,
        start, 1'000'000'000ULL);
    CHECK(proactive_ram.state == AdaptiveControllerState::intervention);
    CHECK(proactive_ram.current_resource_pressure);
    CHECK(proactive_ram.resources.primary == ResourceKind::ram);

    auto impacted_external_gpu = stable_external_gpu;
    impacted_external_gpu.fps = 30.0;
    impacted_external_gpu.average_fps = 30.0;
    impacted_external_gpu.frame_time_ms = 33.33;
    impacted_external_gpu.median_frame_time_ms = 33.33;
    impacted_external_gpu.p95_frame_time_ms = 42.0;
    impacted_external_gpu.p99_frame_time_ms = 46.2;
    impacted_external_gpu.one_percent_low_fps = 28.0;
    AdaptiveGovernor impacted_external_gpu_governor;
    const auto shared_gpu = drive(
        impacted_external_gpu_governor, adaptive, impacted_external_gpu,
        start, 1'000'000'000ULL);
    CHECK(shared_gpu.state == AdaptiveControllerState::emergency);
    CHECK(shared_gpu.current_frame_pressure);
    CHECK(shared_gpu.resources.primary == ResourceKind::gpu);
    CHECK(shared_gpu.resources.shared_gpu_pressure);
    CHECK(shared_gpu.bottleneck.type == AdaptiveBottleneck::gpu);
    CHECK(shared_gpu.bottleneck.confidence == 0.72);
    CHECK(shared_gpu.bottleneck.supporting_count >= 2);
    CHECK(shared_gpu.bottleneck.supporting_signals[1] ==
          "shared_gpu_pressure_with_frame_impact");

    auto recovered_external_gpu = stable_external_gpu;
    AdaptiveDecision recovered_shared_gpu;
    for (std::uint64_t offset = 1'200'000'000ULL;
         offset <= 3'000'000'000ULL; offset += 200'000'000ULL) {
        recovered_external_gpu.timestamp_ns = start + offset;
        recovered_shared_gpu = impacted_external_gpu_governor.evaluate(
            adaptive, recovered_external_gpu,
            recovered_external_gpu.timestamp_ns);
    }
    CHECK(recovered_shared_gpu.state == AdaptiveControllerState::emergency);
    CHECK(!recovered_shared_gpu.current_frame_pressure);

    AdaptiveGovernor unreachable_governor;
    auto minimum = sample(start, 30.0, 33.33, 42.0, 35.0, 98.0);
    minimum.quality_score =
        static_cast<double>(adaptive.minimum_quality);
    minimum.minimum_quality_reached = true;
    const auto unreachable = drive(
        unreachable_governor, adaptive, minimum, start,
        1'000'000'000ULL);
    CHECK(unreachable.state == AdaptiveControllerState::target_unreachable);
    CHECK(unreachable.target_unreachable);
    CHECK(unreachable.reason ==
          "target_unreachable_at_minimum_safe_quality");

    AdaptiveGovernor locked_governor;
    const AdaptiveManualLock lock{
        "FarParticleLOD", ManualLockState::lock_current, std::nullopt};
    const auto locked = drive(
        locked_governor, adaptive,
        sample(start, 30.0, 33.33, 42.0, 35.0, 98.0),
        start, 1'000'000'000ULL, std::span{&lock, 1U});
    CHECK(locked.disposition == AdaptiveDisposition::blocked);
    CHECK(locked.reason == "per_setting_manual_lock");
    AdaptivePolicy locks_ui_disabled = adaptive;
    locks_ui_disabled.manual_locks_enabled = false;
    AdaptiveGovernor absolute_lock_governor;
    const auto absolute_lock = drive(
        absolute_lock_governor, locks_ui_disabled,
        sample(start, 30.0, 33.33, 42.0, 35.0, 98.0),
        start, 1'000'000'000ULL, std::span{&lock, 1U});
    CHECK(absolute_lock.disposition == AdaptiveDisposition::blocked);

    AdaptiveGovernor online_governor;
    AdaptiveGovernor offline_governor;
    const auto online = drive(
        online_governor, adaptive,
        sample(start, 30.0, 33.33, 42.0, 35.0, 98.0,
               AdaptiveSessionClass::unknown),
        start, 1'000'000'000ULL);
    const auto offline = drive(
        offline_governor, adaptive,
        sample(start, 30.0, 33.33, 42.0, 35.0, 98.0,
               AdaptiveSessionClass::verified_offline),
        start, 1'000'000'000ULL);
    CHECK(online.state == offline.state);
    CHECK(online.disposition == offline.disposition);
    CHECK(online.reason == offline.reason);
    CHECK(online.disposition == AdaptiveDisposition::restart_required);

    AdaptiveGovernor cpu_governor;
    auto cpu_sample = sample(start, 45.0, 22.2, 28.0, 96.0, 40.0);
    const auto cpu = drive(cpu_governor, adaptive, cpu_sample, start,
                           2'000'000'000ULL);
    CHECK(cpu.bottleneck.type == AdaptiveBottleneck::cpu);
    CHECK(cpu.bottleneck.confidence <= 0.48);
    CHECK(cpu.bottleneck.contradicting_count > 0);

    // Whole-system CPU saturation follows the same rule: no quality loss
    // while KF2 holds the target, but a classified shared CPU bottleneck once
    // the saturation and frame-budget miss occur together.
    auto stable_external_cpu = sample(
        start, 60.0, 1000.0 / 60.0, 16.9, 18.0, 30.0);
    stable_external_cpu.system_cpu_percent = 98.0;
    AdaptiveGovernor stable_external_cpu_governor;
    const auto stable_cpu = drive(
        stable_external_cpu_governor, adaptive, stable_external_cpu,
        start, 1'000'000'000ULL);
    CHECK(stable_cpu.state != AdaptiveControllerState::intervention);
    CHECK(stable_cpu.state != AdaptiveControllerState::emergency);
    CHECK(!stable_cpu.resources.shared_cpu_pressure);
    CHECK(stable_cpu.bottleneck.type == AdaptiveBottleneck::unknown);

    auto impacted_external_cpu = stable_external_cpu;
    impacted_external_cpu.fps = 30.0;
    impacted_external_cpu.average_fps = 30.0;
    impacted_external_cpu.frame_time_ms = 33.33;
    impacted_external_cpu.median_frame_time_ms = 33.33;
    impacted_external_cpu.p95_frame_time_ms = 42.0;
    impacted_external_cpu.p99_frame_time_ms = 46.2;
    impacted_external_cpu.one_percent_low_fps = 28.0;
    AdaptiveGovernor impacted_external_cpu_governor;
    const auto shared_cpu = drive(
        impacted_external_cpu_governor, adaptive, impacted_external_cpu,
        start, 1'000'000'000ULL);
    CHECK(shared_cpu.state == AdaptiveControllerState::emergency);
    CHECK(shared_cpu.current_frame_pressure);
    CHECK(shared_cpu.resources.primary == ResourceKind::cpu);
    CHECK(shared_cpu.resources.shared_cpu_pressure);
    CHECK(shared_cpu.bottleneck.type == AdaptiveBottleneck::cpu);
    CHECK(shared_cpu.bottleneck.confidence == 0.72);
    CHECK(shared_cpu.bottleneck.supporting_count >= 2);
    CHECK(shared_cpu.bottleneck.supporting_signals[1] ==
          "shared_cpu_pressure_with_frame_impact");

    AdaptiveGovernor critical_thread_governor;
    auto critical_thread_sample =
        sample(start, 30.0, 33.33, 42.0, 5.0, 16.0);
    critical_thread_sample.critical_core_percent = 98.0;
    const auto critical_thread = drive(
        critical_thread_governor, adaptive, critical_thread_sample,
        start, 1'000'000'000ULL);
    CHECK(critical_thread.bottleneck.type == AdaptiveBottleneck::cpu);
    CHECK(critical_thread.bottleneck.confidence >= 0.80);
    CHECK(critical_thread.selected_setting == "FarAnimationUpdateRate");
    CHECK(critical_thread.bottleneck.supporting_count >= 2);
    CHECK(critical_thread.bottleneck.supporting_signals[1] ==
          "critical_thread_pressure_with_gpu_reserve");

    auto held_sample = critical_thread_sample;
    held_sample.critical_core_percent = 70.0;
    held_sample.timestamp_ns = start + 1'400'000'000ULL;
    const auto held = critical_thread_governor.evaluate(
        adaptive, held_sample, held_sample.timestamp_ns);
    CHECK(held.bottleneck.type == AdaptiveBottleneck::cpu);
    CHECK(held.bottleneck.confidence == 0.68);
    CHECK(held.bottleneck.supporting_signals[
              held.bottleneck.supporting_count - 1] ==
          "critical_thread_hysteresis_hold");
    held_sample.timestamp_ns = start + 3'200'000'000ULL;
    const auto released = critical_thread_governor.evaluate(
        adaptive, held_sample, held_sample.timestamp_ns);
    CHECK(released.bottleneck.type == AdaptiveBottleneck::unknown);

    AdaptiveGovernor measured_parallelism_governor;
    auto measured_parallelism =
        sample(start, 30.0, 33.33, 42.0, 5.0, 16.0);
    measured_parallelism.critical_core_percent = 80.0;
    measured_parallelism.effective_core_usage = 1.5;
    measured_parallelism.dominant_thread_share_percent = 54.0;
    measured_parallelism.active_cpu_threads = 7;
    measured_parallelism.affinity_logical_processors = 16;
    measured_parallelism.affinity_physical_cores = 8;
    measured_parallelism.system_logical_processors = 32;
    const auto measured_cpu = drive(
        measured_parallelism_governor, adaptive, measured_parallelism,
        start, 1'000'000'000ULL);
    CHECK(measured_cpu.cpu.workload ==
          AdaptiveCpuWorkload::main_thread_dominant);
    CHECK(measured_cpu.cpu.affinity_limited);
    CHECK(measured_cpu.cpu.effective_core_usage == 1.5);
    CHECK(measured_cpu.bottleneck.type == AdaptiveBottleneck::cpu);
    CHECK(measured_cpu.bottleneck.confidence >= 0.86);
    CHECK(measured_cpu.bottleneck.supporting_signals[1] ==
          "main_thread_dominant_parallelism_evidence");
    CHECK(measured_cpu.selected_setting == "FarAnimationUpdateRate");

    auto invalid_parallelism = measured_parallelism;
    invalid_parallelism.effective_core_usage = 17.0;
    CHECK(validate_adaptive_sample(
              adaptive, invalid_parallelism, start).reason ==
          "invalid_cpu_parallelism_telemetry");

    auto classify = [&](AdaptiveSample input) {
        AdaptiveGovernor governor;
        const auto decision = drive(
            governor, adaptive, input, start, 1'000'000'000ULL);
        return decision.bottleneck.type;
    };

    auto vram = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    vram.vram_used_bytes = 7.8 * 1024.0 * 1024.0 * 1024.0;
    vram.streaming_pressure = 0.8;
    CHECK(classify(vram) == AdaptiveBottleneck::vram);

    auto paging = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    paging.ram_used_bytes = 30.0 * 1024.0 * 1024.0 * 1024.0;
    paging.paging_pressure = 0.5;
    CHECK(classify(paging) == AdaptiveBottleneck::paging);

    auto ram = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    ram.ram_used_bytes = 30.5 * 1024.0 * 1024.0 * 1024.0;
    CHECK(classify(ram) == AdaptiveBottleneck::ram);

    CHECK(classify(sample(start, 30.0, 33.33, 42.0, 90.0, 95.0)) ==
          AdaptiveBottleneck::mixed);

    auto io = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    io.io_pressure = 0.8;
    CHECK(classify(io) == AdaptiveBottleneck::io_pressure);

    auto streaming = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    streaming.streaming_pressure = 0.8;
    CHECK(classify(streaming) == AdaptiveBottleneck::streaming);

    auto thermal = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    thermal.thermal_power_pressure = 0.9;
    CHECK(classify(thermal) == AdaptiveBottleneck::thermal_power);

    auto flex = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    flex.gameplay_context_fresh = true;
    flex.flex_pressure = 0.9;
    CHECK(classify(flex) == AdaptiveBottleneck::flex);

    auto particles = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    particles.gameplay_context_fresh = true;
    particles.particle_pressure = 0.9;
    CHECK(classify(particles) == AdaptiveBottleneck::particles);

    auto ragdoll = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    ragdoll.gameplay_context_fresh = true;
    ragdoll.ragdoll_pressure = 0.9;
    CHECK(classify(ragdoll) == AdaptiveBottleneck::ragdoll);

    auto physics = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    physics.gameplay_context_fresh = true;
    physics.physics_pressure = 0.9;
    CHECK(classify(physics) == AdaptiveBottleneck::physics);

    auto animation = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    animation.gameplay_context_fresh = true;
    animation.animation_pressure = 0.9;
    CHECK(classify(animation) == AdaptiveBottleneck::animation);

    auto gore = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    gore.gameplay_context_fresh = true;
    gore.gore_pressure = 0.9;
    CHECK(classify(gore) == AdaptiveBottleneck::gore);

    auto rendering = sample(start, 30.0, 33.33, 42.0, 35.0, 55.0);
    rendering.rendering_pressure = 0.9;
    CHECK(classify(rendering) == AdaptiveBottleneck::rendering);

    CHECK(classify(sample(start, 30.0, 33.33, 42.0, 35.0, 55.0)) ==
          AdaptiveBottleneck::unknown);

    AdaptiveGovernor corpse_governor;
    auto corpse_pressure = sample(
        start, 58.0, 1000.0 / 58.0, 18.0, 35.0, 60.0);
    corpse_pressure.capabilities.corpse_telemetry =
        AdaptiveCapabilityState::available;
    corpse_pressure.capabilities.corpse_control =
        AdaptiveCapabilityState::available;
    corpse_pressure.capabilities.ragdoll_control =
        AdaptiveCapabilityState::available;
    corpse_pressure.live_corpse_burden = 120;
    corpse_pressure.user_max_dead_bodies = 2000;
    corpse_pressure.adaptive_corpse_runtime_limit = 2000;
    const auto corpse = drive(
        corpse_governor, adaptive, corpse_pressure,
        start, 1'000'000'000ULL);
    CHECK(corpse.selected_setting == "AdaptiveCorpseRuntimeLimit");
    CHECK(corpse.disposition == AdaptiveDisposition::pending);
    CHECK(corpse.old_value == 2000.0);
    CHECK(corpse.effective_value == 2000.0);
    CHECK(corpse.proposed_value.has_value());
    CHECK(*corpse.proposed_value < 120.0);
    CHECK(*corpse.proposed_value >= 4.0);

    AdaptiveGovernor zed_time_governor;
    corpse_pressure.zed_time_protected = true;
    const auto zed_time = drive(
        zed_time_governor, adaptive, corpse_pressure,
        start, 1'000'000'000ULL);
    CHECK(zed_time.disposition == AdaptiveDisposition::hold);
    CHECK(zed_time.reason == "zed_time_corpse_correction_protected");

    AdaptiveGovernor particle_only_governor;
    auto particle_only = sample(
        start, 58.0, 1000.0 / 58.0, 18.0, 35.0, 60.0);
    particle_only.gameplay_context_fresh = true;
    particle_only.particle_pressure = 0.9;
    const auto unavailable_particle = drive(
        particle_only_governor, adaptive, particle_only,
        start, 1'000'000'000ULL);
    CHECK(unavailable_particle.bottleneck.type ==
          AdaptiveBottleneck::particles);
    CHECK(unavailable_particle.disposition ==
          AdaptiveDisposition::skipped_unavailable);
    CHECK(unavailable_particle.reason ==
          "candidate_capability_unavailable");

    AdaptiveGovernor variance_governor;
    auto high_variance = sample(
        start, 60.0, 1000.0 / 60.0,
        adaptive_stability_bands(60).critical_frame_time_ms + 0.5,
        35.0, 60.0);
    const auto variance = drive(
        variance_governor, adaptive, high_variance,
        start, 1'000'000'000ULL);
    CHECK(variance.stability_state == AdaptiveStabilityState::watch);

    AdaptiveGovernor isolated_outlier_governor;
    auto isolated = sample(
        start, 45.0, 1000.0 / 45.0, 28.0, 35.0, 60.0);
    const auto isolated_result = isolated_outlier_governor.evaluate(
        adaptive, isolated, start);
    CHECK(isolated_result.stability_state !=
          AdaptiveStabilityState::correcting);
    auto recovered_from_outlier = sample(
        start + 200'000'000ULL, 60.0, 1000.0 / 60.0,
        17.0, 35.0, 60.0);
    const auto after_outlier = isolated_outlier_governor.evaluate(
        adaptive, recovered_from_outlier,
        recovered_from_outlier.timestamp_ns);
    CHECK(after_outlier.stability_state !=
          AdaptiveStabilityState::correcting);

    AdaptiveGovernor target_change_governor;
    static_cast<void>(target_change_governor.evaluate(
        adaptive, sample(start, 60.0, 1000.0 / 60.0, 17.0, 30.0, 50.0),
        start));
    AdaptivePolicy changed_target = adaptive;
    changed_target.target_fps = 137;
    auto changed_sample = sample(
        start + 200'000'000ULL, 137.0, 1000.0 / 137.0,
        1000.0 / 133.0, 30.0, 50.0);
    const auto rebased = target_change_governor.evaluate(
        changed_target, changed_sample, changed_sample.timestamp_ns);
    CHECK(rebased.disposition == AdaptiveDisposition::hold);
    CHECK(rebased.reason == "target_changed_stabilization_hold");
    CHECK(rebased.settings_generation == 2);
    CHECK(rebased.target_frame_time_ms ==
          adaptive_stability_bands(137).target_frame_time_ms);

    AdaptiveGovernor unbound_governor;
    auto unbound = sample(start, 30.0, 33.33, 42.0, 35.0, 98.0);
    unbound.adapter_luid.reset();
    const auto unbound_decision = drive(
        unbound_governor, adaptive, unbound, start, 1'000'000'000ULL);
    CHECK(unbound_decision.data.quality == AdaptiveDataQuality::degraded);
    CHECK(unbound_decision.disposition == AdaptiveDisposition::shadow);
    CHECK(unbound_decision.reason == "degraded_telemetry_shadow_only");

    AdaptiveGovernor contradictory_governor;
    auto contradictory = sample(start, 60.0, 33.33, 42.0, 35.0, 98.0);
    contradictory.sample_loss = true;
    const auto contradictory_decision = drive(
        contradictory_governor, adaptive, contradictory,
        start, 1'000'000'000ULL);
    CHECK(contradictory_decision.data.quality ==
          AdaptiveDataQuality::degraded);
    CHECK(contradictory_decision.disposition == AdaptiveDisposition::shadow);

    AdaptiveGovernor boundary_governor;
    auto boundary = sample(start, 55.0, 18.18, 20.0, 40.0, 70.0);
    boundary.map_changed = true;
    const auto boundary_decision = boundary_governor.evaluate(
        adaptive, boundary, start);
    CHECK(boundary_decision.data.quality == AdaptiveDataQuality::degraded);
    CHECK(boundary_decision.state == AdaptiveControllerState::observing);
    CHECK(boundary_decision.reason ==
          "telemetry_transition_stabilization_hold");
    CHECK(boundary_decision.restore_generation == 1);

    AdaptiveGovernor discontinuity_governor;
    static_cast<void>(discontinuity_governor.evaluate(
        adaptive, sample(start, 60.0, 16.67, 17.0, 30.0, 50.0), start));
    auto discontinuity = sample(
        start + 200'000'000ULL, 55.0, 18.18, 20.0, 40.0, 70.0);
    discontinuity.discontinuity = true;
    const auto discontinuity_decision = discontinuity_governor.evaluate(
        adaptive, discontinuity, discontinuity.timestamp_ns);
    CHECK(discontinuity_decision.state ==
          AdaptiveControllerState::observing);
    CHECK(discontinuity_decision.reason ==
          "telemetry_transition_stabilization_hold");

    // The same recorded trace must yield the same decisions. This is the
    // offline replay contract used to regression-test the controller without
    // touching KF2.
    AdaptiveGovernor replay_a;
    AdaptiveGovernor replay_b;
    for (std::uint64_t offset = 0; offset <= 3'000'000'000ULL;
         offset += 200'000'000ULL) {
        const double fps = offset < 1'000'000'000ULL ? 60.0 : 45.0;
        auto trace = sample(start + offset, fps, 1000.0 / fps,
                            fps == 60.0 ? 17.0 : 28.0, 40.0, 95.0);
        const auto a = replay_a.evaluate(
            adaptive, trace, trace.timestamp_ns);
        const auto b = replay_b.evaluate(
            adaptive, trace, trace.timestamp_ns);
        CHECK(a.state == b.state);
        CHECK(a.disposition == b.disposition);
        CHECK(a.bottleneck.type == b.bottleneck.type);
        CHECK(a.selected_setting == b.selected_setting);
        CHECK(a.reason == b.reason);
        CHECK(a.restore_generation == b.restore_generation);
    }

    AdaptiveGovernor watchdog_governor;
    static_cast<void>(watchdog_governor.evaluate(
        adaptive, sample(start, 60.0, 16.67, 17.0, 30.0, 50.0), start));
    const auto watchdog = watchdog_governor.evaluate(
        adaptive,
        sample(start - 1, 60.0, 16.67, 17.0, 30.0, 50.0), start - 1);
    CHECK(watchdog.state == AdaptiveControllerState::frozen);
    CHECK(watchdog.watchdog_frozen);
    CHECK(watchdog.disposition == AdaptiveDisposition::blocked);

    CHECK(adaptive_controller_state_name(AdaptiveControllerState::emergency) ==
          L"emergency");
    CHECK(adaptive_bottleneck_name(AdaptiveBottleneck::io_pressure) == L"I/O");
    CHECK(adaptive_disposition_name(AdaptiveDisposition::rollback) ==
          L"rollback");
    CHECK(adaptive_cpu_workload_name(
              AdaptiveCpuWorkload::main_thread_dominant) ==
          L"main-thread dominant");

    // A12/A13/A14: deterministic and seeded randomized long-run coverage.
    // This combines stable intervals, degradation, spikes, capability loss,
    // user/target changes and map/session boundaries without unbounded state.
    AdaptiveGovernor soak_a;
    AdaptiveGovernor soak_b;
    AdaptivePolicy soak_policy = adaptive;
    std::mt19937 random{0x4B463252U};
    std::uniform_real_distribution<double> jitter{-0.20, 0.20};
    std::uniform_int_distribution<int> target_distribution(30, 240);
    std::uint64_t soak_now = start + 100'000'000'000ULL;
    std::uint64_t session_generation = 2;
    std::uint64_t map_generation = 2;
    const auto performance_started = std::chrono::steady_clock::now();
    for (int cycle = 0; cycle < 12'000; ++cycle) {
        if (cycle != 0 && cycle % 2000 == 0) {
            soak_policy.target_fps = target_distribution(random);
        }
        const int phase = cycle % 800;
        const double target_ms =
            adaptive_stability_bands(soak_policy.target_fps)
                .target_frame_time_ms;
        double multiplier = phase < 250 ? 0.99
            : phase < 500 ? 60.0 / 58.0 + 0.01
            : phase < 650 ? 60.0 / 56.0
                          : 1.0;
        if (cycle % 97 == 0) multiplier = 1.8;
        const double frame_ms =
            target_ms * std::max(0.25, multiplier + jitter(random));
        auto trace = sample(
            soak_now, 1000.0 / frame_ms, frame_ms,
            cycle % 31 == 0 ? frame_ms * 1.8 : frame_ms * 1.08,
            45.0, phase >= 250 && phase < 650 ? 92.0 : 55.0);
        trace.session_generation = session_generation;
        trace.map_generation = map_generation;
        if (cycle % 733 == 0 && cycle != 0) {
            ++map_generation;
            trace.map_generation = map_generation;
            trace.map_changed = true;
        }
        if (cycle % 2309 == 0 && cycle != 0) {
            ++session_generation;
            trace.session_generation = session_generation;
            trace.session_changed = true;
        }
        if (cycle % 401 < 300) {
            trace.capabilities.corpse_telemetry =
                AdaptiveCapabilityState::available;
            trace.capabilities.corpse_control =
                AdaptiveCapabilityState::available;
            trace.live_corpse_burden = 120;
            trace.user_max_dead_bodies = 2000;
            trace.adaptive_corpse_runtime_limit = 2000;
        }
        const auto a = soak_a.evaluate(soak_policy, trace, soak_now);
        const auto b = soak_b.evaluate(soak_policy, trace, soak_now);
        CHECK(a.state == b.state);
        CHECK(a.stability_state == b.stability_state);
        CHECK(a.disposition == b.disposition);
        CHECK(a.reason == b.reason);
        CHECK(a.settings_generation == b.settings_generation);
        CHECK(a.target_frame_time_ms > 0.0);
        CHECK(a.warning_frame_time_ms > a.target_frame_time_ms);
        CHECK(a.corrective_frame_time_ms > a.warning_frame_time_ms);
        CHECK(a.critical_frame_time_ms > a.corrective_frame_time_ms);
        CHECK(!a.watchdog_frozen);
        CHECK(soak_a.quality_debt_count() <= 32);
        if (a.selected_setting == "AdaptiveCorpseRuntimeLimit" &&
            a.proposed_value) {
            CHECK(*a.proposed_value >= 4.0);
            CHECK(*a.proposed_value < 120.0);
        }
        soak_now += 120'000'000ULL;
    }
    const auto performance_elapsed = std::chrono::steady_clock::now() -
        performance_started;
    CHECK(performance_elapsed < std::chrono::seconds(3));
    return EXIT_SUCCESS;
}
