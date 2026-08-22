#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kf2::game {

enum class GameLogPhase {
    map_loaded,
    main_menu,
    match_ended,
};

struct GameLogSession {
    std::string map;
    std::optional<std::string> game_class;
    std::optional<double> difficulty;
    std::optional<int> game_length;
    std::optional<std::string> net_mode;
    std::optional<int> zeds_remaining;
    std::optional<int> zeds_alive;
    std::optional<int> wave_number;
    std::optional<int> wave_total_ai;
    std::optional<int> telemetry_living_zeds;
    std::optional<int> telemetry_living_classes;
    std::optional<int> telemetry_living_bosses;
    std::optional<int> telemetry_living_visible;
    std::optional<int> telemetry_living_offscreen;
    std::optional<int> telemetry_living_lod_total;
    std::optional<int> telemetry_living_anim_rate_total;
    std::optional<int> telemetry_living_injured_zones;
    std::optional<int> telemetry_living_required_bones;
    std::optional<int> telemetry_living_material_slots;
    std::optional<int> telemetry_living_attachments;
    std::optional<int> telemetry_living_anim_skipped;
    std::optional<int> telemetry_living_bone_atoms_skipped;
    std::optional<int> telemetry_living_bone_interpolation;
    std::optional<int> telemetry_living_kinematic_distance_skipped;
    std::optional<int> telemetry_living_ticks_offscreen;
    std::optional<int> telemetry_living_special_moves;
    std::optional<int> telemetry_living_attack_moves;
    std::optional<int> telemetry_living_grapple_moves;
    std::optional<int> telemetry_living_stumbles;
    std::optional<int> telemetry_living_knockdowns;
    std::optional<int> telemetry_living_hit_reactions;
    std::optional<int> telemetry_living_other_special_moves;
    std::optional<int> telemetry_corpse_total;
    std::optional<int> telemetry_corpse_awake;
    std::optional<int> telemetry_corpse_sleeping;
    std::optional<int> telemetry_corpse_other;
    std::optional<int> telemetry_corpse_final_pose;
    std::optional<int> telemetry_corpse_visible;
    std::optional<int> telemetry_corpse_offscreen;
    std::optional<int> telemetry_corpse_lod_total;
    std::optional<int> telemetry_corpse_injured_zones;
    std::optional<int> telemetry_corpse_max_age_ms;
    std::optional<int> telemetry_corpse_limit;
    std::optional<int> telemetry_corpse_offscreen_time_ms;
    std::optional<int> telemetry_corpse_offscreen_distance;
    std::optional<int> telemetry_dismembered_corpses;
    std::optional<int> telemetry_dismembered_limbs;
    std::optional<int> telemetry_ragdoll_warned_corpses;
    std::optional<int> telemetry_ragdoll_warning_max;
    std::optional<bool> telemetry_corpse_collide_dead;
    std::optional<bool> telemetry_corpse_collide_living;
    std::optional<bool> telemetry_corpse_collide_dead_after_sleep;
    std::optional<bool> telemetry_corpse_collide_living_after_sleep;
    std::optional<int> telemetry_visible_gibs;
    std::optional<int> telemetry_spray_actors;
    std::optional<int> telemetry_fire_spray_actors;
    std::optional<int> telemetry_toxic_spray_actors;
    std::optional<int> telemetry_other_spray_actors;
    std::optional<int> telemetry_explosion_actors;
    std::optional<int> telemetry_damaging_explosion_actors;
    std::optional<int> telemetry_fire_explosion_actors;
    std::optional<int> telemetry_toxic_explosion_actors;
    std::optional<int> telemetry_other_damaging_explosion_actors;
    std::optional<int> telemetry_unclassified_explosion_actors;
    std::optional<int> telemetry_lingering_explosion_actors;
    std::optional<int> telemetry_smoke_explosion_actors;
    std::optional<int> telemetry_bloat_king_fart_explosion_actors;
    std::optional<int> telemetry_smoke_grenade_projectiles;
    std::optional<int> telemetry_puke_mine_projectiles;
    std::optional<int> telemetry_bloat_king_puke_mine_projectiles;
    std::optional<int> telemetry_wound_decals;
    std::optional<int> telemetry_splatter_decals;
    std::optional<int> telemetry_pool_decals;
    std::optional<int> telemetry_impact_decals;
    std::optional<int> telemetry_explosion_decals;
    std::optional<int> telemetry_wound_decal_limit;
    std::optional<int> telemetry_splatter_decal_limit;
    std::optional<int> telemetry_pool_decal_limit;
    std::optional<int> telemetry_impact_decal_limit;
    std::optional<int> telemetry_explosion_decal_limit;
    std::optional<int> telemetry_blood_effect_limit;
    std::optional<int> telemetry_gore_effect_limit;
    std::optional<int> telemetry_wound_lifetime_ms;
    std::optional<int> telemetry_splatter_lifetime_ms;
    std::optional<int> telemetry_pool_lifetime_ms;
    std::optional<int> telemetry_gib_lifetime_ms;
    std::optional<int> telemetry_gore_particle_components;
    std::optional<int> telemetry_gore_particles;
    std::optional<int> telemetry_gore_particle_visible_components;
    std::optional<int> telemetry_gore_particle_lod_total;
    std::optional<int> telemetry_gore_particle_bounded_components;
    std::optional<int> telemetry_world_particle_components;
    std::optional<int> telemetry_world_particles;
    std::optional<int> telemetry_world_particle_visible_components;
    std::optional<int> telemetry_world_particle_lod_total;
    std::optional<int> telemetry_world_particle_bounded_components;
    std::optional<int> telemetry_ground_fire_particle_components;
    std::optional<int> telemetry_ground_fire_particles;
    std::optional<int> telemetry_impact_particle_components;
    std::optional<int> telemetry_impact_particles;
    std::optional<int> telemetry_gore_particle_pool_capacity;
    std::optional<int> telemetry_world_particle_pool_capacity;
    std::optional<int> telemetry_ground_fire_particle_pool_capacity;
    std::optional<int> telemetry_impact_particle_pool_capacity;
    std::optional<int> telemetry_particle_constant_spawn_emitters;
    std::optional<int> telemetry_particle_dynamic_spawn_emitters;
    std::optional<int> telemetry_particle_constant_spawn_rate_milli;
    std::optional<int> telemetry_particle_burst_entries;
    std::optional<int> telemetry_particle_peak_capacity;
    std::optional<int> telemetry_particle_flex_components;
    std::optional<int> telemetry_particle_flex_fluid_components;
    std::optional<int> telemetry_particle_flex_nonfluid_components;
    std::optional<int> telemetry_particle_flex_mixed_components;
    std::optional<int> telemetry_particle_nonflex_components;
    std::optional<int> telemetry_particle_unclassified_components;
    std::optional<bool> telemetry_flex_surrogate_active;
    std::optional<int> telemetry_flex_surrogate_particles;
    std::optional<bool> telemetry_flex_surrogate_visible;
    std::optional<int> telemetry_flex_surrogate_lod;
    std::optional<bool> telemetry_zed_time_active;
    std::optional<int> telemetry_sample;
    std::uint64_t zeds_remaining_observed_ns{0};
    std::uint64_t zeds_alive_observed_ns{0};
    std::uint64_t wave_observed_ns{0};
    std::uint64_t telemetry_observed_ns{0};
    GameLogPhase phase{GameLogPhase::map_loaded};
    bool main_menu{false};

