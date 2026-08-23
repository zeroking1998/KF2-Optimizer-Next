#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/game/game_log_session.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

std::string telemetry_line(int sample, int corpse_awake = 2) {
    return "ScriptLog: KF2OPT_TELEMETRY schema=6 sample=" +
        std::to_string(sample) +
        " living=23 living_classes=7 living_bosses=1 living_visible=18"
        " living_offscreen=5 living_lod_total=31"
        " living_anim_rate_total=1380 living_injured_zones=9"
        " living_required_bones=1840 living_material_slots=92"
        " living_attachments=17 living_anim_skipped=3"
        " living_bone_atoms_skipped=4 living_bone_interpolation=2"
        " living_kinematic_distance_skipped=1 living_ticks_offscreen=23"
        " living_special_moves=8 living_attack_moves=3"
        " living_grapple_moves=1 living_stumbles=1 living_knockdowns=1"
        " living_hit_reactions=1 living_other_special_moves=1"
        " corpse_total=8 corpse_awake=" + std::to_string(corpse_awake) +
        " corpse_sleeping=5 corpse_other=1 corpse_final=4"
        " corpse_visible=3 corpse_offscreen=5 corpse_lod_total=14"
        " corpse_injured_zones=12 corpse_max_age_ms=42000"
        " corpse_limit=12 corpse_offscreen_time_ms=60000"
        " corpse_offscreen_distance=5000 dismembered=3 dismembered_limbs=7"
        " ragdoll_warned=2 ragdoll_warning_max=3"
        " corpse_collide_dead=1 corpse_collide_living=1"
        " corpse_collide_dead_after_sleep=0"
        " corpse_collide_living_after_sleep=1 gibs=11 zed_time=1"
        " spray_actors=6 fire_spray_actors=3 toxic_spray_actors=2"
        " other_spray_actors=1 explosion_actors=5"
        " damaging_explosion_actors=4 fire_explosion_actors=1"
        " toxic_explosion_actors=2"
        " other_damaging_explosion_actors=1"
        " unclassified_explosion_actors=1 lingering_explosion_actors=2"
        " smoke_explosion_actors=1 bloat_king_fart_explosion_actors=1"
        " smoke_grenade_projectiles=2 puke_mine_projectiles=3"
        " bloat_king_puke_mine_projectiles=1"
        " wound_decals=12 splatter_decals=9"
        " pool_decals=4 impact_decals=7 explosion_decals=3"
        " wound_decal_limit=64 splatter_decal_limit=64 pool_decal_limit=20"
        " impact_decal_limit=40 explosion_decal_limit=20"
        " blood_effect_limit=25 gore_effect_limit=25"
        " wound_lifetime_ms=10000 splatter_lifetime_ms=15000"
        " pool_lifetime_ms=30000 gib_lifetime_ms=20000"
        " gore_particle_components=6 gore_particles=345"
        " gore_particle_visible_components=4 gore_particle_lod_total=8"
        " gore_particle_bounded_components=6"
        " world_particle_components=18 world_particles=987"
        " world_particle_visible_components=12 world_particle_lod_total=21"
        " world_particle_bounded_components=18"
        " ground_fire_particle_components=4 ground_fire_particles=222"
        " impact_particle_components=5 impact_particles=333"
        " gore_particle_pool_capacity=30 world_particle_pool_capacity=200"
        " ground_fire_particle_pool_capacity=100"
        " impact_particle_pool_capacity=60"
        " particle_constant_spawn_emitters=14"
        " particle_dynamic_spawn_emitters=10"
        " particle_constant_spawn_rate_milli=125500"
        " particle_burst_entries=19 particle_peak_capacity=2048"
        " particle_flex_components=5 particle_flex_fluid_components=3"
        " particle_flex_nonfluid_components=1 particle_flex_mixed_components=1"
        " particle_nonflex_components=17 particle_unclassified_components=2"
        " flex_surrogate_active=1 flex_surrogate_particles=72"
        " flex_surrogate_visible=1 flex_surrogate_lod=2\n";
}

