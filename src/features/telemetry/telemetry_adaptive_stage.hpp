#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "features/telemetry/telemetry_frame.hpp"
#include "features/telemetry/enemy_scene_pressure.hpp"
#include "kf2/game/adaptive_control_client.hpp"
#include "kf2/optimizer/adaptive_governor.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct AdaptiveSampleContext final {
    int current_quality{-1};
    int minimum_quality{10};
    int user_max_dead_bodies{20};
    std::string current_map;
    std::uint64_t map_generation{0};
    int last_telemetry_sample{0};
    std::uint64_t flex_now_ms{0};
};

struct AdaptiveSampleBuildResult final {
    optimizer::AdaptiveSample sample;
    std::string map;
    std::uint64_t map_generation{0};
    int telemetry_sample{0};
};

[[nodiscard]] inline bool requires_fresh_frame_window(
    const AdaptiveSampleBuildResult& result) noexcept {
    return result.sample.map_changed;
}

struct AdaptiveRuntimeControlInput final {
    optimizer::AdaptiveControllerState state{
        optimizer::AdaptiveControllerState::observing};
    optimizer::AdaptiveDataQuality data_quality{
        optimizer::AdaptiveDataQuality::not_available};
    optimizer::ResourceKind primary_resource{optimizer::ResourceKind::unknown};
    double primary_confidence{0.0};
    int current_quality{100};
    int minimum_quality{10};
    int maximum_quality{100};
    int quality_change_budget{2};
    bool current_frame_pressure{false};
    bool current_resource_pressure{false};
    std::optional<double> enemy_scene_pressure;
    bool recovery_eligible{false};
    bool active_gameplay{false};
    bool verified_offline{false};
    bool bridge_available{false};
    bool zed_time_active{false};
    bool shadow_mode{false};
    std::uint64_t now_ns{0};
    std::uint64_t last_dispatch_ns{0};
};

struct AdaptiveRuntimeControlSelection final {
    game::AdaptiveResourceControl resource{
        game::AdaptiveResourceControl::mixed};
    int quality{100};
};

[[nodiscard]] inline game::AdaptiveResourceControl adaptive_runtime_resource(
    optimizer::ResourceKind resource, double confidence) noexcept {
    if (confidence < 0.55) return game::AdaptiveResourceControl::mixed;
    switch (resource) {
        case optimizer::ResourceKind::cpu:
            return game::AdaptiveResourceControl::cpu;
        case optimizer::ResourceKind::gpu:
            return game::AdaptiveResourceControl::gpu;
        case optimizer::ResourceKind::vram:
            return game::AdaptiveResourceControl::vram;
        case optimizer::ResourceKind::ram:
            return game::AdaptiveResourceControl::ram;
        case optimizer::ResourceKind::unknown:
            return game::AdaptiveResourceControl::mixed;
    }
    return game::AdaptiveResourceControl::mixed;
}

