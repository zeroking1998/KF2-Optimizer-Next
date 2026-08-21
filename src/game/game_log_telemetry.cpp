#include "game_log_session_internal.hpp"

#include <charconv>

namespace kf2::game {
namespace {

struct OfflineTelemetrySnapshot {
    int sample{0};
    int living{0};
    int living_classes{0};
    int living_bosses{0};
    int living_visible{0};
    int living_offscreen{0};
    int living_lod_total{0};
    int living_anim_rate_total{0};
    int living_injured_zones{0};
    int living_required_bones{0};
    int living_material_slots{0};
    int living_attachments{0};
    int living_anim_skipped{0};
    int living_bone_atoms_skipped{0};
    int living_bone_interpolation{0};
    int living_kinematic_distance_skipped{0};
    int living_ticks_offscreen{0};
    int living_special_moves{0};
    int living_attack_moves{0};
    int living_grapple_moves{0};
    int living_stumbles{0};
    int living_knockdowns{0};
    int living_hit_reactions{0};
    int living_other_special_moves{0};
    int corpse_total{0};
    int corpse_awake{0};
    int corpse_sleeping{0};
    int corpse_other{0};
    int corpse_final{0};
    int corpse_visible{0};
    int corpse_offscreen{0};
    int corpse_lod_total{0};
    int corpse_injured_zones{0};
    int corpse_max_age_ms{0};
    int corpse_limit{0};
    int corpse_offscreen_time_ms{0};
    int corpse_offscreen_distance{0};
    int dismembered{0};
    int dismembered_limbs{0};
    int ragdoll_warned{0};
    int ragdoll_warning_max{0};
    bool corpse_collide_dead{false};
    bool corpse_collide_living{false};
    bool corpse_collide_dead_after_sleep{false};
    bool corpse_collide_living_after_sleep{false};
    int gibs{0};
    int spray_actors{0};
    int fire_spray_actors{0};
    int toxic_spray_actors{0};
    int other_spray_actors{0};
    int explosion_actors{0};
    int damaging_explosion_actors{0};
    int fire_explosion_actors{0};
    int toxic_explosion_actors{0};
    int other_damaging_explosion_actors{0};
    int unclassified_explosion_actors{0};
    int lingering_explosion_actors{0};
    int smoke_explosion_actors{0};
    int bloat_king_fart_explosion_actors{0};
    int smoke_grenade_projectiles{0};
    int puke_mine_projectiles{0};
    int bloat_king_puke_mine_projectiles{0};
    int wound_decals{0};
    int splatter_decals{0};
    int pool_decals{0};
    int impact_decals{0};
    int explosion_decals{0};
    int wound_decal_limit{0};
    int splatter_decal_limit{0};
    int pool_decal_limit{0};
    int impact_decal_limit{0};
    int explosion_decal_limit{0};
    int blood_effect_limit{0};
    int gore_effect_limit{0};
    int wound_lifetime_ms{0};
    int splatter_lifetime_ms{0};
    int pool_lifetime_ms{0};
    int gib_lifetime_ms{0};
    int gore_particle_components{0};
    int gore_particles{0};
    int gore_particle_visible_components{0};
    int gore_particle_lod_total{0};
    int gore_particle_bounded_components{0};
    int world_particle_components{0};
    int world_particles{0};
    int world_particle_visible_components{0};
    int world_particle_lod_total{0};
    int world_particle_bounded_components{0};
    int ground_fire_particle_components{0};
    int ground_fire_particles{0};
    int impact_particle_components{0};
    int impact_particles{0};
    int gore_particle_pool_capacity{0};
    int world_particle_pool_capacity{0};
    int ground_fire_particle_pool_capacity{0};
    int impact_particle_pool_capacity{0};
    int particle_constant_spawn_emitters{0};
    int particle_dynamic_spawn_emitters{0};
    int particle_constant_spawn_rate_milli{0};
    int particle_burst_entries{0};
    int particle_peak_capacity{0};
    int particle_flex_components{0};
    int particle_flex_fluid_components{0};
    int particle_flex_nonfluid_components{0};
    int particle_flex_mixed_components{0};
    int particle_nonflex_components{0};
    int particle_unclassified_components{0};
    bool flex_surrogate_active{false};
    int flex_surrogate_particles{0};
    bool flex_surrogate_visible{false};
    int flex_surrogate_lod{0};
    bool zed_time{false};
};

std::optional<OfflineTelemetrySnapshot> parse_offline_telemetry_line(
    std::string_view line) {
    constexpr std::string_view marker =
        "KF2OPT_TELEMETRY schema=6 sample=";
    const auto marker_position = line.find(marker);
    if (marker_position == std::string_view::npos) return std::nullopt;
    auto payload = line.substr(marker_position + marker.size());
    const auto take = [&](std::string_view next,
                          int maximum) -> std::optional<int> {
        const auto end = next.empty() ? std::string_view::npos
                                      : payload.find(next);
        if (!next.empty() && end == std::string_view::npos) {
            return std::nullopt;
        }
        const auto text = payload.substr(
            0, end == std::string_view::npos ? payload.size() : end);
        int value = 0;
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), value);
        if (text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + text.size() || value < 0 ||
            value > maximum) {
            return std::nullopt;
        }
        if (!next.empty()) payload.remove_prefix(end + next.size());
        return value;
    };

