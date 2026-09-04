#include <cmath>
#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "features/telemetry/corpse_telemetry_state.hpp"

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

kf2::telemetry_pipeline::TelemetryFrame complete_frame() {
    using namespace kf2;
    telemetry_pipeline::TelemetryFrame frame;
    frame.identity = {42, 9001};
    frame.observed_at_ns = 20'000'000'000ULL;
    frame.frames.age_ns = 1'000'000'000ULL;
    frame.frames.fps = 58.0;
    frame.frames.average_fps = 54.0;
    frame.frames.frame_time_ms = 17.2;
    frame.frames.p95_ms = 20.0;
    frame.frames.p99_ms = 24.0;
    frame.frames.sustained_one_percent_low_fps = 47.0;
    frame.frames.one_percent_low_fps = 45.0;
    frame.frames.quality = telemetry::SampleQuality::good;
    frame.frames.stutter_count = 3;
    frame.frames.loss_count = 2;
    frame.frames.reason = telemetry::UnavailableReason::discontinuity;
    frame.adapter_luid = 77;
    frame.active_gameplay = true;
    frame.offline_gameplay = true;
    frame.evidence.cpu_percent = 35.0;
    frame.evidence.system_cpu_percent = 67.0;
    frame.evidence.critical_core_percent = 92.0;
    frame.evidence.effective_core_usage = 4.25;
    frame.evidence.dominant_thread_share_percent = 55.0;
    frame.evidence.active_cpu_threads = 8;
    frame.evidence.affinity_logical_processors = 16;
    frame.evidence.affinity_physical_cores = 8;
    frame.evidence.system_logical_processors = 24;
    frame.evidence.process_gpu_percent = 52.0;
    frame.evidence.gpu_percent = 70.0;
    frame.evidence.dedicated_vram_bytes = 6ULL << 30U;
    frame.evidence.dedicated_vram_budget_bytes = 12ULL << 30U;
    frame.evidence.adapter_vram_used_bytes = 7ULL << 30U;
    frame.evidence.adapter_vram_budget_bytes = 10ULL << 30U;
    frame.evidence.system_ram_used_bytes = 24ULL << 30U;
    frame.evidence.system_ram_budget_bytes = 32ULL << 30U;
    frame.evidence.system_commit_used_bytes = 30ULL << 30U;
    frame.evidence.system_commit_budget_bytes = 48ULL << 30U;
    frame.evidence.process_private_bytes = 5ULL << 30U;

    game::GameLogSession session;
    session.map = "KF-Outpost";
    session.net_mode = "NM_Standalone";
    session.phase = game::GameLogPhase::map_loaded;
    session.telemetry_sample = 17;
    session.telemetry_observed_ns = 19'000'000'000ULL;
    session.telemetry_corpse_total = 8;
    session.telemetry_corpse_awake = 5;
    session.telemetry_corpse_limit = 10;
    session.telemetry_gore_particles = 20;
    session.telemetry_gore_particle_pool_capacity = 100;
    session.telemetry_gore_particle_visible_components = 30;
    session.telemetry_gore_particle_bounded_components = 40;
    session.telemetry_world_particles = 30;
    session.telemetry_world_particle_pool_capacity = 60;
    session.telemetry_world_particle_visible_components = 50;
    session.telemetry_world_particle_bounded_components = 60;
    session.telemetry_ground_fire_particles = 25;
    session.telemetry_impact_particles = 25;
    session.telemetry_particle_peak_capacity = 100;
    session.telemetry_wound_decals = 10;
    session.telemetry_splatter_decals = 10;
    session.telemetry_pool_decals = 5;
    session.telemetry_impact_decals = 5;
    session.telemetry_explosion_decals = 0;
    session.telemetry_wound_decal_limit = 20;
    session.telemetry_splatter_decal_limit = 20;
    session.telemetry_pool_decal_limit = 20;
    session.telemetry_impact_decal_limit = 20;
    session.telemetry_explosion_decal_limit = 20;
    session.telemetry_living_visible = 7;
    session.telemetry_living_offscreen = 4;
    frame.gameplay = session;

    flex::ObservationSnapshot flex;
    flex.fresh = true;
    flex.pass_through_healthy = true;
    flex.aggregate_particles_fresh = true;
    flex.particle_capacity = 100;
    flex.aggregate_active_particles = 25;
    flex.last_update_tick = 9'000;
    frame.flex = flex;
    return frame;
}

}  // namespace