[[nodiscard]] inline std::optional<AdaptiveRuntimeControlSelection>
select_adaptive_runtime_control(
    const AdaptiveRuntimeControlInput& input) noexcept {
    if (!input.active_gameplay || !input.verified_offline ||
        !input.bridge_available || input.zed_time_active || input.shadow_mode ||
        input.data_quality != optimizer::AdaptiveDataQuality::valid ||
        input.minimum_quality < 10 || input.maximum_quality > 100 ||
        input.minimum_quality > input.maximum_quality ||
        input.quality_change_budget < 1 || input.quality_change_budget > 5 ||
        (input.enemy_scene_pressure &&
         (!std::isfinite(*input.enemy_scene_pressure) ||
          *input.enemy_scene_pressure < 0.0 ||
          *input.enemy_scene_pressure > 1.0)) ||
        input.current_quality < input.minimum_quality ||
        input.current_quality > input.maximum_quality || input.now_ns == 0) {
        return std::nullopt;
    }

    int desired = input.current_quality;
    bool recovery = false;
    std::uint64_t minimum_dispatch_interval_ns = 0;
    const double enemy_pressure =
        input.enemy_scene_pressure.value_or(0.0);
    if (input.state == optimizer::AdaptiveControllerState::emergency) {
        if (!input.current_frame_pressure &&
            !input.current_resource_pressure) return std::nullopt;
        const int base_step = std::clamp(
            input.quality_change_budget * 8, 20, 40);
        const int step = std::min(
            50, base_step + static_cast<int>(
                std::lround(enemy_pressure * 10.0)));
        desired = std::max(
            input.minimum_quality, input.current_quality - step);
        minimum_dispatch_interval_ns = 500'000'000ULL;
    } else if (input.state ==
               optimizer::AdaptiveControllerState::intervention) {
        if (!input.current_frame_pressure &&
            !input.current_resource_pressure) return std::nullopt;
        const int step = input.quality_change_budget * 5 +
            static_cast<int>(std::lround(enemy_pressure * 5.0));
        desired = std::max(
            input.minimum_quality, input.current_quality - step);
        minimum_dispatch_interval_ns = 1'000'000'000ULL;
    } else if (input.state == optimizer::AdaptiveControllerState::stable &&
               input.recovery_eligible &&
               input.current_quality < input.maximum_quality) {
        desired = std::min(
            input.maximum_quality, input.current_quality + 5);
        recovery = true;
        minimum_dispatch_interval_ns = 4'000'000'000ULL;
    } else {
        return std::nullopt;
    }

    if (input.last_dispatch_ns != 0 &&
        (input.now_ns < input.last_dispatch_ns ||
         input.now_ns - input.last_dispatch_ns <
             minimum_dispatch_interval_ns)) {
        return std::nullopt;
    }

    desired = std::clamp(
        desired, input.minimum_quality, input.maximum_quality);
    if (desired == input.current_quality) return std::nullopt;
    if (!recovery && desired >= input.current_quality) {
        return std::nullopt;
    }
    const auto resource = recovery
        ? game::AdaptiveResourceControl::recover
        : adaptive_runtime_resource(
              input.primary_resource, input.primary_confidence);
    return AdaptiveRuntimeControlSelection{resource, desired};
}