std::string empty_telemetry_line() {
    return
        "ScriptLog: KF2OPT_TELEMETRY schema=6 sample=1"
        " living=1 living_classes=1 living_bosses=0 living_visible=1"
        " living_offscreen=0 living_lod_total=0 living_anim_rate_total=60"
        " living_injured_zones=0"
        " living_required_bones=80 living_material_slots=4"
        " living_attachments=1 living_anim_skipped=0"
        " living_bone_atoms_skipped=0 living_bone_interpolation=0"
        " living_kinematic_distance_skipped=0 living_ticks_offscreen=1"
        " living_special_moves=0 living_attack_moves=0"
        " living_grapple_moves=0 living_stumbles=0 living_knockdowns=0"
        " living_hit_reactions=0 living_other_special_moves=0"
        " corpse_total=0 corpse_awake=0 corpse_sleeping=0 corpse_other=0"
        " corpse_final=0 corpse_visible=0"
        " corpse_offscreen=0 corpse_lod_total=0 corpse_injured_zones=0"
        " corpse_max_age_ms=0 corpse_limit=12"
        " corpse_offscreen_time_ms=60000 corpse_offscreen_distance=5000"
        " dismembered=0 dismembered_limbs=0 ragdoll_warned=0"
        " ragdoll_warning_max=0 corpse_collide_dead=1"
        " corpse_collide_living=1 corpse_collide_dead_after_sleep=0"
        " corpse_collide_living_after_sleep=1 gibs=0 zed_time=0"
        " spray_actors=0"
        " fire_spray_actors=0 toxic_spray_actors=0 other_spray_actors=0"
        " explosion_actors=0 damaging_explosion_actors=0"
        " fire_explosion_actors=0 toxic_explosion_actors=0"
        " other_damaging_explosion_actors=0"
        " unclassified_explosion_actors=0 lingering_explosion_actors=0"
        " smoke_explosion_actors=0 bloat_king_fart_explosion_actors=0"
        " smoke_grenade_projectiles=0 puke_mine_projectiles=0"
        " bloat_king_puke_mine_projectiles=0"
        " wound_decals=0 splatter_decals=0 pool_decals=0 impact_decals=0"
        " explosion_decals=0 wound_decal_limit=64 splatter_decal_limit=64"
        " pool_decal_limit=20 impact_decal_limit=40 explosion_decal_limit=20"
        " blood_effect_limit=25 gore_effect_limit=25 wound_lifetime_ms=10000"
        " splatter_lifetime_ms=15000 pool_lifetime_ms=30000"
        " gib_lifetime_ms=20000 gore_particle_components=0 gore_particles=0"
        " gore_particle_visible_components=0 gore_particle_lod_total=0"
        " gore_particle_bounded_components=0 world_particle_components=0"
        " world_particles=0 world_particle_visible_components=0"
        " world_particle_lod_total=0 world_particle_bounded_components=0"
        " ground_fire_particle_components=0 ground_fire_particles=0"
        " impact_particle_components=0 impact_particles=0"
        " gore_particle_pool_capacity=30 world_particle_pool_capacity=200"
        " ground_fire_particle_pool_capacity=100"
        " impact_particle_pool_capacity=60"
        " particle_constant_spawn_emitters=0"
        " particle_dynamic_spawn_emitters=0"
        " particle_constant_spawn_rate_milli=0"
        " particle_burst_entries=0 particle_peak_capacity=0"
        " particle_flex_components=0 particle_flex_fluid_components=0"
        " particle_flex_nonfluid_components=0 particle_flex_mixed_components=0"
        " particle_nonflex_components=0 particle_unclassified_components=0"
        " flex_surrogate_active=0 flex_surrogate_particles=0"
        " flex_surrogate_visible=0 flex_surrogate_lod=0\n";
}

}  // namespace

