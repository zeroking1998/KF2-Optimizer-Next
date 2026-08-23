#include <cstdlib>
#include <iostream>

#include "kf2/optimizer/optimizer_engine.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

const kf2::config::RequestedChange* find_change(
    const kf2::optimizer::OptimizerDecision& decision,
    kf2::config::SettingId id) {
    for (const auto& change : decision.changes) {
        if (change.id == id) return &change;
    }
    return nullptr;
}

}  // namespace

int main() {
    using namespace kf2;
    using namespace kf2::optimizer;

    OptimizerInput unavailable;
    auto no_evidence = evaluate(unavailable);
    CHECK(no_evidence.bottleneck == Bottleneck::unavailable);
    CHECK(no_evidence.confidence == Confidence::unavailable);
    CHECK(no_evidence.changes.empty());

    OptimizerInput selected_profile_without_evidence{
        .target_fps = 120,
        .quality = QualityPolicy::performance,
        .profile = Profile::high_performance,
        .profile_preview_requested = true,
    };
    const auto explicit_preview = evaluate(selected_profile_without_evidence);
    CHECK(explicit_preview.bottleneck == Bottleneck::unavailable);
    CHECK(explicit_preview.confidence == Confidence::unavailable);
    CHECK(explicit_preview.reason.find(L"Explicit selected-profile preview") !=
          std::wstring::npos);
    CHECK(explicit_preview.changes.size() == 67);
    CHECK(find_change(explicit_preview, config::SettingId::target_fps) == nullptr);
    CHECK(find_change(explicit_preview,
                      config::SettingId::minimum_smooth_frame_rate) == nullptr);
    CHECK(find_change(explicit_preview,
                      config::SettingId::smooth_frame_rate) == nullptr);
    CHECK(find_change(explicit_preview, config::SettingId::dynamic_shadows) != nullptr);
    CHECK(find_change(explicit_preview,
                      config::SettingId::corpse_collision_with_living) == nullptr);

    selected_profile_without_evidence.profile_preview_requested = false;
    CHECK(evaluate(selected_profile_without_evidence).changes.empty());

    OptimizerInput gpu_bound{
        .target_fps = 120,
        .quality = QualityPolicy::exact,
        .profile = Profile::balanced,
        .evidence = {.fresh = true, .fps = 72.0, .p95_frame_time_ms = 18.0,
                     .cpu_percent = 42.0, .gpu_percent = 97.0},
    };
    auto exact = evaluate(gpu_bound);
    CHECK(exact.bottleneck == Bottleneck::gpu);
    CHECK(exact.confidence == Confidence::high);
    CHECK(find_change(exact, config::SettingId::target_fps) == nullptr);
    CHECK(find_change(exact, config::SettingId::smooth_frame_rate) == nullptr);
    CHECK(find_change(exact, config::SettingId::corpse_limit) == nullptr);
    CHECK(find_change(exact, config::SettingId::gore_effect_limit) == nullptr);

    gpu_bound.quality = QualityPolicy::performance;
    gpu_bound.profile = Profile::high_performance;
    auto performance = evaluate(gpu_bound);
    CHECK(performance.changes.size() == 67);
    for (const auto& change : performance.changes) {
        const auto* definition = config::find_setting(change.id);
        CHECK(definition != nullptr);
        CHECK(definition->adaptive_allowed);
    }
    CHECK(find_change(performance, config::SettingId::corpse_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::gore_effect_limit) != nullptr);
    CHECK(std::get<int>(find_change(performance, config::SettingId::corpse_limit)->value) == 8);
    CHECK(std::get<int>(find_change(performance, config::SettingId::gore_effect_limit)->value) == 8);
    CHECK(find_change(performance, config::SettingId::explosion_decal_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::impact_decal_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::wound_decal_limit) != nullptr);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::wound_decal_limit)->value) == 5);
    CHECK(find_change(performance, config::SettingId::blood_splatter_decal_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::blood_pool_decal_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::blood_effect_limit) != nullptr);
    CHECK(find_change(performance, config::SettingId::body_wound_decal_lifetime) != nullptr);
    CHECK(find_change(performance, config::SettingId::blood_splatter_lifetime) != nullptr);
    CHECK(find_change(performance, config::SettingId::blood_pool_lifetime) != nullptr);
    CHECK(find_change(performance, config::SettingId::giblet_lifetime) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::gore_lifetime_multiplier) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::persistent_splats_per_frame) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::blood_splatter_decals) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::secondary_blood_effects) != nullptr);
    CHECK(find_change(performance, config::SettingId::static_decals) != nullptr);
    CHECK(find_change(performance, config::SettingId::dynamic_decals) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::decal_cull_distance_scale) != nullptr);
    CHECK(find_change(performance, config::SettingId::dynamic_shadows) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::light_environment_shadows) != nullptr);
    CHECK(find_change(performance, config::SettingId::ambient_occlusion) != nullptr);
    CHECK(find_change(performance, config::SettingId::bloom) != nullptr);
    CHECK(find_change(performance, config::SettingId::distortion) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::drop_particle_distortion) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::high_quality_materials) != nullptr);
    CHECK(find_change(performance, config::SettingId::detail_mode) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::max_shadow_resolution) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::max_whole_scene_shadow_resolution) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::shadow_texels_per_pixel) != nullptr);
    CHECK(find_change(performance,
                      config::SettingId::fracture_cull_distance_scale) != nullptr);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::blood_effect_limit)->value) == 15);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::body_wound_decal_lifetime)->value) == 30);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::blood_splatter_lifetime)->value) == 10);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::blood_pool_lifetime)->value) == 20);
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::gore_lifetime_multiplier)->value) == 0.75);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::persistent_splats_per_frame)->value) == 50);
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::blood_splatter_decals)->value));
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::secondary_blood_effects)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::static_decals)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::dynamic_decals)->value));
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::decal_cull_distance_scale)->value) == 0.5);
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::dynamic_shadows)->value));
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::max_shadow_resolution)->value) == 512);
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::shadow_texels_per_pixel)->value) == 0.9);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::particle_lod_bias)->value) == 0);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::skeletal_mesh_lod_bias)->value) == 1);
    CHECK(find_change(performance,
                      config::SettingId::max_draw_distance_scale) == nullptr);
    CHECK(find_change(performance,
                      config::SettingId::corpse_collision_with_dead) == nullptr);
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::depth_of_field)->value));
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::light_shafts)->value));
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::fractured_damage)->value));
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::motion_blur)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::post_process_aa)->value));
    CHECK(find_change(performance, config::SettingId::distance_fog) == nullptr);
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::filtered_distortion)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::unbatched_decals)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::whole_scene_dominant_shadows)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::conservative_shadow_bounds)->value));
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::global_shadow_distance_scale)->value) == 0.75);
    CHECK(std::get<int>(find_change(performance,
              config::SettingId::max_anisotropy)->value) == 1);
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::emitter_pool_scale)->value) == 0.5);
    CHECK(std::get<double>(find_change(performance,
              config::SettingId::shell_eject_lifetime)->value) == 5.0);
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::spray_actor_lights)->value));
    CHECK(std::get<bool>(find_change(performance,
              config::SettingId::pilot_lights)->value));
    CHECK(!std::get<bool>(find_change(performance,
              config::SettingId::override_map_whole_scene_shadow)->value));
    CHECK(find_change(performance, config::SettingId::footstep_sounds) == nullptr);
    CHECK(find_change(performance, config::SettingId::always_on_physics) == nullptr);

    auto stability_input = gpu_bound;
    stability_input.profile = Profile::stability;
    auto stability = evaluate(stability_input);
    CHECK(stability.changes.size() == 67);
    CHECK(std::get<int>(find_change(stability,
              config::SettingId::corpse_limit)->value) == 15);
    CHECK(std::get<bool>(find_change(stability,
              config::SettingId::blood_splatter_decals)->value));
    CHECK(std::get<bool>(find_change(stability,
              config::SettingId::secondary_blood_effects)->value));
    CHECK(std::get<bool>(find_change(stability,
              config::SettingId::dynamic_shadows)->value));
    CHECK(std::get<int>(find_change(stability,
              config::SettingId::max_shadow_resolution)->value) == 2048);
    CHECK(std::get<int>(find_change(stability,
              config::SettingId::detail_mode)->value) == 2);
    CHECK(std::get<int>(find_change(stability,
              config::SettingId::max_anisotropy)->value) == 16);
    CHECK(std::get<bool>(find_change(stability,
              config::SettingId::screen_space_reflections)->value));

    auto balanced_input = gpu_bound;
    balanced_input.profile = Profile::balanced;
    auto balanced = evaluate(balanced_input);
    CHECK(balanced.changes.size() == 67);
    CHECK(std::get<int>(find_change(balanced,
              config::SettingId::corpse_limit)->value) == 12);
    CHECK(std::get<bool>(find_change(balanced,
              config::SettingId::dynamic_shadows)->value));
    CHECK(std::get<int>(find_change(balanced,
              config::SettingId::detail_mode)->value) == 1);
    CHECK(std::get<int>(find_change(balanced,
              config::SettingId::max_shadow_resolution)->value) == 1024);

    // Moving away from High Performance must explicitly restore every value
    // owned by the next profile instead of retaining a mixed profile.
    for (const auto& high_performance_change : performance.changes) {
        CHECK(find_change(balanced, high_performance_change.id) != nullptr);
        CHECK(find_change(stability, high_performance_change.id) != nullptr);
    }

    auto custom_input = gpu_bound;
    custom_input.profile = Profile::custom;
    auto custom = evaluate(custom_input);
    CHECK(custom.changes.empty());
    CHECK(find_change(custom, config::SettingId::corpse_limit) == nullptr);
    CHECK(find_change(custom, config::SettingId::dynamic_shadows) == nullptr);

    gpu_bound.quality = QualityPolicy::invisible;
    auto invisible = evaluate(gpu_bound);
    CHECK(find_change(invisible, config::SettingId::corpse_limit) == nullptr);
    CHECK(find_change(invisible, config::SettingId::gore_effect_limit) == nullptr);
    CHECK(find_change(invisible, config::SettingId::blood_effect_limit) == nullptr);
    CHECK(find_change(invisible,
                      config::SettingId::body_wound_decal_lifetime) == nullptr);
    CHECK(find_change(invisible,
                      config::SettingId::gore_lifetime_multiplier) == nullptr);
    CHECK(find_change(invisible, config::SettingId::dynamic_shadows) == nullptr);
    CHECK(find_change(invisible,
                      config::SettingId::drop_particle_distortion) == nullptr);

    OptimizerInput capped = gpu_bound;
    capped.target_fps = 60;
    capped.evidence = {.fresh = true, .fps = 59.8, .p95_frame_time_ms = 16.9,
                       .cpu_percent = 30.0, .gpu_percent = 45.0};
    auto cap = evaluate(capped);
    CHECK(cap.bottleneck == Bottleneck::frame_cap);

    capped.quality = QualityPolicy::performance;
    capped.profile = Profile::high_performance;
    capped.profile_preview_requested = true;
    auto explicit_cap_profile = evaluate(capped);
    CHECK(find_change(explicit_cap_profile,
                      config::SettingId::dynamic_shadows) != nullptr);

    OptimizerInput cpu_bound = gpu_bound;
    cpu_bound.evidence = {.fresh = true, .fps = 70.0, .p95_frame_time_ms = 22.0,
                          .cpu_percent = 92.0, .gpu_percent = 55.0};
    CHECK(evaluate(cpu_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput critical_thread_bound = gpu_bound;
    critical_thread_bound.evidence = {
        .fresh = true, .fps = 45.0, .p95_frame_time_ms = 28.0,
        .cpu_percent = 5.0, .critical_core_percent = 98.0,
        .gpu_percent = 16.0};
    CHECK(evaluate(critical_thread_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput measured_main_thread_bound = gpu_bound;
    measured_main_thread_bound.evidence = {
        .fresh = true, .fps = 45.0, .p95_frame_time_ms = 28.0,
        .cpu_percent = 5.0, .critical_core_percent = 80.0,
        .effective_core_usage = 1.5,
        .dominant_thread_share_percent = 54.0,
        .active_cpu_threads = 7,
        .affinity_logical_processors = 16,
        .affinity_physical_cores = 8,
        .system_logical_processors = 32,
        .gpu_percent = 16.0};
    CHECK(evaluate(measured_main_thread_bound).bottleneck == Bottleneck::cpu);

    OptimizerInput vram_bound = gpu_bound;
    vram_bound.evidence = {.fresh = true, .fps = 70.0, .p95_frame_time_ms = 25.0,
                           .cpu_percent = 55.0, .gpu_percent = 85.0,
                           .dedicated_vram_bytes = 7'900,
                           .dedicated_vram_budget_bytes = 8'000};
    CHECK(evaluate(vram_bound).bottleneck == Bottleneck::vram_streaming);

    OptimizerInput ram_bound = gpu_bound;
    ram_bound.evidence = {.fresh = true, .fps = 70.0,
                          .p95_frame_time_ms = 25.0,
                          .cpu_percent = 40.0, .gpu_percent = 60.0,
                          .system_ram_used_bytes = 7'500,
                          .system_ram_budget_bytes = 8'000};
    CHECK(evaluate(ram_bound).bottleneck == Bottleneck::ram_pressure);

    gpu_bound.target_fps = 10;
    auto bounded = evaluate(gpu_bound);
    CHECK(find_change(bounded, config::SettingId::target_fps) == nullptr);
    return EXIT_SUCCESS;
}