[[nodiscard]] inline AdaptiveSampleBuildResult build_adaptive_sample(
    const TelemetryFrame& frame, const AdaptiveSampleContext& context) {
    AdaptiveSampleBuildResult result;
    result.map = context.current_map;
    result.map_generation = context.map_generation;
    result.telemetry_sample = context.last_telemetry_sample;
    auto& sample = result.sample;
    sample.pid = frame.identity.pid;
    sample.process_start_id = frame.identity.process_start_id;
    sample.timestamp_ns = frame.observed_at_ns >= frame.frames.age_ns
        ? frame.observed_at_ns - frame.frames.age_ns : 0;
    sample.session_generation = frame.identity.process_start_id;
    sample.adapter_luid = frame.adapter_luid;
    sample.fps = frame.frames.fps;
    sample.average_fps = frame.frames.average_fps;
    sample.frame_time_ms = frame.frames.frame_time_ms;
    sample.p95_frame_time_ms = frame.frames.p95_ms;
    sample.p99_frame_time_ms = frame.frames.p99_ms;
    sample.one_percent_low_fps = frame.frames.one_percent_low_fps;
    sample.stutter_count = frame.frames.stutter_count;
    if (frame.frames.fps && frame.frames.frame_time_ms &&
        frame.frames.quality !=
            ::kf2::telemetry::SampleQuality::unavailable) {
        sample.capabilities.frame_timing =
            optimizer::AdaptiveCapabilityState::available;
    }
    sample.cpu_percent = frame.evidence.cpu_percent;
    sample.system_cpu_percent = frame.evidence.system_cpu_percent;
    sample.critical_core_percent = frame.evidence.critical_core_percent;
    sample.effective_core_usage = frame.evidence.effective_core_usage;
    sample.dominant_thread_share_percent =
        frame.evidence.dominant_thread_share_percent;
    sample.active_cpu_threads = frame.evidence.active_cpu_threads;
    sample.affinity_logical_processors =
        frame.evidence.affinity_logical_processors;
    sample.affinity_physical_cores =
        frame.evidence.affinity_physical_cores;
    sample.system_logical_processors =
        frame.evidence.system_logical_processors;
    sample.process_gpu_percent = frame.evidence.process_gpu_percent;
    sample.gpu_percent = frame.evidence.gpu_percent;
    if (frame.evidence.cpu_percent ||
        frame.evidence.critical_core_percent ||
        frame.evidence.effective_core_usage) {
        sample.capabilities.cpu_telemetry =
            optimizer::AdaptiveCapabilityState::available;
    }
    if (frame.evidence.gpu_percent) {
        sample.capabilities.gpu_telemetry =
            optimizer::AdaptiveCapabilityState::available;
    }
    const auto vram_used = frame.evidence.adapter_vram_used_bytes
        ? frame.evidence.adapter_vram_used_bytes
        : frame.evidence.dedicated_vram_bytes;
    const auto vram_budget = frame.evidence.adapter_vram_budget_bytes
        ? frame.evidence.adapter_vram_budget_bytes
        : frame.evidence.dedicated_vram_budget_bytes;
    if (vram_used) {
        sample.vram_used_bytes = static_cast<double>(
            *vram_used);
    }
    if (vram_budget) {
        sample.vram_budget_bytes = static_cast<double>(
            *vram_budget);
    }
    if (frame.evidence.system_ram_used_bytes) {
        sample.ram_used_bytes = static_cast<double>(
            *frame.evidence.system_ram_used_bytes);
    }
    if (frame.evidence.system_ram_budget_bytes) {
        sample.ram_budget_bytes = static_cast<double>(
            *frame.evidence.system_ram_budget_bytes);
    }
    if (frame.evidence.system_commit_used_bytes) {
        sample.commit_used_bytes = static_cast<double>(
            *frame.evidence.system_commit_used_bytes);
    }
    if (frame.evidence.system_commit_budget_bytes) {
        sample.commit_budget_bytes = static_cast<double>(
            *frame.evidence.system_commit_budget_bytes);
    }
    if (frame.evidence.process_private_bytes) {
        sample.process_private_bytes = static_cast<double>(
            *frame.evidence.process_private_bytes);
    }
    sample.sample_loss = frame.frames.loss_count > 0;
    sample.discontinuity = frame.frames.reason ==
        ::kf2::telemetry::UnavailableReason::discontinuity;
    if (context.current_quality >= 0) {
        sample.quality_score = static_cast<double>(context.current_quality);
        sample.minimum_quality_reached =
            context.current_quality <= context.minimum_quality;
    }

    if (frame.gameplay) {
        bool telemetry_restarted = false;
        if (frame.gameplay->telemetry_sample) {
            result.telemetry_sample = *frame.gameplay->telemetry_sample;
            telemetry_restarted = context.last_telemetry_sample > 0 &&
                result.telemetry_sample < context.last_telemetry_sample;
        }
        if (frame.offline_gameplay) {
            sample.session_class =
                optimizer::AdaptiveSessionClass::verified_offline;
        } else if (frame.gameplay->net_mode) {
            sample.session_class =
                frame.gameplay->net_mode->find("Listen") != std::string::npos
                    ? optimizer::AdaptiveSessionClass::host_or_listen_server
                    : optimizer::AdaptiveSessionClass::verified_online;
        }
        if ((!frame.gameplay->map.empty() &&
             frame.gameplay->map != result.map) || telemetry_restarted) {
            result.map = frame.gameplay->map;
            ++result.map_generation;
            sample.map_changed = true;
        }
        sample.map_generation = result.map_generation;
        sample.gameplay_context_fresh =
            frame.gameplay->telemetry_observed_ns != 0 &&
            frame.observed_at_ns >=
                frame.gameplay->telemetry_observed_ns &&
            frame.observed_at_ns -
                    frame.gameplay->telemetry_observed_ns <=
                game::kGameLogObservationFreshnessNs;
        sample.visibility_context_fresh =
            sample.gameplay_context_fresh &&
            frame.gameplay->telemetry_living_visible.has_value() &&
            frame.gameplay->telemetry_living_offscreen.has_value();
        if (sample.gameplay_context_fresh &&
            frame.gameplay->telemetry_corpse_limit &&
            frame.gameplay->telemetry_corpse_total) {
            sample.capabilities.corpse_telemetry =
                optimizer::AdaptiveCapabilityState::available;
            sample.live_corpse_burden =
                frame.gameplay->telemetry_corpse_total;
            sample.adaptive_corpse_runtime_limit =
                frame.gameplay->telemetry_corpse_limit;
            sample.user_max_dead_bodies = context.user_max_dead_bodies;
            sample.zed_time_protected =
                frame.gameplay->telemetry_zed_time_active.value_or(false);
            if (frame.offline_gameplay) {
                sample.capabilities.corpse_control =
                    optimizer::AdaptiveCapabilityState::available;
                sample.capabilities.ragdoll_control =
                    optimizer::AdaptiveCapabilityState::available;
                sample.capabilities.corpse_lod_control =
                    optimizer::AdaptiveCapabilityState::available;
                sample.capabilities.skeleton_update_control =
                    optimizer::AdaptiveCapabilityState::available;
            }
        }
        if (sample.gameplay_context_fresh) {
            const auto normalized = [](
                const std::optional<int>& value,
                const std::optional<int>& capacity) -> std::optional<double> {
                if (!value || !capacity || *value < 0 || *capacity <= 0) {
                    return std::nullopt;
                }
                return std::clamp(
                    static_cast<double>(*value) /
                        static_cast<double>(*capacity),
                    0.0, 1.0);
            };
            sample.ragdoll_pressure = normalized(
                frame.gameplay->telemetry_corpse_awake,
                frame.gameplay->telemetry_corpse_limit);
            sample.gore_pressure = normalized(
                frame.gameplay->telemetry_gore_particles,
                frame.gameplay->telemetry_gore_particle_pool_capacity);
            sample.particle_pressure = normalized(
                frame.gameplay->telemetry_world_particles,
                frame.gameplay->telemetry_world_particle_pool_capacity);
            sample.enemy_scene_pressure = calculate_enemy_scene_pressure(
                frame.gameplay->telemetry_living_visible,
                frame.gameplay->telemetry_living_zeds,
                frame.gameplay->telemetry_living_attack_moves,
                sample.gameplay_context_fresh);
        }
    }
    if (frame.flex && frame.flex->fresh &&
        frame.flex->aggregate_particles_fresh &&
        frame.flex->particle_capacity > 0 &&
        frame.flex->aggregate_active_particles >= 0 &&
        frame.flex->last_update_tick != 0 &&
        context.flex_now_ms >= frame.flex->last_update_tick &&
        context.flex_now_ms - frame.flex->last_update_tick <= 2000) {
        sample.flex_pressure = std::clamp(
            static_cast<double>(frame.flex->aggregate_active_particles) /
                static_cast<double>(frame.flex->particle_capacity),
            0.0, 1.0);
    }
    if (frame.flex && frame.flex->fresh && frame.flex->pass_through_healthy) {
        sample.capabilities.flex_telemetry =
            optimizer::AdaptiveCapabilityState::available;
        if (frame.offline_gameplay &&
            !frame.flex->solver_tracking_quarantined) {
            sample.capabilities.flex_solver_substep_control =
                optimizer::AdaptiveCapabilityState::available;
        }
    }
    // The current forwarder observes particle counts and capacity only. No
    // writable budget/spawn/lifetime or fluid split has been proven.
    sample.capabilities.flex_particle_budget_control =
        optimizer::AdaptiveCapabilityState::unavailable;
    sample.capabilities.flex_particle_spawn_control =
        optimizer::AdaptiveCapabilityState::unavailable;
    sample.capabilities.flex_particle_lifetime_control =
        optimizer::AdaptiveCapabilityState::unavailable;
    sample.capabilities.flex_fluid_particle_control =
        optimizer::AdaptiveCapabilityState::unavailable;
    sample.capabilities.flex_nonfluid_particle_control =
        optimizer::AdaptiveCapabilityState::unavailable;
    return result;
}

void run_adaptive_stage(app::UiRuntime& runtime,
                        const TelemetryFrame& frame);

}  // namespace kf2::telemetry_pipeline