    bool operator==(const GameLogSession&) const = default;
};

struct GameLogParserStats {
    std::uint64_t bytes_received{0};
    std::uint64_t lines_processed{0};
    std::uint64_t oversized_input_resets{0};
    std::uint64_t oversized_line_drops{0};
};

inline constexpr std::uint64_t kGameLogObservationFreshnessNs =
    15'000'000'000ULL;

class GameLogSessionParser final {
public:
    [[nodiscard]] std::optional<GameLogSession> feed(
        std::string_view bytes, std::uint64_t observed_at_ns = 0);
    [[nodiscard]] std::optional<GameLogSession> expire_observations(
        std::uint64_t now_ns,
        std::uint64_t maximum_age_ns = kGameLogObservationFreshnessNs) noexcept;
    void reset() noexcept;
    [[nodiscard]] const std::optional<GameLogSession>& current() const noexcept;
    [[nodiscard]] GameLogParserStats stats() const noexcept;

private:
    std::string pending_;
    std::optional<GameLogSession> current_;
    GameLogParserStats stats_{};
};

[[nodiscard]] std::optional<GameLogSession> parse_load_map_line(
    std::string_view line);
[[nodiscard]] std::optional<std::string> parse_net_mode_line(
    std::string_view line);
[[nodiscard]] std::wstring describe_game_log_session(
    const GameLogSession& session);
[[nodiscard]] bool game_log_is_active_gameplay(
    const GameLogSession& session) noexcept;
[[nodiscard]] bool game_log_is_offline_gameplay(
    const GameLogSession& session) noexcept;
[[nodiscard]] bool game_log_observation_is_fresh(
    const std::optional<int>& value, std::uint64_t observed_at_ns,
    std::uint64_t now_ns,
    std::uint64_t maximum_age_ns = kGameLogObservationFreshnessNs) noexcept;
[[nodiscard]] bool game_log_reports_engine_exit(
    std::string_view text) noexcept;
[[nodiscard]] constexpr bool game_log_belongs_to_process(
    std::uint64_t last_write_filetime,
    std::uint64_t process_start_filetime) noexcept {
    return last_write_filetime != 0 && process_start_filetime != 0 &&
           last_write_filetime >= process_start_filetime;
}

}  // namespace kf2::game
