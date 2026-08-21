#include "features/telemetry/telemetry_presentation_stage.hpp"

#include <array>
#include <utility>

#include "app/application_runtime.hpp"

namespace kf2::telemetry_pipeline {

TelemetryPresentation derive_telemetry_presentation(
    const app::UiRuntime& runtime, const TelemetryFrame& frame) {
    TelemetryPresentation result;
    const auto analysis = frame.frames.fps
        ? std::optional{optimizer::evaluate({
              .target_fps = runtime.optimizer_settings.target_fps,
              .evidence = frame.evidence})}
        : std::nullopt;
    const auto& adaptive_status = runtime.model.status();
    result.status = build_status_projection(
        frame, runtime.telemetry_failure,
        analysis ? std::wstring_view{analysis->reason} : std::wstring_view{},
        adaptive_status.recommended_profile,
        adaptive_status.recommendation_reason);

    if (!runtime.overlay_window) return result;
    const auto process_ram_bytes = frame.process
        ? std::optional<std::uint64_t>{frame.process->working_set_bytes}
        : std::nullopt;
    const auto dedicated_vram_bytes = frame.adapter_gpu
        ? std::optional<std::uint64_t>{frame.adapter_gpu->dedicated_bytes}
        : std::nullopt;
    auto overlay_presentation = overlay::evaluate_overlay(
        {runtime.overlay_enabled && runtime.overlay_scene_ready, frame.window,
         frame.frames, runtime.overlay_corner, runtime.overlay_scale,
         {330, 105}, 10, frame.evidence.cpu_percent,
         frame.evidence.gpu_percent, process_ram_bytes, dedicated_vram_bytes,
         runtime.optimizer_settings.overlay_show_fps,
         runtime.optimizer_settings.overlay_show_frame_time,
         runtime.optimizer_settings.overlay_show_cpu,
         runtime.optimizer_settings.overlay_show_gpu,
         runtime.optimizer_settings.overlay_show_memory,
         runtime.controller.theme().animations_enabled});
    if (overlay_presentation.visible &&
        game::is_game_area_covered(frame.window,
                                   overlay_presentation.bounds)) {
        const std::array corners{
            overlay::OverlayCorner::top_left,
            overlay::OverlayCorner::top_right,
            overlay::OverlayCorner::bottom_left,
            overlay::OverlayCorner::bottom_right};
        bool found_free_corner = false;
        for (const auto corner : corners) {
            if (corner == runtime.overlay_corner) continue;
            auto candidate = overlay::evaluate_overlay(
                {runtime.overlay_enabled, frame.window, frame.frames, corner,
                 runtime.overlay_scale, {330, 105}, 10,
                 frame.evidence.cpu_percent, frame.evidence.gpu_percent,
                 process_ram_bytes, dedicated_vram_bytes,
                 runtime.optimizer_settings.overlay_show_fps,
                 runtime.optimizer_settings.overlay_show_frame_time,
                 runtime.optimizer_settings.overlay_show_cpu,
                 runtime.optimizer_settings.overlay_show_gpu,
                 runtime.optimizer_settings.overlay_show_memory,
                 runtime.controller.theme().animations_enabled});
            if (candidate.visible && !game::is_game_area_covered(
                                         frame.window, candidate.bounds)) {
                overlay_presentation = std::move(candidate);
                found_free_corner = true;
                break;
            }
        }
        if (!found_free_corner) {
            overlay_presentation.visible = false;
            overlay_presentation.reason =
                overlay::OverlayHideReason::not_foreground;
        }
    }
    result.overlay = std::move(overlay_presentation);
    return result;
}

void publish_telemetry_presentation(
    app::UiRuntime& runtime, TelemetryPresentation presentation) {
    const auto& current = runtime.model.status();
    const auto& projection = presentation.status;
    if (current.telemetry != projection.telemetry ||
        current.performance_analysis != projection.performance_analysis ||
        current.live_fps != projection.live_fps ||
        current.live_frame_time_ms != projection.live_frame_time_ms ||
        current.live_cpu_percent != projection.live_cpu_percent ||
        current.live_gpu_percent != projection.live_gpu_percent ||
        current.live_active_corpses != projection.live_active_corpses ||
        current.live_sleeping_corpses != projection.live_sleeping_corpses) {
        auto status = current;
        status.telemetry = std::move(presentation.status.telemetry);
        status.performance_analysis =
            std::move(presentation.status.performance_analysis);
        status.live_fps = projection.live_fps;
        status.live_frame_time_ms = projection.live_frame_time_ms;
        status.live_cpu_percent = projection.live_cpu_percent;
        status.live_gpu_percent = projection.live_gpu_percent;
        status.live_active_corpses = projection.live_active_corpses;
        status.live_sleeping_corpses = projection.live_sleeping_corpses;
        runtime.model.set_status(std::move(status));
        runtime.invalidate();
    }

    if (!runtime.overlay_window || !presentation.overlay) return;
    runtime.overlay_presentation = *presentation.overlay;
    const auto updated =
        runtime.overlay_window->update(*presentation.overlay);
    if (!updated.has_value()) {
        std::wstring message = updated.error().message;
        if (updated.error().native_code != 0) {
            message += L" (Windows error " +
                std::to_wstring(updated.error().native_code) + L")";
        }
        runtime.events->append(
            {0, diagnostics::Severity::warning, "OVERLAY_UPDATE_FAILED",
             std::move(message), L"overlay"});
    }
}
}  // namespace kf2::telemetry_pipeline

