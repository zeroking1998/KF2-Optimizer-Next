#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "kf2/game/game_log_session.hpp"

namespace kf2::diagnostics {

enum class Severity {
    info,
    warning,
    error,
};

struct Event {
    std::uint64_t sequence{0};
    Severity severity{Severity::info};
    std::string code;
    std::wstring message;
    std::wstring source;
    std::uint32_t repeat_count{1};
    // Retained audit events are evicted only after all ordinary events have
    // already left the bounded log. This flag is intentionally not serialized,
    // preserving the version-1 JSON contract for existing consumers.
    bool retained_audit{false};
};

struct EventLogStats {
    std::uint64_t appended{0};
    std::uint64_t deduplicated{0};
    std::uint64_t overwritten{0};
    std::uint64_t persistence_failures{0};
};

class EventLog final {
public:
    explicit EventLog(std::size_t capacity,
                      std::filesystem::path persistence_path = {});
    void append(Event event);
    [[nodiscard]] std::vector<Event> snapshot() const;
    void clear();
    [[nodiscard]] const std::filesystem::path& persistence_path() const noexcept;
    [[nodiscard]] bool persistence_ready() const noexcept;
    [[nodiscard]] EventLogStats stats() const noexcept;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    std::uint64_t next_sequence_{1};
    std::filesystem::path persistence_path_;
    bool persistence_ready_{false};
    EventLogStats stats_{};
    void persist_locked() noexcept;
};

[[nodiscard]] std::string serialize_events_json(
    const std::vector<Event>& events);

struct ProductReport {
    std::wstring build_identity;
    std::wstring mode;
    std::wstring game;
    std::wstring game_session;
    std::wstring telemetry;
    std::wstring performance_analysis;
    std::wstring hardware;
    std::wstring flex;
    std::wstring optimizer_profile;
    std::wstring quality_policy;
    std::wstring overlay_position;
    int target_fps{0};
    int overlay_scale_percent{100};
    bool overlay_enabled{false};
    bool restore_config_after_game{false};
    std::optional<std::uint32_t> game_pid;
    std::optional<std::uint64_t> game_process_start_id;
    std::optional<int> zeds_alive;
    std::optional<int> zeds_remaining;
    std::optional<int> wave_number;
    std::optional<int> wave_total_ai;
    std::optional<int> telemetry_living_zeds;
    std::optional<int> living_classes;
    std::optional<int> living_bosses;
    std::optional<int> living_visible;
    std::optional<int> living_offscreen;
    std::optional<int> living_lod_total;
    std::optional<int> living_anim_rate_total;
    std::optional<int> living_injured_zones;
    std::optional<int> living_required_bones;
    std::optional<int> living_material_slots;
    std::optional<int> living_attachments;
    std::optional<int> living_anim_skipped;
    std::optional<int> living_bone_atoms_skipped;
    std::optional<int> living_bone_interpolation;
    std::optional<int> living_kinematic_distance_skipped;
    std::optional<int> living_ticks_offscreen;
    std::optional<int> living_special_moves;
    std::optional<int> living_attack_moves;
    std::optional<int> living_grapple_moves;
    std::optional<int> living_stumbles;
    std::optional<int> living_knockdowns;
    std::optional<int> living_hit_reactions;
    std::optional<int> living_other_special_moves;
    std::optional<int> corpse_total;
    std::optional<int> corpse_awake;
    std::optional<int> corpse_sleeping;
    std::optional<int> corpse_other;
    std::optional<int> corpse_final_pose;
    std::optional<int> corpse_visible;
    std::optional<int> corpse_offscreen;
    std::optional<int> corpse_lod_total;
    std::optional<int> corpse_injured_zones;
    std::optional<int> corpse_max_age_ms;
    std::optional<int> corpse_limit;
    std::optional<int> corpse_offscreen_time_ms;
    std::optional<int> corpse_offscreen_distance;
    std::optional<int> dismembered_corpses;
    std::optional<int> dismembered_limbs;
    std::optional<int> ragdoll_warned_corpses;
    std::optional<int> ragdoll_warning_max;
    std::optional<bool> corpse_collide_dead;
    std::optional<bool> corpse_collide_living;
    std::optional<bool> corpse_collide_dead_after_sleep;
    std::optional<bool> corpse_collide_living_after_sleep;
    std::optional<int> visible_gibs;
    std::optional<int> spray_actors;
    std::optional<int> fire_spray_actors;
    std::optional<int> toxic_spray_actors;
    std::optional<int> other_spray_actors;
    std::optional<int> explosion_actors;
    std::optional<int> damaging_explosion_actors;
    std::optional<int> fire_explosion_actors;
    std::optional<int> toxic_explosion_actors;
    std::optional<int> other_damaging_explosion_actors;
    std::optional<int> unclassified_explosion_actors;
    std::optional<int> lingering_explosion_actors;
    std::optional<int> smoke_explosion_actors;
    std::optional<int> bloat_king_fart_explosion_actors;
    std::optional<int> smoke_grenade_projectiles;
    std::optional<int> puke_mine_projectiles;
    std::optional<int> bloat_king_puke_mine_projectiles;
    std::optional<int> wound_decals;
    std::optional<int> splatter_decals;
    std::optional<int> pool_decals;
    std::optional<int> impact_decals;
    std::optional<int> explosion_decals;
    std::optional<int> wound_decal_limit;
    std::optional<int> splatter_decal_limit;
    std::optional<int> pool_decal_limit;
    std::optional<int> impact_decal_limit;
    std::optional<int> explosion_decal_limit;
    std::optional<int> blood_effect_limit;
    std::optional<int> gore_effect_limit;
    std::optional<int> wound_lifetime_ms;
    std::optional<int> splatter_lifetime_ms;
    std::optional<int> pool_lifetime_ms;
    std::optional<int> gib_lifetime_ms;
    std::optional<int> gore_particle_components;
    std::optional<int> gore_particles;
    std::optional<int> gore_particle_visible_components;
    std::optional<int> gore_particle_lod_total;
    std::optional<int> gore_particle_bounded_components;
    std::optional<int> world_particle_components;
    std::optional<int> world_particles;
    std::optional<int> world_particle_visible_components;
    std::optional<int> world_particle_lod_total;
    std::optional<int> world_particle_bounded_components;
    std::optional<int> ground_fire_particle_components;
    std::optional<int> ground_fire_particles;
    std::optional<int> impact_particle_components;
    std::optional<int> impact_particles;
    std::optional<int> gore_particle_pool_capacity;
    std::optional<int> world_particle_pool_capacity;
    std::optional<int> ground_fire_particle_pool_capacity;
    std::optional<int> impact_particle_pool_capacity;
    std::optional<int> particle_constant_spawn_emitters;
    std::optional<int> particle_dynamic_spawn_emitters;
    std::optional<int> particle_constant_spawn_rate_milli;
    std::optional<int> particle_burst_entries;
    std::optional<int> particle_peak_capacity;
    std::optional<int> particle_flex_components;
    std::optional<int> particle_flex_fluid_components;
    std::optional<int> particle_flex_nonfluid_components;
    std::optional<int> particle_flex_mixed_components;
    std::optional<int> particle_nonflex_components;
    std::optional<int> particle_unclassified_components;
    std::optional<bool> flex_surrogate_active;
    std::optional<int> flex_surrogate_particles;
    std::optional<bool> flex_surrogate_visible;
    std::optional<int> flex_surrogate_lod;
    std::optional<bool> zed_time_active;
    std::optional<std::uint64_t> gameplay_snapshot_age_ms;
    bool gameplay_snapshot_fresh{false};
    bool offline_gameplay{false};
    EventLogStats event_log_stats;
    game::GameLogParserStats game_log_stats;
    std::size_t retained_crash_records{0};
    std::vector<Event> events;
};

[[nodiscard]] std::string serialize_product_report_json(
    const ProductReport& report);
[[nodiscard]] std::string serialize_support_bundle_json(
    const ProductReport& report, std::string_view issue72_inventory_json);

}  // namespace kf2::diagnostics
