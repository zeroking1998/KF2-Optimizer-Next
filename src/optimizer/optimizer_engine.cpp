#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kf2::optimizer {
namespace {

bool valid_value(config::SettingId id, const config::SettingValue& value) {
    const auto* definition = config::find_setting(id);
    return definition && config::serialize_setting_value(*definition, value).has_value();
}

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

std::wstring bottleneck_reason(Bottleneck bottleneck) {
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

void put(std::map<config::SettingId, config::RequestedChange>& changes,
         config::SettingId id, config::SettingValue value,
         config::ChangeSource source, std::wstring reason) {
    if (!valid_value(id, value)) return;
    changes.insert_or_assign(
        id, config::RequestedChange{id, std::move(value), source, std::move(reason)});
}

void apply_effect_profile(
    std::map<config::SettingId, config::RequestedChange>& changes,
    Profile profile) {
    const bool high_performance = profile == Profile::high_performance;
    const bool stability = profile == Profile::stability;
    const auto select = [high_performance, stability](auto performance_value,
                                                       auto balanced_value,
                                                       auto stability_value) {
        return high_performance ? performance_value
                                : stability ? stability_value : balanced_value;
    };
    const auto source = config::ChangeSource::adaptive;
    const std::wstring reason = high_performance
        ? L"Use KF2's shipped performance effect bucket"
        : stability
            ? L"Use KF2's shipped high-quality effect bucket"
            : L"Use KF2's shipped balanced effect bucket";

    put(changes, config::SettingId::corpse_limit,
        select(8, 12, 15), source, reason);
    put(changes, config::SettingId::gore_effect_limit,
        select(8, 10, 15), source, reason);
    put(changes, config::SettingId::explosion_decal_limit,
        select(12, 15, 20), source, reason);
    put(changes, config::SettingId::impact_decal_limit,
        select(15, 20, 40), source, reason);
    put(changes, config::SettingId::wound_decal_limit,
        5, source, reason);
    put(changes, config::SettingId::blood_splatter_decal_limit,
        20, source, reason);
    put(changes, config::SettingId::blood_pool_decal_limit,
        20, source, reason);
    put(changes, config::SettingId::blood_effect_limit,
        select(15, 25, 40), source, reason);
    put(changes, config::SettingId::body_wound_decal_lifetime,
        30, source, reason);
    put(changes, config::SettingId::blood_splatter_lifetime,
        10, source, reason);
    put(changes, config::SettingId::blood_pool_lifetime,
        20, source, reason);
    put(changes, config::SettingId::giblet_lifetime,
        10, source, reason);
    put(changes, config::SettingId::gore_lifetime_multiplier,
        select(0.75, 1.0, 1.2), source, reason);
    put(changes, config::SettingId::persistent_splats_per_frame,
        select(50, 75, 100), source, reason);
    put(changes, config::SettingId::blood_splatter_decals,
        !high_performance, source, reason);
    put(changes, config::SettingId::secondary_blood_effects,
        !high_performance, source, reason);
}

void apply_render_profile(
    std::map<config::SettingId, config::RequestedChange>& changes,
    Profile profile) {
    const bool high_performance = profile == Profile::high_performance;
    const bool stability = profile == Profile::stability;
    const auto select = [high_performance, stability](auto performance_value,
                                                       auto balanced_value,
                                                       auto stability_value) {
        return high_performance ? performance_value
                                : stability ? stability_value : balanced_value;
    };
    const auto source = config::ChangeSource::adaptive;
    const std::wstring reason = high_performance
        ? L"Use KF2's shipped performance rendering bucket"
        : stability
            ? L"Use KF2's shipped high-quality rendering bucket"
            : L"Use KF2's shipped balanced rendering bucket";

    put(changes, config::SettingId::static_decals, true, source, reason);
    put(changes, config::SettingId::dynamic_decals,
        true, source, reason);
    put(changes, config::SettingId::decal_cull_distance_scale,
        select(0.5, 0.6, 0.8), source, reason);
    put(changes, config::SettingId::dynamic_shadows,
        !high_performance, source, reason);
    put(changes, config::SettingId::light_environment_shadows,
        !high_performance, source, reason);
    put(changes, config::SettingId::ambient_occlusion,
        !high_performance, source, reason);
    put(changes, config::SettingId::bloom,
        !high_performance, source, reason);
    put(changes, config::SettingId::distortion,
        !high_performance, source, reason);
    put(changes, config::SettingId::drop_particle_distortion,
        high_performance, source, reason);
    put(changes, config::SettingId::high_quality_materials,
        !high_performance, source, reason);
    put(changes, config::SettingId::detail_mode,
        select(0, 1, 2), source, reason);
    put(changes, config::SettingId::max_shadow_resolution,
        select(512, 1024, 2048), source, reason);
    put(changes, config::SettingId::max_whole_scene_shadow_resolution,
        select(512, 1280, 2048), source, reason);
    put(changes, config::SettingId::shadow_texels_per_pixel,
        select(0.9, 1.3, 2.0), source, reason);
    put(changes, config::SettingId::fracture_cull_distance_scale,
        select(0.5, 1.0, 1.5), source, reason);

    put(changes, config::SettingId::particle_lod_bias, 0, source, reason);
    put(changes, config::SettingId::skeletal_mesh_lod_bias,
        high_performance ? 1 : 0, source, reason);
    put(changes, config::SettingId::global_shadow_distance_scale,
        select(0.75, 1.0, 1.5), source, reason);
    put(changes, config::SettingId::depth_of_field,
        !high_performance, source, reason);
    put(changes, config::SettingId::light_shafts,
        !high_performance, source, reason);
    put(changes, config::SettingId::lens_flares,
        !high_performance, source, reason);
    put(changes, config::SettingId::radial_blur,
        !high_performance, source, reason);
    put(changes, config::SettingId::fractured_damage,
        !high_performance, source, reason);
    put(changes, config::SettingId::motion_blur,
        stability, source, reason);
    put(changes, config::SettingId::post_process_aa,
        true, source, reason);
    put(changes, config::SettingId::filtered_distortion,
        !high_performance, source, reason);
    put(changes, config::SettingId::unbatched_decals,
        true, source, reason);
    put(changes, config::SettingId::whole_scene_dominant_shadows,
        true, source, reason);
    put(changes, config::SettingId::conservative_shadow_bounds,
        high_performance, source, reason);
    put(changes, config::SettingId::max_anisotropy,
        select(1, 4, 16), source, reason);
    put(changes, config::SettingId::post_process_mlaa,
        !high_performance, source, reason);
    put(changes, config::SettingId::screen_space_reflections,
        stability, source, reason);
    put(changes, config::SettingId::subsurface_scattering,
        stability, source, reason);
    put(changes, config::SettingId::light_functions,
        !high_performance, source, reason);
    put(changes, config::SettingId::light_cones,
        !high_performance, source, reason);
    put(changes, config::SettingId::destruction_lifetime_scale,
        select(0.5, 1.0, 1.2), source, reason);
    put(changes, config::SettingId::explosion_lights,
        true, source, reason);
    put(changes, config::SettingId::depth_of_field_quality,
        high_performance ? 0 : 1, source, reason);
    put(changes, config::SettingId::bloom_quality,
        high_performance ? 1 : 2, source, reason);
    put(changes, config::SettingId::distance_fog_quality,
        high_performance ? 0 : 1, source, reason);
    put(changes, config::SettingId::motion_blur_quality,
        stability ? 1 : 0, source, reason);
    put(changes, config::SettingId::hbao,
        stability, source, reason);
    put(changes, config::SettingId::per_object_shadows,
        true, source, reason);
    put(changes, config::SettingId::min_shadow_resolution,
        stability ? 32 : 64, source, reason);
    put(changes, config::SettingId::shadow_fade_resolution,
        stability ? 64 : 128, source, reason);
    put(changes, config::SettingId::foreground_preshadows,
        !high_performance, source, reason);
    put(changes, config::SettingId::emitter_pool_scale,
        select(0.5, 1.0, 2.0), source, reason);
    put(changes, config::SettingId::shell_eject_lifetime,
        select(5.0, 10.0, 20.0), source, reason);
    put(changes, config::SettingId::spray_actor_lights,
        !high_performance, source, reason);
    put(changes, config::SettingId::pilot_lights,
        true, source, reason);
    put(changes, config::SettingId::override_map_whole_scene_shadow,
        stability, source, reason);
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

    std::map<config::SettingId, config::RequestedChange> changes;
    const bool valid_target = valid_target_fps(input.target_fps);
    const bool explicit_profile_preview =
        input.profile_preview_requested && valid_target;
    if (explicit_profile_preview) {
        decision.reason = decision.bottleneck == Bottleneck::unavailable
            ? L"Explicit selected-profile preview; adaptive classification remains unavailable"
            : L"Explicit selected-profile preview; " + decision.reason;
    }
    if (valid_target &&
        (decision.bottleneck != Bottleneck::unavailable ||
         explicit_profile_preview)) {
        if (input.quality == QualityPolicy::performance &&
            (explicit_profile_preview ||
             decision.bottleneck != Bottleneck::frame_cap)) {
            if (input.profile != Profile::custom) {
                // Every named profile owns the complete verified Adaptive set.
                // This prevents values from a previously applied profile from
                // leaking into the next one.
                apply_effect_profile(changes, input.profile);
                apply_render_profile(changes, input.profile);
            }
        }
    }

    for (auto& [id, change] : changes) {
        static_cast<void>(id);
        decision.changes.push_back(std::move(change));
    }
    return decision;
}

}  // namespace kf2::optimizer