int main() {
    using namespace kf2::game;
    const auto parsed = parse_load_map_line(
        "[0053.20] Log: LoadMap: KF-BioticsLab?Name=Player?Team=255?"
        "Game=KFGameContent.KFGameInfo_Survival?Difficulty=1.0000?"
        "GameLength=1?AllowSeasonalSkins=1");
    CHECK(parsed.has_value());
    CHECK(parsed->map == "KF-BioticsLab");
    CHECK(parsed->game_class == "KFGameContent.KFGameInfo_Survival");
    CHECK(parsed->difficulty == 1.0);
    CHECK(parsed->game_length == 1);
    CHECK(parsed->phase == GameLogPhase::map_loaded);
    CHECK(!parsed->net_mode.has_value());
    CHECK(!parsed->main_menu);
    CHECK(game_log_is_active_gameplay(*parsed));
    CHECK(!game_log_is_offline_gameplay(*parsed));
    CHECK(describe_game_log_session(*parsed).find(L"KF-BioticsLab") !=
          std::wstring::npos);

    const auto menu = parse_load_map_line(
        "[0587.55] Log: LoadMap: KFMainMenu?closed?Name=Player?Team=255");
    CHECK(menu.has_value());
    CHECK(menu->main_menu);
    CHECK(menu->phase == GameLogPhase::main_menu);
    CHECK(!game_log_is_active_gameplay(*menu));
    CHECK(!parse_load_map_line("not a load event").has_value());
    CHECK(!parse_load_map_line("Log: LoadMap: ../unsafe?Difficulty=1").has_value());

    GameLogSessionParser stream;
    CHECK(!stream.feed("[1] Log: LoadMap: KF-Air").has_value());
    const auto chunked = stream.feed(
        "ship?Game=KFGameContent.KFGameInfo_Survival?Difficulty=2.0000?"
        "GameLength=2\r\n");
    CHECK(chunked.has_value());
    CHECK(chunked->map == "KF-Airship");
    CHECK(!stream.feed("unrelated\n").has_value());
    CHECK(stream.current().has_value());

    const auto offline = stream.feed(
        "[0048.42] ScriptLog: WI.NetMode:  NM_Standalone\n");
    CHECK(offline.has_value());
    CHECK(offline->net_mode == "NM_Standalone");
    CHECK(game_log_is_offline_gameplay(*offline));
    CHECK(describe_game_log_session(*offline).find(L"connection: offline") !=
          std::wstring::npos);
    CHECK(!stream.feed(
        "[0048.43] ScriptLog: WI.NetMode:  NM_Standalone\n").has_value());
    CHECK(!stream.feed(
        "[0048.44] ScriptLog: WI.NetMode:  NM_Unknown\n").has_value());
    const auto bridge = stream.feed(
        "[0048.45] ScriptLog: KF2OPT_ADAPTIVE_BRIDGE state=ready "
        "port=49152\n");
    CHECK(bridge.has_value());
    CHECK(bridge->telemetry_control_port == 49152);
    CHECK(!stream.feed(
        "[0048.46] ScriptLog: KF2OPT_ADAPTIVE_BRIDGE state=ready "
        "port=0\n").has_value());
    CHECK(stream.current()->telemetry_control_port == 49152);

    const auto remaining = stream.feed(
        "[0060.10] ScriptLog: @@@@ ZED COUNT DEBUG: "
        "MyKFGRI.AIRemaining = 93\n", 1'000'000'000ULL);
    CHECK(remaining.has_value());
    CHECK(remaining->zeds_remaining == 93);
    CHECK(!remaining->zeds_alive.has_value());
    const auto alive = stream.feed(
        "[0060.11] ScriptLog: @@@@ ZED COUNT DEBUG: AIAliveCount = 24\n",
        2'000'000'000ULL);
    CHECK(alive.has_value());
    CHECK(alive->zeds_remaining == 93);
    CHECK(alive->zeds_alive == 24);
    CHECK(describe_game_log_session(*alive).find(
              L"last confirmed living Zeds: 24") !=
          std::wstring::npos);
    CHECK(describe_game_log_session(*alive).find(
              L"last confirmed wave remaining: 93") !=
          std::wstring::npos);
    const auto wave = stream.feed(
        "[0060.12] ScriptLog: KFAISpawnManager.SetupNextWave() "
        "NextWave: 0 WaveTotalAI: 93\n", 2'500'000'000ULL);
    CHECK(wave.has_value());
    CHECK(wave->wave_number == 1);
    CHECK(wave->wave_total_ai == 93);
    CHECK(describe_game_log_session(*wave).find(
              L"last confirmed wave: 1 (93 total AI)") !=
          std::wstring::npos);
    CHECK(game_log_observation_is_fresh(
        wave->wave_number, wave->wave_observed_ns, 3'000'000'000ULL));
    CHECK(!game_log_observation_is_fresh(
        wave->wave_number, wave->wave_observed_ns, 18'000'000'001ULL));
    CHECK(!stream.feed(
        "[0060.13] ScriptLog: @@@@ ZED COUNT DEBUG: AIAliveCount = 24\n",
        4'000'000'000ULL)
               .has_value());
    CHECK(stream.current()->zeds_alive_observed_ns == 4'000'000'000ULL);
    CHECK(!stream.feed(
        "[0060.14] ScriptLog: @@@@ ZED COUNT DEBUG: AIAliveCount = -1\n")
               .has_value());

    const auto probe = stream.feed(telemetry_line(7), 4'500'000'000ULL);
    CHECK(probe.has_value());
    CHECK(probe->telemetry_sample == 7);
    CHECK(probe->telemetry_living_zeds == 23);
    CHECK(probe->telemetry_living_classes == 7);
    CHECK(probe->telemetry_living_bosses == 1);
    CHECK(probe->telemetry_living_visible == 18);
    CHECK(probe->telemetry_living_offscreen == 5);
    CHECK(probe->telemetry_living_lod_total == 31);
    CHECK(probe->telemetry_living_anim_rate_total == 1380);
    CHECK(probe->telemetry_living_injured_zones == 9);
    CHECK(probe->telemetry_living_required_bones == 1840);
    CHECK(probe->telemetry_living_material_slots == 92);
    CHECK(probe->telemetry_living_attachments == 17);
    CHECK(probe->telemetry_living_anim_skipped == 3);
    CHECK(probe->telemetry_living_bone_atoms_skipped == 4);
    CHECK(probe->telemetry_living_bone_interpolation == 2);
    CHECK(probe->telemetry_living_kinematic_distance_skipped == 1);
    CHECK(probe->telemetry_living_ticks_offscreen == 23);
    CHECK(probe->telemetry_living_special_moves == 8);
    CHECK(probe->telemetry_living_attack_moves == 3);
    CHECK(probe->telemetry_living_grapple_moves == 1);
    CHECK(probe->telemetry_living_stumbles == 1);
    CHECK(probe->telemetry_living_knockdowns == 1);
    CHECK(probe->telemetry_living_hit_reactions == 1);
    CHECK(probe->telemetry_living_other_special_moves == 1);
    CHECK(probe->telemetry_corpse_total == 8);
    CHECK(probe->telemetry_corpse_awake == 2);
    CHECK(probe->telemetry_corpse_sleeping == 5);
    CHECK(probe->telemetry_corpse_other == 1);
    CHECK(probe->telemetry_corpse_final_pose == 4);
    CHECK(probe->telemetry_corpse_visible == 3);
    CHECK(probe->telemetry_corpse_offscreen == 5);
    CHECK(probe->telemetry_corpse_lod_total == 14);
    CHECK(probe->telemetry_corpse_injured_zones == 12);
    CHECK(probe->telemetry_corpse_max_age_ms == 42000);
    CHECK(probe->telemetry_corpse_limit == 12);
    CHECK(probe->telemetry_corpse_offscreen_time_ms == 60000);
    CHECK(probe->telemetry_corpse_offscreen_distance == 5000);
    CHECK(probe->telemetry_dismembered_corpses == 3);
    CHECK(probe->telemetry_dismembered_limbs == 7);
    CHECK(probe->telemetry_ragdoll_warned_corpses == 2);
    CHECK(probe->telemetry_ragdoll_warning_max == 3);
    CHECK(probe->telemetry_corpse_collide_dead == true);
    CHECK(probe->telemetry_corpse_collide_living == true);
    CHECK(probe->telemetry_corpse_collide_dead_after_sleep == false);
    CHECK(probe->telemetry_corpse_collide_living_after_sleep == true);
    CHECK(probe->telemetry_visible_gibs == 11);
    CHECK(probe->telemetry_spray_actors == 6);
    CHECK(probe->telemetry_fire_spray_actors == 3);
    CHECK(probe->telemetry_toxic_spray_actors == 2);
    CHECK(probe->telemetry_other_spray_actors == 1);
    CHECK(probe->telemetry_explosion_actors == 5);
    CHECK(probe->telemetry_damaging_explosion_actors == 4);
    CHECK(probe->telemetry_fire_explosion_actors == 1);
    CHECK(probe->telemetry_toxic_explosion_actors == 2);
    CHECK(probe->telemetry_other_damaging_explosion_actors == 1);
    CHECK(probe->telemetry_unclassified_explosion_actors == 1);
    CHECK(probe->telemetry_lingering_explosion_actors == 2);
    CHECK(probe->telemetry_smoke_explosion_actors == 1);
    CHECK(probe->telemetry_bloat_king_fart_explosion_actors == 1);
    CHECK(probe->telemetry_smoke_grenade_projectiles == 2);
    CHECK(probe->telemetry_puke_mine_projectiles == 3);
    CHECK(probe->telemetry_bloat_king_puke_mine_projectiles == 1);
    CHECK(probe->telemetry_wound_decals == 12);
    CHECK(probe->telemetry_splatter_decals == 9);
    CHECK(probe->telemetry_pool_decals == 4);
    CHECK(probe->telemetry_impact_decals == 7);
    CHECK(probe->telemetry_explosion_decals == 3);
    CHECK(probe->telemetry_wound_decal_limit == 64);
    CHECK(probe->telemetry_splatter_decal_limit == 64);
    CHECK(probe->telemetry_pool_decal_limit == 20);
    CHECK(probe->telemetry_impact_decal_limit == 40);
    CHECK(probe->telemetry_explosion_decal_limit == 20);
    CHECK(probe->telemetry_blood_effect_limit == 25);
    CHECK(probe->telemetry_gore_effect_limit == 25);
    CHECK(probe->telemetry_wound_lifetime_ms == 10000);
    CHECK(probe->telemetry_splatter_lifetime_ms == 15000);
    CHECK(probe->telemetry_pool_lifetime_ms == 30000);
    CHECK(probe->telemetry_gib_lifetime_ms == 20000);
    CHECK(probe->telemetry_gore_particle_components == 6);
    CHECK(probe->telemetry_gore_particles == 345);
    CHECK(probe->telemetry_gore_particle_visible_components == 4);
    CHECK(probe->telemetry_gore_particle_lod_total == 8);
    CHECK(probe->telemetry_gore_particle_bounded_components == 6);
    CHECK(probe->telemetry_world_particle_components == 18);
    CHECK(probe->telemetry_world_particles == 987);
    CHECK(probe->telemetry_world_particle_visible_components == 12);
    CHECK(probe->telemetry_world_particle_lod_total == 21);
    CHECK(probe->telemetry_world_particle_bounded_components == 18);
    CHECK(probe->telemetry_ground_fire_particle_components == 4);
    CHECK(probe->telemetry_ground_fire_particles == 222);
    CHECK(probe->telemetry_impact_particle_components == 5);
    CHECK(probe->telemetry_impact_particles == 333);
    CHECK(probe->telemetry_gore_particle_pool_capacity == 30);
    CHECK(probe->telemetry_world_particle_pool_capacity == 200);
    CHECK(probe->telemetry_ground_fire_particle_pool_capacity == 100);
    CHECK(probe->telemetry_impact_particle_pool_capacity == 60);
    CHECK(probe->telemetry_particle_constant_spawn_emitters == 14);
    CHECK(probe->telemetry_particle_dynamic_spawn_emitters == 10);
    CHECK(probe->telemetry_particle_constant_spawn_rate_milli == 125500);
    CHECK(probe->telemetry_particle_burst_entries == 19);
    CHECK(probe->telemetry_particle_peak_capacity == 2048);
    CHECK(probe->telemetry_particle_flex_components == 5);
    CHECK(probe->telemetry_particle_flex_fluid_components == 3);
    CHECK(probe->telemetry_particle_flex_nonfluid_components == 1);
    CHECK(probe->telemetry_particle_flex_mixed_components == 1);
    CHECK(probe->telemetry_particle_nonflex_components == 17);
    CHECK(probe->telemetry_particle_unclassified_components == 2);
    CHECK(probe->telemetry_flex_surrogate_active == true);
    CHECK(probe->telemetry_flex_surrogate_particles == 72);
    CHECK(probe->telemetry_flex_surrogate_visible == true);
    CHECK(probe->telemetry_flex_surrogate_lod == 2);
    CHECK(probe->telemetry_zed_time_active == true);
    CHECK(probe->telemetry_observed_ns == 4'500'000'000ULL);
    CHECK(describe_game_log_session(*probe).find(
              L"probe: 23 living, 8 corpses (2 active/5 sleeping), 7 detached limbs, 2 ragdoll warnings, 11 gibs, 35 decals, particles 345 gore/987 world (4 ground-fire/5 impact components), effects 6 spray/5 explosion actors, Zed Time active") !=
          std::wstring::npos);
    CHECK(!stream.feed(telemetry_line(8, 4),
                       4'600'000'000ULL).has_value());
    CHECK(!stream.feed(
        "ScriptLog: KF2OPT_TELEMETRY schema=1 sample=9 living=23\n",
        4'700'000'000ULL).has_value());
    CHECK(stream.current()->telemetry_observed_ns == 4'500'000'000ULL);
    const auto expired = stream.expire_observations(19'499'999'999ULL);
    CHECK(expired.has_value());
    CHECK(!expired->zeds_alive.has_value());
    CHECK(!expired->zeds_remaining.has_value());
    CHECK(!expired->wave_number.has_value());
    CHECK(expired->telemetry_living_zeds == 23);
    const auto probe_expired =
        stream.expire_observations(19'500'000'001ULL);
    CHECK(probe_expired.has_value());
    CHECK(!probe_expired->telemetry_living_zeds.has_value());
    CHECK(!probe_expired->telemetry_corpse_total.has_value());
    CHECK(!probe_expired->telemetry_dismembered_limbs.has_value());
    CHECK(!probe_expired->telemetry_living_special_moves.has_value());
    CHECK(!probe_expired->telemetry_puke_mine_projectiles.has_value());
    CHECK(!probe_expired->telemetry_corpse_collide_living_after_sleep.has_value());
    CHECK(!probe_expired->telemetry_world_particles.has_value());
    CHECK(!probe_expired->telemetry_particle_peak_capacity.has_value());
    CHECK(!probe_expired->telemetry_flex_surrogate_active.has_value());
    CHECK(!stream.expire_observations(20'000'000'000ULL).has_value());

    const auto ended = stream.feed(
        "[0376.84] ScriptLog: KFGameInfo_Survival - "
        "MatchEnded.BeginState - AARDisplayDelay: 15.0000\n");
    CHECK(ended.has_value());
    CHECK(ended->phase == GameLogPhase::match_ended);
    CHECK(ended->net_mode == "NM_Standalone");
    CHECK(!ended->zeds_remaining.has_value());
    CHECK(!ended->zeds_alive.has_value());
    CHECK(!ended->wave_number.has_value());
    CHECK(!ended->wave_total_ai.has_value());
    CHECK(!ended->telemetry_living_zeds.has_value());
    CHECK(!game_log_is_active_gameplay(*ended));
    CHECK(!game_log_is_offline_gameplay(*ended));
    CHECK(describe_game_log_session(*ended).find(L"state: match ended") !=
          std::wstring::npos);
    CHECK(!stream.feed(
        "ScriptLog: KFGameInfo_Survival - MatchEnded.BeginState\n").has_value());

    CHECK(stream.feed("Log: LoadMap: KFMainMenu?closed\n")->main_menu);
    CHECK(stream.current()->phase == GameLogPhase::main_menu);
    CHECK(!stream.current()->net_mode.has_value());
    CHECK(!stream.feed(
        "ScriptLog: KFGameInfo_Survival - MatchEnded.BeginState\n").has_value());

    CHECK(parse_net_mode_line(
        "[1] ScriptLog: WI.NetMode:  NM_Client") == "NM_Client");
    CHECK(!parse_net_mode_line(
        "[1] ScriptLog: WI.NetMode:  NM_Client?unsafe").has_value());

    GameLogSessionParser network_stream;
    CHECK(network_stream.feed("Log: LoadMap: KF-Outpost\n").has_value());
    CHECK(network_stream.feed(
        "ScriptLog: WI.NetMode:  NM_Client\n").has_value());
    CHECK(!network_stream.feed(
        "ScriptLog: @@@@ ZED COUNT DEBUG: AIAliveCount = 12\n")
               .has_value());
    CHECK(!network_stream.current()->zeds_alive.has_value());
    CHECK(!network_stream.feed(empty_telemetry_line()).has_value());
    CHECK(!network_stream.current()->telemetry_living_zeds.has_value());
    stream.reset();
    CHECK(!stream.current().has_value());
    CHECK(stream.stats().bytes_received == 0);
    std::string oversized(16 * 1024 + 1, 'x');
    CHECK(!stream.feed(oversized).has_value());
    CHECK(stream.stats().oversized_line_drops == 1);
    CHECK(stream.stats().bytes_received == oversized.size());
    std::string oversized_chunk(64 * 1024 + 1, 'y');
    CHECK(!stream.feed(oversized_chunk).has_value());
    CHECK(stream.stats().oversized_input_resets == 1);
    CHECK(stream.stats().oversized_line_drops == 2);
    CHECK(stream.stats().bytes_received ==
          oversized.size() + oversized_chunk.size());
    stream.reset();
    CHECK(stream.stats().oversized_line_drops == 0);
    CHECK(game_log_belongs_to_process(1'050, 1'000));
    CHECK(game_log_belongs_to_process(1'000, 1'000));
    CHECK(!game_log_belongs_to_process(999, 1'000));
    CHECK(!game_log_belongs_to_process(0, 1'000));
    CHECK(!game_log_belongs_to_process(1'000, 0));
    CHECK(game_log_reports_engine_exit(
        "[0004.29] Exit: Exiting.\n"));
    CHECK(game_log_reports_engine_exit(
        "[0004.29] Log: Log file closed, 08/22/26 22:34:02\n"));
    CHECK(!game_log_reports_engine_exit(
        "[0004.29] Exit: Preparing to exit.\n"));
    CHECK(!game_log_reports_engine_exit(
        "WidgetInitialized - WidgetName:  StartMenu\n"));
    return EXIT_SUCCESS;
}