    OfflineTelemetrySnapshot result;
    constexpr int entity_max = 100'000;
    constexpr int aggregate_max = 100'000'000;
    constexpr int runtime_max = 1'000'000'000;
    const auto sample = take(" living=", std::numeric_limits<int>::max());
    const auto living = take(" living_classes=", entity_max);
    const auto living_classes = take(" living_bosses=", entity_max);
    const auto living_bosses = take(" living_visible=", entity_max);
    const auto living_visible = take(" living_offscreen=", entity_max);
    const auto living_offscreen = take(" living_lod_total=", entity_max);
    const auto living_lod_total = take(" living_anim_rate_total=", aggregate_max);
    const auto living_anim_rate_total = take(" living_injured_zones=", aggregate_max);
    const auto living_injured_zones = take(" living_required_bones=", aggregate_max);
    const auto living_required_bones = take(" living_material_slots=", aggregate_max);
    const auto living_material_slots = take(" living_attachments=", aggregate_max);
    const auto living_attachments = take(" living_anim_skipped=", aggregate_max);
    const auto living_anim_skipped = take(" living_bone_atoms_skipped=", entity_max);
    const auto living_bone_atoms_skipped = take(" living_bone_interpolation=", entity_max);
    const auto living_bone_interpolation = take(" living_kinematic_distance_skipped=", entity_max);
    const auto living_kinematic_distance_skipped = take(" living_ticks_offscreen=", entity_max);
    const auto living_ticks_offscreen = take(" living_special_moves=", entity_max);
    const auto living_special_moves = take(" living_attack_moves=", entity_max);
    const auto living_attack_moves = take(" living_grapple_moves=", entity_max);
    const auto living_grapple_moves = take(" living_stumbles=", entity_max);
    const auto living_stumbles = take(" living_knockdowns=", entity_max);
    const auto living_knockdowns = take(" living_hit_reactions=", entity_max);
    const auto living_hit_reactions = take(
        " living_other_special_moves=", entity_max);
    const auto living_other_special_moves = take(" corpse_total=", entity_max);
    const auto total = take(" corpse_awake=", 100'000);
    const auto awake = take(" corpse_sleeping=", 100'000);
    const auto sleeping = take(" corpse_other=", 100'000);
    const auto other = take(" corpse_final=", 100'000);
    const auto final_pose = take(" corpse_visible=", entity_max);
    const auto corpse_visible = take(" corpse_offscreen=", entity_max);
    const auto corpse_offscreen = take(" corpse_lod_total=", entity_max);
    const auto corpse_lod_total = take(" corpse_injured_zones=", aggregate_max);
    const auto corpse_injured_zones = take(" corpse_max_age_ms=", aggregate_max);
    const auto corpse_max_age_ms = take(" corpse_limit=", runtime_max);
    const auto corpse_limit = take(" corpse_offscreen_time_ms=", runtime_max);
    const auto corpse_offscreen_time_ms = take(" corpse_offscreen_distance=", runtime_max);
    const auto corpse_offscreen_distance = take(" dismembered=", runtime_max);
    const auto dismembered = take(" dismembered_limbs=", 100'000);
    const auto dismembered_limbs = take(" ragdoll_warned=", aggregate_max);
    const auto ragdoll_warned = take(" ragdoll_warning_max=", entity_max);
    const auto ragdoll_warning_max = take(" corpse_collide_dead=", 255);
    const auto corpse_collide_dead = take(" corpse_collide_living=", 1);
    const auto corpse_collide_living =
        take(" corpse_collide_dead_after_sleep=", 1);
    const auto corpse_collide_dead_after_sleep =
        take(" corpse_collide_living_after_sleep=", 1);
    const auto corpse_collide_living_after_sleep = take(" gibs=", 1);
    const auto gibs = take(" zed_time=", 100'000);
    const auto zed_time = take(" spray_actors=", 1);
    const auto spray_actors = take(" fire_spray_actors=", entity_max);
    const auto fire_spray_actors = take(" toxic_spray_actors=", entity_max);
    const auto toxic_spray_actors = take(" other_spray_actors=", entity_max);
    const auto other_spray_actors = take(" explosion_actors=", entity_max);
    const auto explosion_actors = take(" damaging_explosion_actors=", entity_max);
    const auto damaging_explosion_actors = take(
        " fire_explosion_actors=", entity_max);
    const auto fire_explosion_actors = take(
        " toxic_explosion_actors=", entity_max);
    const auto toxic_explosion_actors = take(
        " other_damaging_explosion_actors=", entity_max);
    const auto other_damaging_explosion_actors = take(
        " unclassified_explosion_actors=", entity_max);
    const auto unclassified_explosion_actors = take(" lingering_explosion_actors=", entity_max);
    const auto lingering_explosion_actors = take(
        " smoke_explosion_actors=", entity_max);
    const auto smoke_explosion_actors = take(
        " bloat_king_fart_explosion_actors=", entity_max);
    const auto bloat_king_fart_explosion_actors = take(
        " smoke_grenade_projectiles=", entity_max);
    const auto smoke_grenade_projectiles = take(
        " puke_mine_projectiles=", entity_max);
    const auto puke_mine_projectiles = take(
        " bloat_king_puke_mine_projectiles=", entity_max);
    const auto bloat_king_puke_mine_projectiles = take(
        " wound_decals=", entity_max);
    const auto wound_decals = take(" splatter_decals=", 100'000);
    const auto splatter_decals = take(" pool_decals=", 100'000);
    const auto pool_decals = take(" impact_decals=", entity_max);
    const auto impact_decals = take(" explosion_decals=", entity_max);
    const auto explosion_decals = take(" wound_decal_limit=", entity_max);
    const auto wound_decal_limit = take(" splatter_decal_limit=", runtime_max);
    const auto splatter_decal_limit = take(" pool_decal_limit=", runtime_max);
    const auto pool_decal_limit = take(" impact_decal_limit=", runtime_max);
    const auto impact_decal_limit = take(" explosion_decal_limit=", runtime_max);
    const auto explosion_decal_limit = take(" blood_effect_limit=", runtime_max);
    const auto blood_effect_limit = take(" gore_effect_limit=", runtime_max);
    const auto gore_effect_limit = take(" wound_lifetime_ms=", runtime_max);
    const auto wound_lifetime_ms = take(" splatter_lifetime_ms=", runtime_max);
    const auto splatter_lifetime_ms = take(" pool_lifetime_ms=", runtime_max);
    const auto pool_lifetime_ms = take(" gib_lifetime_ms=", runtime_max);
    const auto gib_lifetime_ms = take(" gore_particle_components=", runtime_max);
    const auto gore_components = take(" gore_particles=", entity_max);
    const auto gore_particles = take(" gore_particle_visible_components=", aggregate_max);
    const auto gore_visible = take(" gore_particle_lod_total=", entity_max);
    const auto gore_lod_total = take(" gore_particle_bounded_components=", aggregate_max);
    const auto gore_bounded = take(" world_particle_components=", entity_max);
    const auto world_components = take(" world_particles=", entity_max);
    const auto world_particles = take(" world_particle_visible_components=", aggregate_max);
    const auto world_visible = take(" world_particle_lod_total=", entity_max);
    const auto world_lod_total = take(" world_particle_bounded_components=", aggregate_max);
    const auto world_bounded = take(" ground_fire_particle_components=", entity_max);
    const auto ground_fire_components = take(" ground_fire_particles=", entity_max);
    const auto ground_fire_particles = take(" impact_particle_components=", aggregate_max);
    const auto impact_components = take(" impact_particles=", entity_max);
    const auto impact_particles = take(" gore_particle_pool_capacity=", aggregate_max);
    const auto gore_pool_capacity = take(
        " world_particle_pool_capacity=", runtime_max);
    const auto world_pool_capacity = take(
        " ground_fire_particle_pool_capacity=", runtime_max);
    const auto ground_fire_pool_capacity = take(
        " impact_particle_pool_capacity=", runtime_max);
    const auto impact_pool_capacity = take(
        " particle_constant_spawn_emitters=", runtime_max);
    const auto constant_spawn_emitters = take(
        " particle_dynamic_spawn_emitters=", aggregate_max);
    const auto dynamic_spawn_emitters = take(
        " particle_constant_spawn_rate_milli=", aggregate_max);
    const auto constant_spawn_rate_milli = take(
        " particle_burst_entries=", runtime_max);
    const auto burst_entries = take(
        " particle_peak_capacity=", runtime_max);
    const auto peak_capacity = take(
        " particle_flex_components=", runtime_max);
    const auto particle_flex = take(" particle_flex_fluid_components=", entity_max);
    const auto particle_flex_fluid = take(" particle_flex_nonfluid_components=", entity_max);
    const auto particle_flex_nonfluid = take(" particle_flex_mixed_components=", entity_max);
    const auto particle_flex_mixed = take(" particle_nonflex_components=", entity_max);
    const auto particle_nonflex = take(" particle_unclassified_components=", entity_max);
    const auto particle_unclassified = take(" flex_surrogate_active=", entity_max);
    const auto flex_active = take(" flex_surrogate_particles=", 1);
    const auto flex_particles = take(" flex_surrogate_visible=", aggregate_max);
    const auto flex_visible = take(" flex_surrogate_lod=", 1);
    const auto flex_lod = take({}, entity_max);
    if (!sample || *sample <= 0 || !living || !total || !awake || !sleeping ||
        !living_classes || !living_bosses || !living_visible ||
        !living_offscreen || !living_lod_total || !living_anim_rate_total ||
        !living_injured_zones || !living_required_bones ||
        !living_material_slots || !living_attachments || !living_anim_skipped ||
        !living_bone_atoms_skipped || !living_bone_interpolation ||
        !living_kinematic_distance_skipped || !living_ticks_offscreen ||
        !living_special_moves || !living_attack_moves ||
        !living_grapple_moves || !living_stumbles || !living_knockdowns ||
        !living_hit_reactions || !living_other_special_moves ||
        !other || !final_pose || !corpse_visible ||
        !corpse_offscreen || !corpse_lod_total || !corpse_injured_zones ||
        !corpse_max_age_ms || !corpse_limit || !corpse_offscreen_time_ms ||
        !corpse_offscreen_distance || !dismembered || !dismembered_limbs ||
        !ragdoll_warned || !ragdoll_warning_max || !corpse_collide_dead ||
        !corpse_collide_living || !corpse_collide_dead_after_sleep ||
        !corpse_collide_living_after_sleep || !gibs || !zed_time ||
        !spray_actors || !fire_spray_actors || !toxic_spray_actors ||
        !other_spray_actors || !explosion_actors ||
        !damaging_explosion_actors || !fire_explosion_actors ||
        !toxic_explosion_actors || !other_damaging_explosion_actors ||
        !unclassified_explosion_actors || !lingering_explosion_actors ||
        !smoke_explosion_actors || !bloat_king_fart_explosion_actors ||
        !smoke_grenade_projectiles || !puke_mine_projectiles ||
        !bloat_king_puke_mine_projectiles || !wound_decals || !splatter_decals ||
        !pool_decals || !impact_decals || !explosion_decals ||
        !wound_decal_limit || !splatter_decal_limit || !pool_decal_limit ||
        !impact_decal_limit || !explosion_decal_limit ||
        !blood_effect_limit || !gore_effect_limit || !wound_lifetime_ms ||
        !splatter_lifetime_ms || !pool_lifetime_ms || !gib_lifetime_ms ||
        !gore_components || !gore_particles || !gore_visible ||
        !gore_lod_total || !gore_bounded || !world_components ||
        !world_particles || !world_visible || !world_lod_total ||
        !world_bounded || !ground_fire_components || !ground_fire_particles ||
        !impact_components || !impact_particles || !gore_pool_capacity ||
        !world_pool_capacity || !ground_fire_pool_capacity ||
        !impact_pool_capacity || !constant_spawn_emitters ||
        !dynamic_spawn_emitters || !constant_spawn_rate_milli ||
        !burst_entries || !peak_capacity || !particle_flex ||
        !particle_flex_fluid ||
        !particle_flex_nonfluid || !particle_flex_mixed ||
        !particle_nonflex || !particle_unclassified || !flex_active ||
        !flex_particles || !flex_visible ||
        !flex_lod || *living_classes > *living || *living_bosses > *living ||
        *living_anim_skipped > *living ||
        *living_bone_atoms_skipped > *living ||
        *living_bone_interpolation > *living ||
        *living_kinematic_distance_skipped > *living ||
        *living_ticks_offscreen > *living ||
        *living_special_moves > *living ||
        static_cast<std::int64_t>(*living_attack_moves) +
                *living_grapple_moves + *living_stumbles +
                *living_knockdowns + *living_hit_reactions +
                *living_other_special_moves !=
            *living_special_moves ||
        static_cast<std::int64_t>(*living_visible) + *living_offscreen > *living ||
        *awake > *total || *sleeping > *total || *other > *total ||
        *final_pose > *total || *dismembered > *total ||
        *ragdoll_warned > *total ||
        (*ragdoll_warned == 0 && *ragdoll_warning_max != 0) ||
        *corpse_visible > *total || *corpse_offscreen > *total ||
        static_cast<std::int64_t>(*corpse_visible) + *corpse_offscreen > *total ||
        *spray_actors != static_cast<std::int64_t>(*fire_spray_actors) +
            *toxic_spray_actors + *other_spray_actors ||
        *explosion_actors !=
            static_cast<std::int64_t>(*damaging_explosion_actors) +
            *unclassified_explosion_actors ||
        *damaging_explosion_actors !=
            static_cast<std::int64_t>(*fire_explosion_actors) +
            *toxic_explosion_actors + *other_damaging_explosion_actors ||
        *lingering_explosion_actors > *explosion_actors ||
        *smoke_explosion_actors > *explosion_actors ||
        *bloat_king_fart_explosion_actors > *lingering_explosion_actors ||
        *bloat_king_puke_mine_projectiles > *puke_mine_projectiles ||
        *gore_visible > *gore_components || *gore_bounded > *gore_components ||
        *world_visible > *world_components || *world_bounded > *world_components ||
        *ground_fire_components > *world_components ||
        *ground_fire_particles > *world_particles ||
        *impact_components > *world_components ||
        *impact_particles > *world_particles ||
        static_cast<std::int64_t>(*ground_fire_components) +
            *impact_components > *world_components ||
        (*gore_pool_capacity > 0 &&
         *gore_components > *gore_pool_capacity) ||
        (*ground_fire_pool_capacity > 0 &&
         *ground_fire_components > *ground_fire_pool_capacity) ||
        (*impact_pool_capacity > 0 &&
         *impact_components > *impact_pool_capacity) ||
        static_cast<std::int64_t>(*particle_flex_fluid) +
            *particle_flex_nonfluid + *particle_flex_mixed != *particle_flex ||
        static_cast<std::int64_t>(*particle_flex) + *particle_nonflex +
            *particle_unclassified !=
            static_cast<std::int64_t>(*gore_components) + *world_components ||
        static_cast<std::int64_t>(*awake) + *sleeping + *other != *total) {
        return std::nullopt;
    }
    result.sample = *sample;
    result.living = *living;
    result.living_classes = *living_classes;
    result.living_bosses = *living_bosses;
    result.living_visible = *living_visible;
    result.living_offscreen = *living_offscreen;
    result.living_lod_total = *living_lod_total;
    result.living_anim_rate_total = *living_anim_rate_total;
    result.living_injured_zones = *living_injured_zones;
    result.living_required_bones = *living_required_bones;
    result.living_material_slots = *living_material_slots;
    result.living_attachments = *living_attachments;
    result.living_anim_skipped = *living_anim_skipped;
    result.living_bone_atoms_skipped = *living_bone_atoms_skipped;
    result.living_bone_interpolation = *living_bone_interpolation;
    result.living_kinematic_distance_skipped =
        *living_kinematic_distance_skipped;
    result.living_ticks_offscreen = *living_ticks_offscreen;
    result.living_special_moves = *living_special_moves;
    result.living_attack_moves = *living_attack_moves;
    result.living_grapple_moves = *living_grapple_moves;
    result.living_stumbles = *living_stumbles;
    result.living_knockdowns = *living_knockdowns;
    result.living_hit_reactions = *living_hit_reactions;
    result.living_other_special_moves = *living_other_special_moves;
    result.corpse_total = *total;
    result.corpse_awake = *awake;
    result.corpse_sleeping = *sleeping;
    result.corpse_other = *other;
    result.corpse_final = *final_pose;
    result.corpse_visible = *corpse_visible;
    result.corpse_offscreen = *corpse_offscreen;
    result.corpse_lod_total = *corpse_lod_total;
    result.corpse_injured_zones = *corpse_injured_zones;
    result.corpse_max_age_ms = *corpse_max_age_ms;
    result.corpse_limit = *corpse_limit;
    result.corpse_offscreen_time_ms = *corpse_offscreen_time_ms;
    result.corpse_offscreen_distance = *corpse_offscreen_distance;
    result.dismembered = *dismembered;
    result.dismembered_limbs = *dismembered_limbs;
    result.ragdoll_warned = *ragdoll_warned;
    result.ragdoll_warning_max = *ragdoll_warning_max;
    result.corpse_collide_dead = *corpse_collide_dead != 0;
    result.corpse_collide_living = *corpse_collide_living != 0;
    result.corpse_collide_dead_after_sleep =
        *corpse_collide_dead_after_sleep != 0;
    result.corpse_collide_living_after_sleep =
        *corpse_collide_living_after_sleep != 0;
    result.gibs = *gibs;
    result.spray_actors = *spray_actors;
    result.fire_spray_actors = *fire_spray_actors;
    result.toxic_spray_actors = *toxic_spray_actors;
    result.other_spray_actors = *other_spray_actors;
    result.explosion_actors = *explosion_actors;
    result.damaging_explosion_actors = *damaging_explosion_actors;
    result.fire_explosion_actors = *fire_explosion_actors;
    result.toxic_explosion_actors = *toxic_explosion_actors;
    result.other_damaging_explosion_actors =
        *other_damaging_explosion_actors;
    result.unclassified_explosion_actors = *unclassified_explosion_actors;
    result.lingering_explosion_actors = *lingering_explosion_actors;
    result.smoke_explosion_actors = *smoke_explosion_actors;
    result.bloat_king_fart_explosion_actors =
        *bloat_king_fart_explosion_actors;
    result.smoke_grenade_projectiles = *smoke_grenade_projectiles;
    result.puke_mine_projectiles = *puke_mine_projectiles;
    result.bloat_king_puke_mine_projectiles =
        *bloat_king_puke_mine_projectiles;
    result.wound_decals = *wound_decals;
    result.splatter_decals = *splatter_decals;
    result.pool_decals = *pool_decals;
    result.impact_decals = *impact_decals;
    result.explosion_decals = *explosion_decals;
    result.wound_decal_limit = *wound_decal_limit;
    result.splatter_decal_limit = *splatter_decal_limit;
    result.pool_decal_limit = *pool_decal_limit;
    result.impact_decal_limit = *impact_decal_limit;
    result.explosion_decal_limit = *explosion_decal_limit;
    result.blood_effect_limit = *blood_effect_limit;
    result.gore_effect_limit = *gore_effect_limit;
    result.wound_lifetime_ms = *wound_lifetime_ms;
    result.splatter_lifetime_ms = *splatter_lifetime_ms;
    result.pool_lifetime_ms = *pool_lifetime_ms;
    result.gib_lifetime_ms = *gib_lifetime_ms;
    result.gore_particle_components = *gore_components;
    result.gore_particles = *gore_particles;
    result.gore_particle_visible_components = *gore_visible;
    result.gore_particle_lod_total = *gore_lod_total;
    result.gore_particle_bounded_components = *gore_bounded;
    result.world_particle_components = *world_components;
    result.world_particles = *world_particles;
    result.world_particle_visible_components = *world_visible;
    result.world_particle_lod_total = *world_lod_total;
    result.world_particle_bounded_components = *world_bounded;
    result.ground_fire_particle_components = *ground_fire_components;
    result.ground_fire_particles = *ground_fire_particles;
    result.impact_particle_components = *impact_components;
    result.impact_particles = *impact_particles;
    result.gore_particle_pool_capacity = *gore_pool_capacity;
    result.world_particle_pool_capacity = *world_pool_capacity;
    result.ground_fire_particle_pool_capacity = *ground_fire_pool_capacity;
    result.impact_particle_pool_capacity = *impact_pool_capacity;
    result.particle_constant_spawn_emitters = *constant_spawn_emitters;
    result.particle_dynamic_spawn_emitters = *dynamic_spawn_emitters;
    result.particle_constant_spawn_rate_milli = *constant_spawn_rate_milli;
    result.particle_burst_entries = *burst_entries;
    result.particle_peak_capacity = *peak_capacity;
    result.particle_flex_components = *particle_flex;
    result.particle_flex_fluid_components = *particle_flex_fluid;
    result.particle_flex_nonfluid_components = *particle_flex_nonfluid;
    result.particle_flex_mixed_components = *particle_flex_mixed;
    result.particle_nonflex_components = *particle_nonflex;
    result.particle_unclassified_components = *particle_unclassified;
    result.flex_surrogate_active = *flex_active != 0;
    result.flex_surrogate_particles = *flex_particles;
    result.flex_surrogate_visible = *flex_visible != 0;
    result.flex_surrogate_lod = *flex_lod;
    result.zed_time = *zed_time != 0;
    return result;
}

void apply_offline_telemetry_snapshot(
    GameLogSession& session,
    const OfflineTelemetrySnapshot& telemetry) noexcept {
    session.telemetry_sample = telemetry.sample;
    session.telemetry_living_zeds = telemetry.living;
    session.telemetry_living_classes = telemetry.living_classes;
    session.telemetry_living_bosses = telemetry.living_bosses;
    session.telemetry_living_visible = telemetry.living_visible;
    session.telemetry_living_offscreen = telemetry.living_offscreen;
    session.telemetry_living_lod_total = telemetry.living_lod_total;
    session.telemetry_living_anim_rate_total =
        telemetry.living_anim_rate_total;
    session.telemetry_living_injured_zones = telemetry.living_injured_zones;
    session.telemetry_living_required_bones = telemetry.living_required_bones;
    session.telemetry_living_material_slots = telemetry.living_material_slots;
    session.telemetry_living_attachments = telemetry.living_attachments;
    session.telemetry_living_anim_skipped = telemetry.living_anim_skipped;
    session.telemetry_living_bone_atoms_skipped =
        telemetry.living_bone_atoms_skipped;
    session.telemetry_living_bone_interpolation =
        telemetry.living_bone_interpolation;
    session.telemetry_living_kinematic_distance_skipped =
        telemetry.living_kinematic_distance_skipped;
    session.telemetry_living_ticks_offscreen = telemetry.living_ticks_offscreen;
    session.telemetry_living_special_moves = telemetry.living_special_moves;
    session.telemetry_living_attack_moves = telemetry.living_attack_moves;
    session.telemetry_living_grapple_moves = telemetry.living_grapple_moves;
    session.telemetry_living_stumbles = telemetry.living_stumbles;
    session.telemetry_living_knockdowns = telemetry.living_knockdowns;
    session.telemetry_living_hit_reactions = telemetry.living_hit_reactions;
    session.telemetry_living_other_special_moves =
        telemetry.living_other_special_moves;
    session.telemetry_corpse_total = telemetry.corpse_total;
    session.telemetry_corpse_awake = telemetry.corpse_awake;
    session.telemetry_corpse_sleeping = telemetry.corpse_sleeping;
    session.telemetry_corpse_other = telemetry.corpse_other;
    session.telemetry_corpse_final_pose = telemetry.corpse_final;
    session.telemetry_corpse_visible = telemetry.corpse_visible;
    session.telemetry_corpse_offscreen = telemetry.corpse_offscreen;
    session.telemetry_corpse_lod_total = telemetry.corpse_lod_total;
    session.telemetry_corpse_injured_zones = telemetry.corpse_injured_zones;
    session.telemetry_corpse_max_age_ms = telemetry.corpse_max_age_ms;
    session.telemetry_corpse_limit = telemetry.corpse_limit;
    session.telemetry_corpse_offscreen_time_ms =
        telemetry.corpse_offscreen_time_ms;
    session.telemetry_corpse_offscreen_distance =
        telemetry.corpse_offscreen_distance;
    session.telemetry_dismembered_corpses = telemetry.dismembered;
    session.telemetry_dismembered_limbs = telemetry.dismembered_limbs;
    session.telemetry_ragdoll_warned_corpses = telemetry.ragdoll_warned;
    session.telemetry_ragdoll_warning_max = telemetry.ragdoll_warning_max;
    session.telemetry_corpse_collide_dead = telemetry.corpse_collide_dead;
    session.telemetry_corpse_collide_living = telemetry.corpse_collide_living;
    session.telemetry_corpse_collide_dead_after_sleep =
        telemetry.corpse_collide_dead_after_sleep;
    session.telemetry_corpse_collide_living_after_sleep =
        telemetry.corpse_collide_living_after_sleep;
    session.telemetry_visible_gibs = telemetry.gibs;
    session.telemetry_spray_actors = telemetry.spray_actors;
    session.telemetry_fire_spray_actors = telemetry.fire_spray_actors;
    session.telemetry_toxic_spray_actors = telemetry.toxic_spray_actors;
    session.telemetry_other_spray_actors = telemetry.other_spray_actors;
    session.telemetry_explosion_actors = telemetry.explosion_actors;
    session.telemetry_damaging_explosion_actors =
        telemetry.damaging_explosion_actors;
    session.telemetry_fire_explosion_actors = telemetry.fire_explosion_actors;
    session.telemetry_toxic_explosion_actors = telemetry.toxic_explosion_actors;
    session.telemetry_other_damaging_explosion_actors =
        telemetry.other_damaging_explosion_actors;
    session.telemetry_unclassified_explosion_actors =
        telemetry.unclassified_explosion_actors;
    session.telemetry_lingering_explosion_actors =
        telemetry.lingering_explosion_actors;
    session.telemetry_smoke_explosion_actors =
        telemetry.smoke_explosion_actors;
    session.telemetry_bloat_king_fart_explosion_actors =
        telemetry.bloat_king_fart_explosion_actors;
    session.telemetry_smoke_grenade_projectiles =
        telemetry.smoke_grenade_projectiles;
    session.telemetry_puke_mine_projectiles = telemetry.puke_mine_projectiles;
    session.telemetry_bloat_king_puke_mine_projectiles =
        telemetry.bloat_king_puke_mine_projectiles;
    session.telemetry_wound_decals = telemetry.wound_decals;
    session.telemetry_splatter_decals = telemetry.splatter_decals;
    session.telemetry_pool_decals = telemetry.pool_decals;
    session.telemetry_impact_decals = telemetry.impact_decals;
    session.telemetry_explosion_decals = telemetry.explosion_decals;
    session.telemetry_wound_decal_limit = telemetry.wound_decal_limit;
    session.telemetry_splatter_decal_limit = telemetry.splatter_decal_limit;
    session.telemetry_pool_decal_limit = telemetry.pool_decal_limit;
    session.telemetry_impact_decal_limit = telemetry.impact_decal_limit;
    session.telemetry_explosion_decal_limit = telemetry.explosion_decal_limit;
    session.telemetry_blood_effect_limit = telemetry.blood_effect_limit;
    session.telemetry_gore_effect_limit = telemetry.gore_effect_limit;
    session.telemetry_wound_lifetime_ms = telemetry.wound_lifetime_ms;
    session.telemetry_splatter_lifetime_ms = telemetry.splatter_lifetime_ms;
    session.telemetry_pool_lifetime_ms = telemetry.pool_lifetime_ms;
    session.telemetry_gib_lifetime_ms = telemetry.gib_lifetime_ms;
    session.telemetry_gore_particle_components =
        telemetry.gore_particle_components;
    session.telemetry_gore_particles = telemetry.gore_particles;
    session.telemetry_gore_particle_visible_components =
        telemetry.gore_particle_visible_components;
    session.telemetry_gore_particle_lod_total =
        telemetry.gore_particle_lod_total;
    session.telemetry_gore_particle_bounded_components =
        telemetry.gore_particle_bounded_components;
    session.telemetry_world_particle_components =
        telemetry.world_particle_components;
    session.telemetry_world_particles = telemetry.world_particles;
    session.telemetry_world_particle_visible_components =
        telemetry.world_particle_visible_components;
    session.telemetry_world_particle_lod_total =
        telemetry.world_particle_lod_total;
    session.telemetry_world_particle_bounded_components =
        telemetry.world_particle_bounded_components;
    session.telemetry_ground_fire_particle_components =
        telemetry.ground_fire_particle_components;
    session.telemetry_ground_fire_particles = telemetry.ground_fire_particles;
    session.telemetry_impact_particle_components =
        telemetry.impact_particle_components;
    session.telemetry_impact_particles = telemetry.impact_particles;
    session.telemetry_gore_particle_pool_capacity =
        telemetry.gore_particle_pool_capacity;
    session.telemetry_world_particle_pool_capacity =
        telemetry.world_particle_pool_capacity;
    session.telemetry_ground_fire_particle_pool_capacity =
        telemetry.ground_fire_particle_pool_capacity;
    session.telemetry_impact_particle_pool_capacity =
        telemetry.impact_particle_pool_capacity;
    session.telemetry_particle_constant_spawn_emitters =
        telemetry.particle_constant_spawn_emitters;
    session.telemetry_particle_dynamic_spawn_emitters =
        telemetry.particle_dynamic_spawn_emitters;
    session.telemetry_particle_constant_spawn_rate_milli =
        telemetry.particle_constant_spawn_rate_milli;
    session.telemetry_particle_burst_entries =
        telemetry.particle_burst_entries;
    session.telemetry_particle_peak_capacity =
        telemetry.particle_peak_capacity;
    session.telemetry_particle_flex_components =
        telemetry.particle_flex_components;
    session.telemetry_particle_flex_fluid_components =
        telemetry.particle_flex_fluid_components;
    session.telemetry_particle_flex_nonfluid_components =
        telemetry.particle_flex_nonfluid_components;
    session.telemetry_particle_flex_mixed_components =
        telemetry.particle_flex_mixed_components;
    session.telemetry_particle_nonflex_components =
        telemetry.particle_nonflex_components;
    session.telemetry_particle_unclassified_components =
        telemetry.particle_unclassified_components;
    session.telemetry_flex_surrogate_active = telemetry.flex_surrogate_active;
    session.telemetry_flex_surrogate_particles =
        telemetry.flex_surrogate_particles;
    session.telemetry_flex_surrogate_visible = telemetry.flex_surrogate_visible;
    session.telemetry_flex_surrogate_lod = telemetry.flex_surrogate_lod;
    session.telemetry_zed_time_active = telemetry.zed_time;
}

}  // namespace

namespace detail {

std::optional<bool> apply_offline_telemetry_line(
    GameLogSession& session, std::string_view line,
    std::uint64_t observed_at_ns) {
    const auto telemetry = parse_offline_telemetry_line(line);
    if (!telemetry) return std::nullopt;
    auto updated = session;
    apply_offline_telemetry_snapshot(updated, *telemetry);
    const bool value_changed = updated != session;
    apply_offline_telemetry_snapshot(session, *telemetry);
    if (observed_at_ns != 0) session.telemetry_observed_ns = observed_at_ns;
    return value_changed;
}

void clear_offline_telemetry_snapshot(GameLogSession& session) noexcept {
    session.telemetry_sample.reset();
    session.telemetry_living_zeds.reset();
    session.telemetry_living_classes.reset();
    session.telemetry_living_bosses.reset();
    session.telemetry_living_visible.reset();
    session.telemetry_living_offscreen.reset();
    session.telemetry_living_lod_total.reset();
    session.telemetry_living_anim_rate_total.reset();
    session.telemetry_living_injured_zones.reset();
    session.telemetry_living_required_bones.reset();
    session.telemetry_living_material_slots.reset();
    session.telemetry_living_attachments.reset();
    session.telemetry_living_anim_skipped.reset();
    session.telemetry_living_bone_atoms_skipped.reset();
    session.telemetry_living_bone_interpolation.reset();
    session.telemetry_living_kinematic_distance_skipped.reset();
    session.telemetry_living_ticks_offscreen.reset();
    session.telemetry_living_special_moves.reset();
    session.telemetry_living_attack_moves.reset();
    session.telemetry_living_grapple_moves.reset();
    session.telemetry_living_stumbles.reset();
    session.telemetry_living_knockdowns.reset();
    session.telemetry_living_hit_reactions.reset();
    session.telemetry_living_other_special_moves.reset();
    session.telemetry_corpse_total.reset();
    session.telemetry_corpse_awake.reset();
    session.telemetry_corpse_sleeping.reset();
    session.telemetry_corpse_other.reset();
    session.telemetry_corpse_final_pose.reset();
    session.telemetry_corpse_visible.reset();
    session.telemetry_corpse_offscreen.reset();
    session.telemetry_corpse_lod_total.reset();
    session.telemetry_corpse_injured_zones.reset();
    session.telemetry_corpse_max_age_ms.reset();
    session.telemetry_corpse_limit.reset();
    session.telemetry_corpse_offscreen_time_ms.reset();
    session.telemetry_corpse_offscreen_distance.reset();
    session.telemetry_dismembered_corpses.reset();
    session.telemetry_dismembered_limbs.reset();
    session.telemetry_ragdoll_warned_corpses.reset();
    session.telemetry_ragdoll_warning_max.reset();
    session.telemetry_corpse_collide_dead.reset();
    session.telemetry_corpse_collide_living.reset();
    session.telemetry_corpse_collide_dead_after_sleep.reset();
    session.telemetry_corpse_collide_living_after_sleep.reset();
    session.telemetry_visible_gibs.reset();
    session.telemetry_spray_actors.reset();
    session.telemetry_fire_spray_actors.reset();
    session.telemetry_toxic_spray_actors.reset();
    session.telemetry_other_spray_actors.reset();
    session.telemetry_explosion_actors.reset();
    session.telemetry_damaging_explosion_actors.reset();
    session.telemetry_fire_explosion_actors.reset();
    session.telemetry_toxic_explosion_actors.reset();
    session.telemetry_other_damaging_explosion_actors.reset();
    session.telemetry_unclassified_explosion_actors.reset();
    session.telemetry_lingering_explosion_actors.reset();
    session.telemetry_smoke_explosion_actors.reset();
    session.telemetry_bloat_king_fart_explosion_actors.reset();
    session.telemetry_smoke_grenade_projectiles.reset();
    session.telemetry_puke_mine_projectiles.reset();
    session.telemetry_bloat_king_puke_mine_projectiles.reset();
    session.telemetry_wound_decals.reset();
    session.telemetry_splatter_decals.reset();
    session.telemetry_pool_decals.reset();
    session.telemetry_impact_decals.reset();
    session.telemetry_explosion_decals.reset();
    session.telemetry_wound_decal_limit.reset();
    session.telemetry_splatter_decal_limit.reset();
    session.telemetry_pool_decal_limit.reset();
    session.telemetry_impact_decal_limit.reset();
    session.telemetry_explosion_decal_limit.reset();
    session.telemetry_blood_effect_limit.reset();
    session.telemetry_gore_effect_limit.reset();
    session.telemetry_wound_lifetime_ms.reset();
    session.telemetry_splatter_lifetime_ms.reset();
    session.telemetry_pool_lifetime_ms.reset();
    session.telemetry_gib_lifetime_ms.reset();
    session.telemetry_gore_particle_components.reset();
    session.telemetry_gore_particles.reset();
    session.telemetry_gore_particle_visible_components.reset();
    session.telemetry_gore_particle_lod_total.reset();
    session.telemetry_gore_particle_bounded_components.reset();
    session.telemetry_world_particle_components.reset();
    session.telemetry_world_particles.reset();
    session.telemetry_world_particle_visible_components.reset();
    session.telemetry_world_particle_lod_total.reset();
    session.telemetry_world_particle_bounded_components.reset();
    session.telemetry_ground_fire_particle_components.reset();
    session.telemetry_ground_fire_particles.reset();
    session.telemetry_impact_particle_components.reset();
    session.telemetry_impact_particles.reset();
    session.telemetry_gore_particle_pool_capacity.reset();
    session.telemetry_world_particle_pool_capacity.reset();
    session.telemetry_ground_fire_particle_pool_capacity.reset();
    session.telemetry_impact_particle_pool_capacity.reset();
    session.telemetry_particle_constant_spawn_emitters.reset();
    session.telemetry_particle_dynamic_spawn_emitters.reset();
    session.telemetry_particle_constant_spawn_rate_milli.reset();
    session.telemetry_particle_burst_entries.reset();
    session.telemetry_particle_peak_capacity.reset();
    session.telemetry_particle_flex_components.reset();
    session.telemetry_particle_flex_fluid_components.reset();
    session.telemetry_particle_flex_nonfluid_components.reset();
    session.telemetry_particle_flex_mixed_components.reset();
    session.telemetry_particle_nonflex_components.reset();
    session.telemetry_particle_unclassified_components.reset();
    session.telemetry_flex_surrogate_active.reset();
    session.telemetry_flex_surrogate_particles.reset();
    session.telemetry_flex_surrogate_visible.reset();
    session.telemetry_flex_surrogate_lod.reset();
    session.telemetry_zed_time_active.reset();
    session.telemetry_observed_ns = 0;
}

void clear_gameplay_snapshot(GameLogSession& session) noexcept {
    session.zeds_remaining.reset();
    session.zeds_alive.reset();
    session.wave_number.reset();
    session.wave_total_ai.reset();
    clear_offline_telemetry_snapshot(session);
    session.zeds_remaining_observed_ns = 0;
    session.zeds_alive_observed_ns = 0;
    session.wave_observed_ns = 0;
}

}  // namespace detail
}  // namespace kf2::game
