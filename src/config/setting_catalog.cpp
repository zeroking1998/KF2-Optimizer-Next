#include "kf2/config/setting_catalog.hpp"
#include "kf2/config/ini_document.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace kf2::config {
namespace {

Result<std::string> read_bounded_config_file(
    const std::filesystem::path& path, std::uintmax_t maximum_size) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found, L"Required KF2 configuration file is missing",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &information) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 ||
        static_cast<std::uintmax_t>(size.QuadPart) > maximum_size) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"KF2 configuration file identity or size is unsafe", native});
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, MAXDWORD));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) ||
            read == 0) {
            const DWORD native = GetLastError();
            CloseHandle(file);
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"KF2 configuration file cannot be read", native});
        }
        offset += read;
    }
    CloseHandle(file);
    return Result<std::string>::success(std::move(bytes));
}

}  // namespace

std::span<const SettingCategory> all_setting_categories() noexcept {
    static constexpr std::array categories{
        SettingCategory::frame_pacing_cpu,
        SettingCategory::gore_decals,
        SettingCategory::rendering_effects,
        SettingCategory::shadows_lighting,
        SettingCategory::lod_distance,
        SettingCategory::physics_flex,
    };
    return categories;
}

SettingCategory setting_category(SettingId id) noexcept {
    switch (id) {
        case SettingId::target_fps:
        case SettingId::minimum_smooth_frame_rate:
        case SettingId::smooth_frame_rate:
        case SettingId::vertical_sync:
        case SettingId::one_frame_thread_lag:
        case SettingId::per_frame_sleep:
        case SettingId::per_frame_yield:
        case SettingId::force_affinity:
            return SettingCategory::frame_pacing_cpu;

        case SettingId::corpse_limit:
        case SettingId::gore_effect_limit:
        case SettingId::explosion_decal_limit:
        case SettingId::impact_decal_limit:
        case SettingId::wound_decal_limit:
        case SettingId::blood_splatter_decal_limit:
        case SettingId::blood_pool_decal_limit:
        case SettingId::blood_effect_limit:
        case SettingId::body_wound_decal_lifetime:
        case SettingId::blood_splatter_lifetime:
        case SettingId::blood_pool_lifetime:
        case SettingId::giblet_lifetime:
        case SettingId::gore_lifetime_multiplier:
        case SettingId::persistent_splats_per_frame:
        case SettingId::blood_splatter_decals:
        case SettingId::secondary_blood_effects:
        case SettingId::static_decals:
        case SettingId::dynamic_decals:
        case SettingId::decal_cull_distance_scale:
        case SettingId::ragdoll_and_gore_on_dead_bodies:
        case SettingId::fractured_damage:
        case SettingId::unbatched_decals:
        case SettingId::persistent_splats:
        case SettingId::destruction_lifetime_scale:
        case SettingId::explosion_lights:
        case SettingId::blood_splat_size:
        case SettingId::blood_pool_size:
        case SettingId::persistent_splat_trace_length:
        case SettingId::fractured_parts_scale:
        case SettingId::fracture_direct_spawn_chance:
        case SettingId::fracture_radial_spawn_chance:
        case SettingId::gore_level:
        case SettingId::fractured_mesh_weapon_damage:
        case SettingId::decal_lifetime:
            return SettingCategory::gore_decals;

        case SettingId::ambient_occlusion:
        case SettingId::bloom:
        case SettingId::distortion:
        case SettingId::drop_particle_distortion:
        case SettingId::high_quality_materials:
        case SettingId::detail_mode:
        case SettingId::depth_of_field:
        case SettingId::light_shafts:
        case SettingId::lens_flares:
        case SettingId::radial_blur:
        case SettingId::separate_translucency:
        case SettingId::motion_blur:
        case SettingId::post_process_aa:
        case SettingId::distance_fog:
        case SettingId::filtered_distortion:
        case SettingId::max_anisotropy:
        case SettingId::post_process_mlaa:
        case SettingId::screen_space_reflections:
        case SettingId::variable_blur_reflections:
        case SettingId::subsurface_scattering:
        case SettingId::depth_prepass:
        case SettingId::reflection_downsample_factor:
        case SettingId::image_grain_scale:
        case SettingId::depth_of_field_quality:
        case SettingId::bloom_quality:
        case SettingId::distance_fog_quality:
        case SettingId::volumetric_lighting_mode:
        case SettingId::screen_percentage:
        case SettingId::motion_blur_quality:
        case SettingId::compute_bloom:
        case SettingId::compute_depth_of_field:
        case SettingId::compute_motion_blur:
        case SettingId::compute_ssao:
        case SettingId::compute_ssr:
        case SettingId::hbao:
        case SettingId::new_depth_of_field:
        case SettingId::image_reflections:
        case SettingId::image_reflection_shadowing:
        case SettingId::downsampled_translucency:
        case SettingId::max_filter_blur_samples:
        case SettingId::motion_blur_static_scale:
        case SettingId::motion_blur_dynamic_scale:
        case SettingId::override_weapon_depth_of_field:
        case SettingId::motion_blur_pause:
        case SettingId::motion_blur_skinning:
        case SettingId::fog_volumes:
        case SettingId::floating_point_render_targets:
        case SettingId::only_stream_in_textures:
        case SettingId::texture_streaming:
        case SettingId::texture_pool_size:
        case SettingId::texture_streaming_memory_margin:
        case SettingId::texture_streaming_hysteresis_limit:
        case SettingId::minimum_texture_resident_mips:
        case SettingId::texture_async_defrag:
        case SettingId::texture_async_reallocation:
        case SettingId::boost_player_textures:
        case SettingId::background_level_streaming:
        case SettingId::max_occlusion_pixels_fraction:
        case SettingId::check_particle_render_size:
        case SettingId::max_tracked_occlusion_increment:
        case SettingId::tracked_occlusion_step_size:
        case SettingId::streaming_pause:
        case SettingId::stop_streaming_limit:
        case SettingId::lightmap_streaming_factor:
        case SettingId::shadowmap_streaming_factor:
        case SettingId::streaming_lightmaps:
        case SettingId::priority_streaming:
        case SettingId::switching_streaming_system:
        case SettingId::dynamic_streaming:
        case SettingId::shell_eject_lifetime:
        case SettingId::spray_actor_lights:
        case SettingId::pilot_lights:
        case SettingId::footstep_sounds:
        case SettingId::upscale_screen_percentage:
        case SettingId::temporal_aa:
        case SettingId::max_multi_samples:
        case SettingId::high_precision_gbuffers:
        case SettingId::tessellation_pixels_per_triangle:
        case SettingId::scene_capture_streaming_multiplier:
        case SettingId::instanced_rendering:
        case SettingId::summed_area_table_compute:
        case SettingId::histogram_techniques:
        case SettingId::unverified_effect_profile:
            return SettingCategory::rendering_effects;

        case SettingId::dynamic_shadows:
        case SettingId::light_environment_shadows:
        case SettingId::max_shadow_resolution:
        case SettingId::max_whole_scene_shadow_resolution:
        case SettingId::shadow_texels_per_pixel:
        case SettingId::global_shadow_distance_scale:
        case SettingId::whole_scene_dominant_shadows:
        case SettingId::conservative_shadow_bounds:
        case SettingId::light_functions:
        case SettingId::light_cones:
        case SettingId::whole_scene_shadow_cutoff_distance:
        case SettingId::whole_scene_shadow_fade_distance:
        case SettingId::grouped_per_object_shadows:
        case SettingId::grouped_shadow_min_radius:
        case SettingId::grouped_shadow_max_radius:
        case SettingId::grouped_shadow_ramp_up_factor:
        case SettingId::grouped_shadow_ramp_cutoff:
        case SettingId::max_overlapping_lights:
        case SettingId::light_occlusion_queries:
        case SettingId::per_object_shadows:
        case SettingId::hardware_shadow_filtering:
        case SettingId::foreground_shadows_on_world:
        case SettingId::min_shadow_resolution:
        case SettingId::shadow_fade_resolution:
        case SettingId::shadow_depth_bias:
        case SettingId::boolean_preshadows:
        case SettingId::foreground_preshadows:
        case SettingId::max_per_object_shadow_bounds:
        case SettingId::foreground_projection_depth_bias:
        case SettingId::dynamic_lights:
        case SettingId::composite_dynamic_lights:
        case SettingId::secondary_h_lighting:
        case SettingId::directional_lightmaps:
        case SettingId::shadow_filter_quality_bias:
        case SettingId::min_pre_shadow_resolution:
        case SettingId::pre_shadow_fade_resolution:
        case SettingId::pre_shadow_resolution_factor:
        case SettingId::shadow_fade_exponent:
        case SettingId::shadow_filter_radius:
        case SettingId::per_object_shadow_transition:
        case SettingId::per_scene_shadow_transition:
        case SettingId::branching_pcf_shadows:
        case SettingId::foreground_self_shadowing:
        case SettingId::csm_split_penumbra_scale:
        case SettingId::csm_split_soft_transition_scale:
        case SettingId::csm_split_depth_bias_scale:
        case SettingId::csm_minimum_fov:
        case SettingId::csm_fov_round_factor:
        case SettingId::override_map_whole_scene_shadow:
            return SettingCategory::shadows_lighting;

        case SettingId::fracture_cull_distance_scale:
        case SettingId::particle_lod_bias:
        case SettingId::skeletal_mesh_lod_bias:
        case SettingId::max_draw_distance_scale:
            return SettingCategory::lod_distance;

        case SettingId::corpse_collision_with_dead:
        case SettingId::corpse_collision_with_living:
        case SettingId::corpse_collision_after_sleep:
        case SettingId::physx_level:
        case SettingId::physics_async_scene:
        case SettingId::enable_async_scene:
        case SettingId::flex_invisible_frames_before_sleep:
        case SettingId::flex_distance_before_sleep:
        case SettingId::sph_fluid_mipmap:
        case SettingId::flex_rigid_collision_high_level:
        case SettingId::apex_destruction_chunk_islands:
        case SettingId::apex_destruction_shape_count:
        case SettingId::apex_destruction_chunk_separation_lod:
        case SettingId::apex_destruction_actor_creates_per_frame:
        case SettingId::apex_destruction_fractures_per_frame:
        case SettingId::apex_destruction_sort_by_benefit:
        case SettingId::apex_clothing_frequency_window:
        case SettingId::apex_clothing_async_cooking:
        case SettingId::apex_clothing_between_substeps:
        case SettingId::apex_clothing_async_fetch:
        case SettingId::disable_dynamic_wakeup:
        case SettingId::dynamic_collision_threshold:
        case SettingId::max_physics_substeps:
        case SettingId::emitter_pool_scale:
        case SettingId::physics_chunk_override_chance:
        case SettingId::physics_chunk_override_enabled:
        case SettingId::fracture_explosion_velocity_scale:
        case SettingId::limit_explosion_chunk_size:
        case SettingId::limit_damage_chunk_size:
        case SettingId::max_explosion_chunk_size:
        case SettingId::max_damage_chunk_size:
        case SettingId::max_particle_vertex_memory:
        case SettingId::max_particle_resize:
        case SettingId::max_particle_resize_warning:
        case SettingId::particle_percentage:
        case SettingId::rigid_body_gravity_scale:
        case SettingId::kinematic_update_distance_scale:
        case SettingId::always_on_physics:
        case SettingId::apex_write_buffer_task:
        case SettingId::apex_render_resources_game_thread:
            return SettingCategory::physics_flex;
    }
    return SettingCategory::rendering_effects;
}