namespace kf2::app {

void UiRuntime::append_gameplay_report_fields(
    diagnostics::ProductReport& report) const noexcept {
    const auto& session = last_report_gameplay_session;
    if (!session || !game::game_log_is_offline_gameplay(*session)) return;
    report.offline_gameplay = true;
    const auto now = monotonic_ns();
    std::uint64_t oldest_age_ns = 0;
    const auto include = [&](const std::optional<int>& source,
                             std::uint64_t observed_at_ns,
                             std::optional<int>& destination) {
        if (!game::game_log_observation_is_fresh(
                source, observed_at_ns, now)) {
            return;
        }
        destination = source;
        oldest_age_ns = std::max(oldest_age_ns, now - observed_at_ns);
        report.gameplay_snapshot_fresh = true;
    };
    include(session->zeds_alive, session->zeds_alive_observed_ns,
            report.zeds_alive);
    include(session->zeds_remaining, session->zeds_remaining_observed_ns,
            report.zeds_remaining);
    include(session->wave_number, session->wave_observed_ns,
            report.wave_number);
    include(session->wave_total_ai, session->wave_observed_ns,
            report.wave_total_ai);
    include(session->telemetry_living_zeds,
            session->telemetry_observed_ns,
            report.telemetry_living_zeds);
    include(session->telemetry_living_classes,
            session->telemetry_observed_ns, report.living_classes);
    include(session->telemetry_living_bosses,
            session->telemetry_observed_ns, report.living_bosses);
    include(session->telemetry_living_visible,
            session->telemetry_observed_ns, report.living_visible);
    include(session->telemetry_living_offscreen,
            session->telemetry_observed_ns, report.living_offscreen);
    include(session->telemetry_living_lod_total,
            session->telemetry_observed_ns, report.living_lod_total);
    include(session->telemetry_living_anim_rate_total,
            session->telemetry_observed_ns,
            report.living_anim_rate_total);
    include(session->telemetry_living_injured_zones,
            session->telemetry_observed_ns,
            report.living_injured_zones);
    include(session->telemetry_living_required_bones,
            session->telemetry_observed_ns, report.living_required_bones);
    include(session->telemetry_living_material_slots,
            session->telemetry_observed_ns, report.living_material_slots);
    include(session->telemetry_living_attachments,
            session->telemetry_observed_ns, report.living_attachments);
    include(session->telemetry_living_anim_skipped,
            session->telemetry_observed_ns, report.living_anim_skipped);
    include(session->telemetry_living_bone_atoms_skipped,
            session->telemetry_observed_ns,
            report.living_bone_atoms_skipped);
    include(session->telemetry_living_bone_interpolation,
            session->telemetry_observed_ns,
            report.living_bone_interpolation);
    include(session->telemetry_living_kinematic_distance_skipped,
            session->telemetry_observed_ns,
            report.living_kinematic_distance_skipped);
    include(session->telemetry_living_ticks_offscreen,
            session->telemetry_observed_ns,
            report.living_ticks_offscreen);
    include(session->telemetry_living_special_moves,
            session->telemetry_observed_ns, report.living_special_moves);
    include(session->telemetry_living_attack_moves,
            session->telemetry_observed_ns, report.living_attack_moves);
    include(session->telemetry_living_grapple_moves,
            session->telemetry_observed_ns, report.living_grapple_moves);
    include(session->telemetry_living_stumbles,
            session->telemetry_observed_ns, report.living_stumbles);
    include(session->telemetry_living_knockdowns,
            session->telemetry_observed_ns, report.living_knockdowns);
    include(session->telemetry_living_hit_reactions,
            session->telemetry_observed_ns, report.living_hit_reactions);
    include(session->telemetry_living_other_special_moves,
            session->telemetry_observed_ns,
            report.living_other_special_moves);
    include(session->telemetry_corpse_total,
            session->telemetry_observed_ns, report.corpse_total);
    include(session->telemetry_corpse_awake,
            session->telemetry_observed_ns, report.corpse_awake);
    include(session->telemetry_corpse_sleeping,
            session->telemetry_observed_ns, report.corpse_sleeping);
    include(session->telemetry_corpse_other,
            session->telemetry_observed_ns, report.corpse_other);
    include(session->telemetry_corpse_final_pose,
            session->telemetry_observed_ns, report.corpse_final_pose);
    include(session->telemetry_corpse_visible,
            session->telemetry_observed_ns, report.corpse_visible);
    include(session->telemetry_corpse_offscreen,
            session->telemetry_observed_ns, report.corpse_offscreen);
    include(session->telemetry_corpse_lod_total,
            session->telemetry_observed_ns, report.corpse_lod_total);
    include(session->telemetry_corpse_injured_zones,
            session->telemetry_observed_ns,
            report.corpse_injured_zones);
    include(session->telemetry_corpse_max_age_ms,
            session->telemetry_observed_ns, report.corpse_max_age_ms);
    include(session->telemetry_corpse_limit,
            session->telemetry_observed_ns, report.corpse_limit);
    include(session->telemetry_corpse_offscreen_time_ms,
            session->telemetry_observed_ns,
            report.corpse_offscreen_time_ms);
    include(session->telemetry_corpse_offscreen_distance,
            session->telemetry_observed_ns,
            report.corpse_offscreen_distance);
    include(session->telemetry_dismembered_corpses,
            session->telemetry_observed_ns,
            report.dismembered_corpses);
    include(session->telemetry_dismembered_limbs,
            session->telemetry_observed_ns, report.dismembered_limbs);
    include(session->telemetry_ragdoll_warned_corpses,
            session->telemetry_observed_ns,
            report.ragdoll_warned_corpses);
    include(session->telemetry_ragdoll_warning_max,
            session->telemetry_observed_ns, report.ragdoll_warning_max);
    include(session->telemetry_visible_gibs,
            session->telemetry_observed_ns, report.visible_gibs);
    include(session->telemetry_spray_actors,
            session->telemetry_observed_ns, report.spray_actors);
    include(session->telemetry_fire_spray_actors,
            session->telemetry_observed_ns, report.fire_spray_actors);
    include(session->telemetry_toxic_spray_actors,
            session->telemetry_observed_ns, report.toxic_spray_actors);
    include(session->telemetry_other_spray_actors,
            session->telemetry_observed_ns, report.other_spray_actors);
    include(session->telemetry_explosion_actors,
            session->telemetry_observed_ns, report.explosion_actors);
    include(session->telemetry_damaging_explosion_actors,
            session->telemetry_observed_ns,
            report.damaging_explosion_actors);
    include(session->telemetry_fire_explosion_actors,
            session->telemetry_observed_ns,
            report.fire_explosion_actors);
    include(session->telemetry_toxic_explosion_actors,
            session->telemetry_observed_ns,
            report.toxic_explosion_actors);
    include(session->telemetry_other_damaging_explosion_actors,
            session->telemetry_observed_ns,
            report.other_damaging_explosion_actors);
    include(session->telemetry_unclassified_explosion_actors,
            session->telemetry_observed_ns,
            report.unclassified_explosion_actors);
    include(session->telemetry_lingering_explosion_actors,
            session->telemetry_observed_ns,
            report.lingering_explosion_actors);
    include(session->telemetry_smoke_explosion_actors,
            session->telemetry_observed_ns,
            report.smoke_explosion_actors);
    include(session->telemetry_bloat_king_fart_explosion_actors,
            session->telemetry_observed_ns,
            report.bloat_king_fart_explosion_actors);
    include(session->telemetry_smoke_grenade_projectiles,
            session->telemetry_observed_ns,
            report.smoke_grenade_projectiles);
    include(session->telemetry_puke_mine_projectiles,
            session->telemetry_observed_ns,
            report.puke_mine_projectiles);
    include(session->telemetry_bloat_king_puke_mine_projectiles,
            session->telemetry_observed_ns,
            report.bloat_king_puke_mine_projectiles);
    include(session->telemetry_wound_decals,
            session->telemetry_observed_ns, report.wound_decals);
    include(session->telemetry_splatter_decals,
            session->telemetry_observed_ns, report.splatter_decals);
    include(session->telemetry_pool_decals,
            session->telemetry_observed_ns, report.pool_decals);
    include(session->telemetry_impact_decals,
            session->telemetry_observed_ns, report.impact_decals);
    include(session->telemetry_explosion_decals,
            session->telemetry_observed_ns, report.explosion_decals);
    include(session->telemetry_wound_decal_limit,
            session->telemetry_observed_ns, report.wound_decal_limit);
    include(session->telemetry_splatter_decal_limit,
            session->telemetry_observed_ns,
            report.splatter_decal_limit);
    include(session->telemetry_pool_decal_limit,
            session->telemetry_observed_ns, report.pool_decal_limit);
    include(session->telemetry_impact_decal_limit,
            session->telemetry_observed_ns, report.impact_decal_limit);
    include(session->telemetry_explosion_decal_limit,
            session->telemetry_observed_ns,
            report.explosion_decal_limit);
    include(session->telemetry_blood_effect_limit,
            session->telemetry_observed_ns, report.blood_effect_limit);
    include(session->telemetry_gore_effect_limit,
            session->telemetry_observed_ns, report.gore_effect_limit);
    include(session->telemetry_wound_lifetime_ms,
            session->telemetry_observed_ns, report.wound_lifetime_ms);
    include(session->telemetry_splatter_lifetime_ms,
            session->telemetry_observed_ns,
            report.splatter_lifetime_ms);
    include(session->telemetry_pool_lifetime_ms,
            session->telemetry_observed_ns, report.pool_lifetime_ms);
    include(session->telemetry_gib_lifetime_ms,
            session->telemetry_observed_ns, report.gib_lifetime_ms);
    include(session->telemetry_gore_particle_components,
            session->telemetry_observed_ns,
            report.gore_particle_components);
    include(session->telemetry_gore_particles,
            session->telemetry_observed_ns, report.gore_particles);
    include(session->telemetry_gore_particle_visible_components,
            session->telemetry_observed_ns,
            report.gore_particle_visible_components);
    include(session->telemetry_gore_particle_lod_total,
            session->telemetry_observed_ns,
            report.gore_particle_lod_total);
    include(session->telemetry_gore_particle_bounded_components,
            session->telemetry_observed_ns,
            report.gore_particle_bounded_components);
    include(session->telemetry_world_particle_components,
            session->telemetry_observed_ns,
            report.world_particle_components);
    include(session->telemetry_world_particles,
            session->telemetry_observed_ns, report.world_particles);
    include(session->telemetry_world_particle_visible_components,
            session->telemetry_observed_ns,
            report.world_particle_visible_components);
    include(session->telemetry_world_particle_lod_total,
            session->telemetry_observed_ns,
            report.world_particle_lod_total);
    include(session->telemetry_world_particle_bounded_components,
            session->telemetry_observed_ns,
            report.world_particle_bounded_components);
    include(session->telemetry_ground_fire_particle_components,
            session->telemetry_observed_ns,
            report.ground_fire_particle_components);
    include(session->telemetry_ground_fire_particles,
            session->telemetry_observed_ns,
            report.ground_fire_particles);
    include(session->telemetry_impact_particle_components,
            session->telemetry_observed_ns,
            report.impact_particle_components);
    include(session->telemetry_impact_particles,
            session->telemetry_observed_ns, report.impact_particles);
    include(session->telemetry_gore_particle_pool_capacity,
            session->telemetry_observed_ns,
            report.gore_particle_pool_capacity);
    include(session->telemetry_world_particle_pool_capacity,
            session->telemetry_observed_ns,
            report.world_particle_pool_capacity);
    include(session->telemetry_ground_fire_particle_pool_capacity,
            session->telemetry_observed_ns,
            report.ground_fire_particle_pool_capacity);
    include(session->telemetry_impact_particle_pool_capacity,
            session->telemetry_observed_ns,
            report.impact_particle_pool_capacity);
    include(session->telemetry_particle_constant_spawn_emitters,
            session->telemetry_observed_ns,
            report.particle_constant_spawn_emitters);
    include(session->telemetry_particle_dynamic_spawn_emitters,
            session->telemetry_observed_ns,
            report.particle_dynamic_spawn_emitters);
    include(session->telemetry_particle_constant_spawn_rate_milli,
            session->telemetry_observed_ns,
            report.particle_constant_spawn_rate_milli);
    include(session->telemetry_particle_burst_entries,
            session->telemetry_observed_ns,
            report.particle_burst_entries);
    include(session->telemetry_particle_peak_capacity,
            session->telemetry_observed_ns,
            report.particle_peak_capacity);
    include(session->telemetry_particle_flex_components,
            session->telemetry_observed_ns,
            report.particle_flex_components);
    include(session->telemetry_particle_flex_fluid_components,
            session->telemetry_observed_ns,
            report.particle_flex_fluid_components);
    include(session->telemetry_particle_flex_nonfluid_components,
            session->telemetry_observed_ns,
            report.particle_flex_nonfluid_components);
    include(session->telemetry_particle_flex_mixed_components,
            session->telemetry_observed_ns,
            report.particle_flex_mixed_components);
    include(session->telemetry_particle_nonflex_components,
            session->telemetry_observed_ns,
            report.particle_nonflex_components);
    include(session->telemetry_particle_unclassified_components,
            session->telemetry_observed_ns,
            report.particle_unclassified_components);
    include(session->telemetry_flex_surrogate_particles,
            session->telemetry_observed_ns,
            report.flex_surrogate_particles);
    include(session->telemetry_flex_surrogate_lod,
            session->telemetry_observed_ns, report.flex_surrogate_lod);
    const auto include_boolean = [&](const std::optional<bool>& source,
                                     std::optional<bool>& destination) {
        if (!source || session->telemetry_observed_ns == 0 ||
            now < session->telemetry_observed_ns ||
            now - session->telemetry_observed_ns >
                game::kGameLogObservationFreshnessNs) {
            return;
        }
        destination = source;
        oldest_age_ns = std::max(
            oldest_age_ns, now - session->telemetry_observed_ns);
        report.gameplay_snapshot_fresh = true;
    };
    include_boolean(session->telemetry_flex_surrogate_active,
                    report.flex_surrogate_active);
    include_boolean(session->telemetry_flex_surrogate_visible,
                    report.flex_surrogate_visible);
    include_boolean(session->telemetry_corpse_collide_dead,
                    report.corpse_collide_dead);
    include_boolean(session->telemetry_corpse_collide_living,
                    report.corpse_collide_living);
    include_boolean(session->telemetry_corpse_collide_dead_after_sleep,
                    report.corpse_collide_dead_after_sleep);
    include_boolean(session->telemetry_corpse_collide_living_after_sleep,
                    report.corpse_collide_living_after_sleep);
    include_boolean(session->telemetry_zed_time_active,
                    report.zed_time_active);
    if (report.gameplay_snapshot_fresh) {
        report.gameplay_snapshot_age_ms = oldest_age_ns / 1'000'000ULL;
    }
}

}  // namespace kf2::app