int main() {
    {
        using namespace kf2::telemetry_pipeline;
        constexpr std::uint64_t second = 1'000'000'000ULL;
        auto ready = complete_frame();
        ready.active_gameplay = true;
        ready.offline_gameplay = true;
        ready.gameplay->net_mode = "NM_Standalone";
        ready.gameplay->telemetry_sample = 5;
        ready.gameplay->telemetry_corpse_limit = 2000;
        ready.gameplay->telemetry_corpse_total = 20;
        ready.gameplay->telemetry_observed_ns = ready.observed_at_ns;
        CorpseTelemetryTracker tracker;
        CHECK(tracker.observe(ready).state == CorpseTelemetryState::available);
        CHECK(!tracker.observe(ready).event);
        auto gap = ready;
        gap.gameplay->telemetry_corpse_limit.reset();
        gap.observed_at_ns += second;
        auto result = tracker.observe(gap);
        CHECK(result.state == CorpseTelemetryState::stale);
        CHECK(result.runtime_limit == 2000);
        CHECK(std::string_view{result.event} == "CORPSE_TELEMETRY_STALE");
        const auto gap_start = gap.observed_at_ns;
        gap.gameplay->map = "KF-NextMap";
        gap.observed_at_ns += 9 * second;
        result = tracker.observe(gap);
        CHECK(result.state == CorpseTelemetryState::stale);
        CHECK(!result.event); // Map changes cannot renew the grace period.
        gap.observed_at_ns = gap_start + CorpseTelemetryTracker::grace_ns;
        result = tracker.observe(gap);
        CHECK(result.state == CorpseTelemetryState::unavailable);
        CHECK(!result.runtime_limit);
        CHECK(std::string_view{result.event} == "CORPSE_TELEMETRY_UNAVAILABLE");
        CHECK(!tracker.observe(gap).event);

        tracker.reset();
        CHECK(tracker.observe(ready).state == CorpseTelemetryState::available);
        gap = ready;
        gap.gameplay.reset();
        gap.observed_at_ns += second;
        CHECK(tracker.observe(gap).state == CorpseTelemetryState::stale);
        auto recovered = ready;
        recovered.observed_at_ns += 2 * second;
        recovered.gameplay->telemetry_observed_ns = recovered.observed_at_ns;
        recovered.gameplay->telemetry_corpse_limit = 1500;
        result = tracker.observe(recovered);
        CHECK(result.state == CorpseTelemetryState::available);
        CHECK(result.runtime_limit == 1500);
        CHECK(std::string_view{result.event} == "CORPSE_TELEMETRY_RECOVERED");
        CHECK(!tracker.observe(recovered).event);

        // A long evaluation pause must not start a brand-new grace window.
        recovered.observed_at_ns += 26 * second;
        result = tracker.observe(recovered);
        CHECK(result.state == CorpseTelemetryState::unavailable);
        CHECK(!result.runtime_limit);

        for (int boundary = 0; boundary < 6; ++boundary) {
            tracker.reset();
            auto initial = ready;
            initial.gameplay->telemetry_control_port = std::uint16_t{1234};
            CHECK(tracker.observe(initial).state == CorpseTelemetryState::available);
            auto changed = initial;
            changed.observed_at_ns += second;
            changed.gameplay->telemetry_corpse_limit.reset();
            if (boundary == 0) ++changed.identity.process_start_id;
            if (boundary == 1) changed.gameplay->net_mode = "NM_Client";
            if (boundary == 2) changed.gameplay->telemetry_control_port = std::uint16_t{2345};
            if (boundary == 3) changed.gameplay->telemetry_sample = 1;
            if (boundary == 5) changed.gameplay->telemetry_observed_ns = changed.observed_at_ns + second;
            result = tracker.observe(changed, boundary != 4);
            CHECK(result.state == CorpseTelemetryState::unavailable);
            CHECK(!result.runtime_limit);
        }
        tracker.reset();
        gap = ready;
        gap.gameplay->telemetry_corpse_limit.reset();
        CHECK(tracker.observe(gap).state == CorpseTelemetryState::unavailable);
        CHECK(!tracker.observe(gap).runtime_limit);
    }
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;
    CHECK(should_log_adaptive_decision(
        true, false, 1'000'000'000ULL, 900'000'000ULL));
    CHECK(should_log_adaptive_decision(
        false, true, 10'000'000'000ULL, 0));
    CHECK(!should_log_adaptive_decision(
        false, true, 14'999'999'999ULL, 10'000'000'000ULL));
    CHECK(should_log_adaptive_decision(
        false, true, 15'000'000'000ULL, 10'000'000'000ULL));
    CHECK(should_log_adaptive_decision(
        false, true, 9'000'000'000ULL, 10'000'000'000ULL));
    CHECK(!should_log_adaptive_decision(
        false, false, 20'000'000'000ULL, 10'000'000'000ULL));

    auto frame = complete_frame();
    // LoadMap is announced before the protected provider starts ticking.
    // Neither that interval nor expired telemetry may admit loading frames.
    {
        auto loading = frame;
        AdaptiveSampleContext boundary;
        boundary.current_map = loading.gameplay->map;
        boundary.last_telemetry_sample = 44;
        loading.gameplay->telemetry_sample.reset();
        loading.gameplay->telemetry_observed_ns = 0;
        const auto waiting = build_adaptive_sample(loading, boundary);
        CHECK(requires_fresh_frame_window(waiting));
        CHECK(waiting.telemetry_sample == 0);
        boundary.last_telemetry_sample = waiting.telemetry_sample;
        loading.gameplay->telemetry_sample = 1;
        loading.gameplay->telemetry_observed_ns = loading.observed_at_ns;
        const auto ready = build_adaptive_sample(loading, boundary);
        CHECK(requires_fresh_frame_window(ready));
        boundary.last_telemetry_sample = ready.telemetry_sample;
        CHECK(!requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
        // LoadMap clears net mode before the later offline/online receipt.
        // Unknown is not online, even if an old provider sample is present.
        loading.gameplay->net_mode.reset();
        CHECK(requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
        loading.gameplay->telemetry_sample = 7;
        loading.gameplay->telemetry_observed_ns = loading.observed_at_ns;
        CHECK(requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
        loading.gameplay->net_mode = "";
        CHECK(requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
        loading.gameplay->net_mode = "NM_Standalone";
        loading.gameplay->telemetry_sample = 1;
        loading.gameplay->telemetry_observed_ns = 1;
        const auto expired = build_adaptive_sample(loading, boundary);
        CHECK(requires_fresh_frame_window(expired));
        CHECK(expired.telemetry_sample == 0);
        boundary.last_telemetry_sample = expired.telemetry_sample;
        CHECK(build_adaptive_sample(loading, boundary).map_generation ==
              boundary.map_generation);
        loading.gameplay->telemetry_observed_ns = loading.observed_at_ns;
        CHECK(requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
        loading.offline_gameplay = false;
        loading.gameplay->net_mode = "NM_Client";
        loading.gameplay->telemetry_sample.reset();
        loading.gameplay->telemetry_observed_ns = 0;
        CHECK(!requires_fresh_frame_window(build_adaptive_sample(loading, boundary)));
    }
    AdaptiveSampleContext context;
    context.current_quality = 80;
    context.minimum_quality = 70;
    context.current_map = "KF-BioticsLab";
    context.map_generation = 4;
    context.last_telemetry_sample = 16;
    context.flex_now_ms = 10'000;
    context.effects_control_verified = true;

    const auto built = build_adaptive_sample(frame, context);
    const auto& sample = built.sample;
    CHECK(requires_fresh_frame_window(built));
    CHECK(sample.pid == 42);
    CHECK(sample.process_start_id == 9001);
    CHECK(sample.timestamp_ns == 19'000'000'000ULL);
    CHECK(sample.session_generation == 9001);
    CHECK(sample.adapter_luid == 77);
    CHECK(sample.fps == frame.frames.fps);
    CHECK(sample.average_fps == frame.frames.average_fps);
    CHECK(sample.frame_time_ms == frame.frames.frame_time_ms);
    CHECK(sample.p95_frame_time_ms == frame.frames.p95_ms);
    CHECK(sample.p99_frame_time_ms == frame.frames.p99_ms);
    CHECK(sample.sustained_one_percent_low_fps ==
          frame.frames.sustained_one_percent_low_fps);
    CHECK(sample.one_percent_low_fps ==
          frame.frames.one_percent_low_fps);
    CHECK(sample.stutter_count == 3);
    CHECK(sample.sample_loss);
    CHECK(sample.discontinuity);
    CHECK(sample.cpu_percent == frame.evidence.cpu_percent);
    CHECK(sample.system_cpu_percent ==
          frame.evidence.system_cpu_percent);
    CHECK(sample.critical_core_percent ==
          frame.evidence.critical_core_percent);
    CHECK(sample.effective_core_usage ==
          frame.evidence.effective_core_usage);
    CHECK(sample.dominant_thread_share_percent ==
          frame.evidence.dominant_thread_share_percent);
    CHECK(sample.active_cpu_threads == frame.evidence.active_cpu_threads);
    CHECK(sample.affinity_logical_processors ==
          frame.evidence.affinity_logical_processors);
    CHECK(sample.affinity_physical_cores ==
          frame.evidence.affinity_physical_cores);
    CHECK(sample.system_logical_processors ==
          frame.evidence.system_logical_processors);
    CHECK(sample.process_gpu_percent ==
          frame.evidence.process_gpu_percent);
    CHECK(sample.gpu_percent == frame.evidence.gpu_percent);
    CHECK(sample.vram_used_bytes ==
          static_cast<double>(*frame.evidence.adapter_vram_used_bytes));
    CHECK(sample.vram_budget_bytes ==
          static_cast<double>(*frame.evidence.adapter_vram_budget_bytes));
    CHECK(sample.ram_used_bytes ==
          static_cast<double>(*frame.evidence.system_ram_used_bytes));
    CHECK(sample.ram_budget_bytes ==
          static_cast<double>(*frame.evidence.system_ram_budget_bytes));
    CHECK(sample.commit_used_bytes ==
          static_cast<double>(*frame.evidence.system_commit_used_bytes));
    CHECK(sample.commit_budget_bytes ==
          static_cast<double>(*frame.evidence.system_commit_budget_bytes));
    CHECK(sample.process_private_bytes ==
          static_cast<double>(*frame.evidence.process_private_bytes));
    CHECK(sample.session_class ==
          optimizer::AdaptiveSessionClass::verified_offline);
    CHECK(sample.capabilities.frame_timing ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.cpu_telemetry ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.gpu_telemetry ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.corpse_telemetry ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.corpse_control ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.gore_control ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.particle_control ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.flex_telemetry ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.flex_solver_substep_control ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(sample.capabilities.flex_particle_budget_control ==
          optimizer::AdaptiveCapabilityState::unavailable);
    CHECK(sample.live_corpse_burden == 8);
    CHECK(sample.adaptive_corpse_runtime_limit == 10);
    CHECK(sample.user_max_dead_bodies == context.user_max_dead_bodies);
    CHECK(sample.map_changed);
    CHECK(sample.map_generation == 5);
    CHECK(built.map == "KF-Outpost");
    CHECK(built.map_generation == 5);
    CHECK(built.telemetry_sample == 17);

    AdaptiveSampleContext current_context = context;
    current_context.current_map = built.map;
    current_context.map_generation = built.map_generation;
    current_context.last_telemetry_sample = built.telemetry_sample;
    CHECK(!requires_fresh_frame_window(
        build_adaptive_sample(frame, current_context)));
    CHECK(sample.gameplay_context_fresh);
    CHECK(sample.visibility_context_fresh);
    CHECK(sample.ragdoll_pressure.has_value());
    CHECK(approximately_equal(*sample.ragdoll_pressure, 0.5));
    CHECK(sample.gore_pressure.has_value());
    CHECK(approximately_equal(*sample.gore_pressure, 0.2));
    CHECK(sample.particle_pressure.has_value());
    CHECK(approximately_equal(*sample.particle_pressure, 0.5));
    CHECK(sample.rendering_pressure.has_value());
    CHECK(approximately_equal(
        *sample.rendering_pressure, std::sqrt(0.8)));
    CHECK(sample.flex_pressure.has_value());
    CHECK(approximately_equal(*sample.flex_pressure, 0.25));
    CHECK(sample.quality_score == 80.0);
    CHECK(!sample.minimum_quality_reached);

    game::GameLogSession tiny_visible_particle_load;
    tiny_visible_particle_load.telemetry_gore_particles = 100;
    tiny_visible_particle_load.telemetry_world_particles = 0;
    tiny_visible_particle_load.telemetry_ground_fire_particles = 0;
    tiny_visible_particle_load.telemetry_impact_particles = 0;
    tiny_visible_particle_load.telemetry_particle_peak_capacity = 100;
    tiny_visible_particle_load.telemetry_gore_particle_visible_components = 1;
    tiny_visible_particle_load.telemetry_world_particle_visible_components = 0;
    tiny_visible_particle_load.telemetry_gore_particle_bounded_components = 50;
    tiny_visible_particle_load.telemetry_world_particle_bounded_components = 50;
    const auto tiny_overdraw = estimate_overdraw_pressure(
        tiny_visible_particle_load);
    CHECK(tiny_overdraw.has_value());
    CHECK(approximately_equal(*tiny_overdraw, 0.1));

    game::GameLogSession invalid_overdraw;
    invalid_overdraw.telemetry_wound_decals = -1;
    invalid_overdraw.telemetry_splatter_decals = 0;
    invalid_overdraw.telemetry_pool_decals = 0;
    invalid_overdraw.telemetry_impact_decals = 0;
    invalid_overdraw.telemetry_explosion_decals = 0;
    invalid_overdraw.telemetry_wound_decal_limit = 10;
    invalid_overdraw.telemetry_splatter_decal_limit = 10;
    invalid_overdraw.telemetry_pool_decal_limit = 10;
    invalid_overdraw.telemetry_impact_decal_limit = 10;
    invalid_overdraw.telemetry_explosion_decal_limit = 10;
    CHECK(!estimate_overdraw_pressure(invalid_overdraw).has_value());

    auto same_map_restart = frame;
    same_map_restart.gameplay->telemetry_sample = 1;
    auto same_map_context = context;
    same_map_context.current_map = "KF-Outpost";
    same_map_context.map_generation = 9;
    same_map_context.last_telemetry_sample = 44;
    const auto restarted = build_adaptive_sample(
        same_map_restart, same_map_context);
    CHECK(restarted.sample.map_changed);
    CHECK(restarted.sample.map_generation == 10);
    CHECK(restarted.telemetry_sample == 1);

    auto underflow = frame;
    underflow.observed_at_ns = 1;
    underflow.frames.age_ns = 2;
    CHECK(build_adaptive_sample(underflow, context).sample.timestamp_ns == 0);

    auto online = frame;
    online.offline_gameplay = false;
    online.gameplay->net_mode = "NM_Client";
    const auto online_sample = build_adaptive_sample(online, context).sample;
    CHECK(online_sample.session_class ==
          optimizer::AdaptiveSessionClass::verified_online);
    CHECK(online_sample.gameplay_context_fresh);
    CHECK(online_sample.capabilities.corpse_telemetry ==
          optimizer::AdaptiveCapabilityState::available);
    CHECK(online_sample.capabilities.corpse_control ==
          optimizer::AdaptiveCapabilityState::unavailable);
    CHECK(online_sample.capabilities.gore_control ==
          optimizer::AdaptiveCapabilityState::unavailable);
    CHECK(online_sample.capabilities.particle_control ==
          optimizer::AdaptiveCapabilityState::unavailable);
    CHECK(online_sample.capabilities.flex_solver_substep_control ==
          optimizer::AdaptiveCapabilityState::unavailable);
    online.gameplay->net_mode = "NM_ListenServer";
    CHECK(build_adaptive_sample(online, context).sample.session_class ==
          optimizer::AdaptiveSessionClass::host_or_listen_server);

    auto stale = frame;
    stale.gameplay->telemetry_observed_ns = 1;
    const auto stale_sample = build_adaptive_sample(stale, context).sample;
    CHECK(!stale_sample.gameplay_context_fresh);
    CHECK(!stale_sample.visibility_context_fresh);
    CHECK(!stale_sample.ragdoll_pressure.has_value());

    auto stale_flex = frame;
    auto late_context = context;
    late_context.flex_now_ms = 11'001;
    CHECK(!build_adaptive_sample(stale_flex, late_context)
               .sample.flex_pressure.has_value());

    TelemetryFrame missing;
    missing.identity = {8, 9};
    missing.observed_at_ns = 100;
    const auto empty = build_adaptive_sample(missing, {});
    CHECK(empty.sample.pid == 8);
    CHECK(empty.sample.process_start_id == 9);
    CHECK(!empty.sample.fps.has_value());
    CHECK(!empty.sample.cpu_percent.has_value());
    CHECK(!empty.sample.system_cpu_percent.has_value());
    CHECK(!empty.sample.gpu_percent.has_value());
    CHECK(!empty.sample.ragdoll_pressure.has_value());
    CHECK(!empty.sample.flex_pressure.has_value());
    CHECK(empty.sample.session_class ==
          optimizer::AdaptiveSessionClass::unknown);

    AdaptiveRuntimeControlInput control;
    control.state = optimizer::AdaptiveControllerState::intervention;
    control.data_quality = optimizer::AdaptiveDataQuality::valid;
    control.primary_resource = optimizer::ResourceKind::cpu;
    control.primary_confidence = 0.80;
    control.current_quality = 100;
    control.minimum_quality = 10;
    control.maximum_quality = 100;
    control.quality_change_budget = 2;
    control.active_gameplay = true;
    control.verified_offline = true;
    control.bridge_available = true;
    control.now_ns = 10'000'000'000ULL;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.current_frame_pressure = true;
    auto selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::cpu);
    CHECK(selected->quality == 90);

    // Time since dispatch is not evidence of a response to the applied change.
    // Even a delayed receipt must get a complete fresh fast window first.
    auto post_applied = control;
    post_applied.state = optimizer::AdaptiveControllerState::emergency;
    post_applied.last_dispatch_ns = 1'000'000'000ULL;
    post_applied.last_applied_ns = 9'500'000'000ULL;
    post_applied.sample_timestamp_ns = post_applied.now_ns;
    CHECK(!select_adaptive_runtime_control(post_applied));
    post_applied.now_ns = 10'500'000'000ULL;
    CHECK(!select_adaptive_runtime_control(post_applied));
    post_applied.sample_timestamp_ns = 10'500'000'000ULL;
    CHECK(select_adaptive_runtime_control(post_applied));
    post_applied.current_frame_pressure = false;
    CHECK(!select_adaptive_runtime_control(post_applied));
    post_applied.current_resource_pressure = true;
    CHECK(select_adaptive_runtime_control(post_applied));

    // Ordinary follow-ups must leave settling, measurement and delivery time
    // after the receipt, including when dispatch happened much earlier.
    for (const auto state : {optimizer::AdaptiveControllerState::intervention,
                             optimizer::AdaptiveControllerState::stable}) {
        auto ordinary = post_applied;
        ordinary.state = state;
        ordinary.current_quality = 70;
        ordinary.recovery_eligible = true;
        for (const auto elapsed : {1'000'000'000ULL, 5'000'000'000ULL,
                                   6'999'999'999ULL}) {
            ordinary.now_ns = ordinary.last_applied_ns + elapsed;
            ordinary.sample_timestamp_ns = ordinary.now_ns;
            CHECK(!select_adaptive_runtime_control(ordinary));
        }
        ordinary.now_ns = ordinary.last_applied_ns + 7'000'000'000ULL;
        ordinary.sample_timestamp_ns = ordinary.now_ns - 250'000'000ULL;
        CHECK(select_adaptive_runtime_control(ordinary));
        ordinary.sample_timestamp_ns = ordinary.last_applied_ns + 999'999'999ULL;
        CHECK(!select_adaptive_runtime_control(ordinary));
        ordinary.now_ns = ordinary.last_applied_ns - 1;
        CHECK(!select_adaptive_runtime_control(ordinary));
    }

    auto fresh_context = context;
    fresh_context.decision_frames = telemetry::FrameMetrics{
        .fps = 60.0, .average_fps = 60.0,
        .sustained_one_percent_low_fps = 60.0,
        .frame_time_ms = 1000.0 / 60.0,
        .p95_ms = 1000.0 / 60.0, .p99_ms = 1000.0 / 60.0,
        .one_percent_low_fps = 60.0,
        .quality = telemetry::SampleQuality::good,
        .reason = telemetry::UnavailableReason::none};
    const auto fresh_sample = build_adaptive_sample(frame, fresh_context).sample;
    CHECK(fresh_sample.fps == 60.0);
    CHECK(fresh_sample.one_percent_low_fps == 60.0);
    CHECK(fresh_sample.stutter_count == 0);
    CHECK(!fresh_sample.sample_loss);
    CHECK(!fresh_sample.discontinuity);
    CHECK(fresh_sample.timestamp_ns == frame.observed_at_ns);
    CHECK(frame.frames.one_percent_low_fps == 45.0);

    control.bottleneck = optimizer::AdaptiveBottleneck::rendering;
    control.bottleneck_confidence = 0.74;
    control.primary_resource = optimizer::ResourceKind::gpu;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::overdraw);
    CHECK(selected->quality == 90);
    control.overdraw_minimum_reached = true;
    control.effects_control_available = true;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::effects);
    control.effects_control_available = false;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::gpu);
    control.effects_control_available = true;
    CHECK(selected->quality == 90);
    control.overdraw_minimum_reached = false;
    control.bottleneck = optimizer::AdaptiveBottleneck::gore;
    control.bottleneck_confidence = 0.60;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::effects);
    control.bottleneck = optimizer::AdaptiveBottleneck::particles;
    control.bottleneck_confidence = 0.64;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::effects);
    control.effects_control_available = false;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::gpu);
    control.effects_control_available = true;
    control.bottleneck_confidence = 0.50;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::gpu);
    control.bottleneck = optimizer::AdaptiveBottleneck::unknown;
    control.bottleneck_confidence = 0.0;
    control.primary_resource = optimizer::ResourceKind::cpu;

    control.current_frame_pressure = false;
    control.current_resource_pressure = true;
    control.primary_resource = optimizer::ResourceKind::vram;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::vram);
    CHECK(selected->quality == 90);
    control.current_resource_pressure = false;
    control.primary_resource = optimizer::ResourceKind::cpu;
    control.current_frame_pressure = true;

    control.state = optimizer::AdaptiveControllerState::emergency;
    control.current_frame_pressure = false;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.state = optimizer::AdaptiveControllerState::intervention;
    control.current_frame_pressure = true;

    control.last_dispatch_ns = 9'500'000'001ULL;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.last_dispatch_ns = 9'000'000'000ULL;
    CHECK(select_adaptive_runtime_control(control).has_value());

    control.last_dispatch_ns = 0;
    control.state = optimizer::AdaptiveControllerState::emergency;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->quality == 80);
    control.current_quality = 75;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->quality == 55);
    control.minimum_quality = 70;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->quality == 70);

    control.minimum_quality = 10;
    control.state = optimizer::AdaptiveControllerState::intervention;
    control.current_quality = 100;
    control.quality_change_budget = 1;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->quality == 95);
    control.quality_change_budget = 5;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->quality == 75);

    control.quality_change_budget = 2;
    control.state = optimizer::AdaptiveControllerState::stable;
    control.current_frame_pressure = false;
    control.recovery_eligible = true;
    control.current_quality = 10;
    control.primary_confidence = 0.1;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::recover);
    CHECK(selected->quality == 15);
    control.current_quality = 75;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::recover);
    CHECK(selected->quality == 80);

    control.maximum_quality = 75;
    control.current_quality = 50;
    selected = select_adaptive_runtime_control(control);
    CHECK(selected.has_value());
    CHECK(selected->resource == game::AdaptiveResourceControl::recover);
    CHECK(selected->quality == 55);

    control.maximum_quality = 100;
    control.current_quality = 75;
    control.last_dispatch_ns = 6'000'000'001ULL;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.last_dispatch_ns = 6'000'000'000ULL;
    CHECK(select_adaptive_runtime_control(control).has_value());

    control.zed_time_active = true;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.zed_time_active = false;
    control.shadow_mode = true;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.shadow_mode = false;
    control.verified_offline = false;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    control.verified_offline = true;
    control.data_quality = optimizer::AdaptiveDataQuality::degraded;
    CHECK(!select_adaptive_runtime_control(control).has_value());
    return EXIT_SUCCESS;
}