std::wstring_view setting_category_label(SettingCategory category) noexcept {
    switch (category) {
        case SettingCategory::frame_pacing_cpu: return L"Frame pacing & CPU";
        case SettingCategory::gore_decals: return L"Gore & decals";
        case SettingCategory::rendering_effects: return L"Rendering & effects";
        case SettingCategory::shadows_lighting: return L"Shadows & lighting";
        case SettingCategory::lod_distance: return L"LOD & distance";
        case SettingCategory::physics_flex: return L"Physics & FleX";
    }
    return L"Verified settings";
}

std::wstring_view setting_category_label(SettingId id) noexcept {
    return setting_category_label(setting_category(id));
}

std::string setting_token(SettingId id) {
    const auto* definition = find_setting(id);
    if (!definition) return {};
    std::string result;
    result.reserve(definition->key.size());
    for (const wchar_t character : definition->key) {
        if (character > 0x7F) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

const SettingDefinition* find_setting_by_token(std::string_view token) noexcept {
    if (token.empty() || token.size() > 96) return nullptr;
    for (const auto& definition : all_settings()) {
        if (setting_token(definition.id) == token) return &definition;
    }
    return nullptr;
}

Result<std::map<SettingId, SettingValue>> read_catalog_values(
    const std::filesystem::path& config_root) {
    std::map<std::filesystem::path, IniDocument> documents;
    std::map<SettingId, SettingValue> values;
    for (const auto& definition : all_settings()) {
        auto document = documents.find(definition.relative_path);
        if (document == documents.end()) {
            const auto path = config_root / definition.relative_path;
            const auto bytes = read_bounded_config_file(path, 4 * 1024 * 1024);
            if (!bytes.has_value()) {
                return Result<std::map<SettingId, SettingValue>>::failure(
                    bytes.error());
            }
            auto parsed = IniDocument::parse(bytes.value());
            if (!parsed.has_value()) {
                return Result<std::map<SettingId, SettingValue>>::failure(
                    parsed.error());
            }
            document = documents.emplace(definition.relative_path,
                                          std::move(parsed.value())).first;
        }
        const auto text = document->second.find(definition.section, definition.key);
        if (!text && definition.insert_if_missing) continue;
        const auto value = text
            ? parse_setting_value(definition, *text) : std::nullopt;
        if (!value) {
            return Result<std::map<SettingId, SettingValue>>::failure(
                {ErrorCode::invalid_argument,
                 L"A verified catalog value is missing or outside its safe range", 0});
        }
        values.emplace(definition.id, *value);
    }
    return Result<std::map<SettingId, SettingValue>>::success(std::move(values));
}

}  // namespace kf2::config
