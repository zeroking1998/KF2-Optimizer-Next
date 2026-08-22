#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include "features/telemetry/telemetry_frame.hpp"
#include "kf2/optimizer/adaptive_governor.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

struct AdaptiveSampleContext final {
    int current_quality{-1};
    int minimum_quality{0};
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
