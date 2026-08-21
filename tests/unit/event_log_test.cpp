#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include <Windows.h>

#include "kf2/diagnostics/event_log.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using kf2::diagnostics::Event;
    using kf2::diagnostics::EventLog;
    using kf2::diagnostics::Severity;

    EventLog bounded{2};
    bounded.append(Event{0, Severity::info, "A", L"first", L"test"});
    bounded.append(Event{0, Severity::warning, "B", L"second", L"test"});
    bounded.append(Event{0, Severity::error, "C", L"third", L"test"});
    const auto events = bounded.snapshot();
    CHECK(events.size() == 2);
    CHECK(events[0].code == "B" && events[1].code == "C");
    CHECK(events[0].sequence < events[1].sequence);
    CHECK(bounded.stats().appended == 3);
    CHECK(bounded.stats().overwritten == 1);
    CHECK(bounded.stats().deduplicated == 0);
    const auto json = kf2::diagnostics::serialize_events_json(events);
    CHECK(json.find("\"version\":1") != std::string::npos);
    CHECK(json.find("\"severity\":\"warning\"") != std::string::npos);
    CHECK(json.find("\"code\":\"C\"") != std::string::npos);

    const auto persistent_root = std::filesystem::temp_directory_path() /
        (L"kf2-event-log-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(persistent_root);
    const auto persistent_path = persistent_root / L"events.json";
    EventLog persistent{4, persistent_path};
    CHECK(persistent.persistence_ready());
    persistent.append(Event{0, Severity::warning, "DUP", L"same", L"test"});
    persistent.append(Event{0, Severity::warning, "DUP", L"same", L"test"});
    CHECK(persistent.snapshot().size() == 1);
    CHECK(persistent.snapshot().front().repeat_count == 2);
    CHECK(persistent.stats().appended == 2);
    CHECK(persistent.stats().deduplicated == 1);
    {
        std::ifstream persisted(persistent_path, std::ios::binary);
        const std::string persisted_json{std::istreambuf_iterator<char>{persisted},
                                         std::istreambuf_iterator<char>{}};
        CHECK(persisted_json.find("\"repeat_count\":2") != std::string::npos);
    }
    persistent.clear();
    CHECK(std::filesystem::file_size(persistent_path) > 0);
    std::filesystem::remove_all(persistent_root);

    EventLog escaped{1};
    escaped.append(Event{0, Severity::info, "QUOTE\"", L"line\ntext", L"quelle"});
    const auto escaped_json = kf2::diagnostics::serialize_events_json(escaped.snapshot());
    CHECK(escaped_json.find("QUOTE\\\"") != std::string::npos);
    CHECK(escaped_json.find("line\\ntext") != std::string::npos);

    kf2::diagnostics::ProductReport report{
        .build_identity = L"1.2.3+abc (release)",
        .mode = L"Adaptive / Automatic",
        .game = L"Game detected: D:\\KF2",
        .game_session = L"KF-BioticsLab",
        .telemetry = L"62 FPS",
        .performance_analysis = L"p95 16.2 ms | p99 17.1 ms | stutters 0",
        .hardware = L"CPU 16 | GPU Test",
        .flex = L"FleX healthy",
        .optimizer_profile = L"balanced",
        .quality_policy = L"exact",
        .overlay_position = L"top right",
        .target_fps = 62,
        .overlay_scale_percent = 100,
        .overlay_enabled = true,
        .restore_config_after_game = true,
        .game_pid = 123,
        .game_process_start_id = 456,
        .zeds_alive = 17,
        .zeds_remaining = 42,
        .wave_number = 3,
        .wave_total_ai = 93,
        .telemetry_living_zeds = 16,
        .living_classes = 7,
        .living_bosses = 1,
        .living_visible = 12,
        .living_offscreen = 4,
        .living_lod_total = 25,
        .living_anim_rate_total = 960,
        .living_injured_zones = 9,
        .living_required_bones = 1840,
        .living_material_slots = 92,
        .living_attachments = 17,
        .living_anim_skipped = 3,
        .living_bone_atoms_skipped = 4,
        .living_bone_interpolation = 2,
        .living_kinematic_distance_skipped = 1,
        .living_ticks_offscreen = 16,
        .living_special_moves = 8,
        .living_attack_moves = 3,
        .living_grapple_moves = 1,
        .living_stumbles = 1,
        .living_knockdowns = 1,
        .living_hit_reactions = 1,
        .living_other_special_moves = 1,
        .corpse_total = 8,
        .corpse_awake = 2,
        .corpse_sleeping = 5,
        .corpse_other = 1,
        .corpse_final_pose = 4,
        .corpse_visible = 3,
        .corpse_offscreen = 5,
        .corpse_lod_total = 14,
        .corpse_injured_zones = 12,
        .corpse_max_age_ms = 42000,
        .corpse_limit = 12,
        .corpse_offscreen_time_ms = 60000,
        .corpse_offscreen_distance = 5000,
        .dismembered_corpses = 3,
        .dismembered_limbs = 7,
        .ragdoll_warned_corpses = 2,
        .ragdoll_warning_max = 3,
        .corpse_collide_dead = true,
        .corpse_collide_living = true,
        .corpse_collide_dead_after_sleep = false,
        .corpse_collide_living_after_sleep = true,
        .visible_gibs = 11,
        .spray_actors = 6,
        .fire_spray_actors = 3,
        .toxic_spray_actors = 2,
        .other_spray_actors = 1,
        .explosion_actors = 5,
        .damaging_explosion_actors = 4,
        .fire_explosion_actors = 1,
        .toxic_explosion_actors = 2,
        .other_damaging_explosion_actors = 1,
        .unclassified_explosion_actors = 1,
        .lingering_explosion_actors = 2,
        .smoke_explosion_actors = 1,
        .bloat_king_fart_explosion_actors = 1,
        .smoke_grenade_projectiles = 2,
        .puke_mine_projectiles = 3,
        .bloat_king_puke_mine_projectiles = 1,
        .wound_decals = 12,
        .splatter_decals = 9,
        .pool_decals = 4,
        .impact_decals = 7,
        .explosion_decals = 3,
        .wound_decal_limit = 64,
        .splatter_decal_limit = 64,
        .pool_decal_limit = 20,
        .impact_decal_limit = 40,
        .explosion_decal_limit = 20,
        .blood_effect_limit = 25,
        .gore_effect_limit = 25,
        .wound_lifetime_ms = 10000,
        .splatter_lifetime_ms = 15000,
        .pool_lifetime_ms = 30000,
        .gib_lifetime_ms = 20000,
        .gore_particle_components = 6,
        .gore_particles = 345,
        .gore_particle_visible_components = 4,
        .gore_particle_lod_total = 8,
        .gore_particle_bounded_components = 6,
        .world_particle_components = 18,
        .world_particles = 987,
        .world_particle_visible_components = 12,
        .world_particle_lod_total = 21,
        .world_particle_bounded_components = 18,
        .ground_fire_particle_components = 4,
        .ground_fire_particles = 222,
        .impact_particle_components = 5,
        .impact_particles = 333,
        .gore_particle_pool_capacity = 30,
        .world_particle_pool_capacity = 200,
        .ground_fire_particle_pool_capacity = 100,
        .impact_particle_pool_capacity = 60,
        .particle_constant_spawn_emitters = 14,
        .particle_dynamic_spawn_emitters = 10,
        .particle_constant_spawn_rate_milli = 125500,
        .particle_burst_entries = 19,
        .particle_peak_capacity = 2048,
        .particle_flex_components = 5,
        .particle_flex_fluid_components = 3,
        .particle_flex_nonfluid_components = 1,
        .particle_flex_mixed_components = 1,
        .particle_nonflex_components = 17,
        .particle_unclassified_components = 2,
        .flex_surrogate_active = true,
        .flex_surrogate_particles = 72,
        .flex_surrogate_visible = true,
        .flex_surrogate_lod = 2,
        .zed_time_active = true,
        .gameplay_snapshot_age_ms = 250,
        .gameplay_snapshot_fresh = true,
        .offline_gameplay = true,
        .event_log_stats = {.appended = 7, .deduplicated = 2,
                            .overwritten = 1, .persistence_failures = 0},
        .game_log_stats = {.bytes_received = 100, .lines_processed = 5,
                           .oversized_input_resets = 1,
                           .oversized_line_drops = 2},
        .retained_crash_records = 3,
        .events = events,
    };
    const auto product = kf2::diagnostics::serialize_product_report_json(report);
    CHECK(product.find("KF2_OPTIMIZER_DIAGNOSTICS_V2") != std::string::npos);
    CHECK(product.find("\"target_fps\":62") != std::string::npos);
    CHECK(product.find("\"game_pid\":123") != std::string::npos);
    CHECK(product.find("\"gameplay\":{\"offline_verified\":true") !=
          std::string::npos);
    CHECK(product.find("\"telemetry_living_zeds\":16,\"living_classes\":7,"
                       "\"living_bosses\":1,\"living_visible\":12,"
                       "\"living_offscreen\":4,\"living_lod_total\":25,"
                       "\"living_anim_rate_total\":960,"
                       "\"living_injured_zones\":9,"
                       "\"living_required_bones\":1840,"
                       "\"living_material_slots\":92,"
                       "\"living_attachments\":17,"
                       "\"living_anim_skipped\":3,"
                       "\"living_bone_atoms_skipped\":4,"
                       "\"living_bone_interpolation\":2,"
                       "\"living_kinematic_distance_skipped\":1,"
                       "\"living_ticks_offscreen\":16,"
                       "\"living_special_moves\":8,"
                       "\"living_attack_moves\":3,"
                       "\"living_grapple_moves\":1,"
                       "\"living_stumbles\":1,"
                       "\"living_knockdowns\":1,"
                       "\"living_hit_reactions\":1,"
                       "\"living_other_special_moves\":1") !=
          std::string::npos);
    CHECK(product.find("\"damaging_explosion_actors\":4,"
                       "\"fire_explosion_actors\":1,"
                       "\"toxic_explosion_actors\":2,"
                       "\"other_damaging_explosion_actors\":1,"
                       "\"unclassified_explosion_actors\":1,"
                       "\"lingering_explosion_actors\":2,"
                       "\"smoke_explosion_actors\":1,"
                       "\"bloat_king_fart_explosion_actors\":1,"
                       "\"smoke_grenade_projectiles\":2,"
                       "\"puke_mine_projectiles\":3,"
                       "\"bloat_king_puke_mine_projectiles\":1") !=
          std::string::npos);
    CHECK(product.find("\"corpse_max_age_ms\":42000,\"corpse_limit\":12,"
                       "\"corpse_offscreen_time_ms\":60000,"
                       "\"corpse_offscreen_distance\":5000") !=
          std::string::npos);
    CHECK(product.find("\"dismembered_corpses\":3,"
                       "\"dismembered_limbs\":7,"
                       "\"ragdoll_warned_corpses\":2,"
                       "\"ragdoll_warning_max\":3,"
                       "\"corpse_collide_dead\":true,"
                       "\"corpse_collide_living\":true,"
                       "\"corpse_collide_dead_after_sleep\":false,"
                       "\"corpse_collide_living_after_sleep\":true") !=
          std::string::npos);
    CHECK(product.find("\"wound_decal_limit\":64,"
                       "\"splatter_decal_limit\":64,"
                       "\"pool_decal_limit\":20,"
                       "\"impact_decal_limit\":40,"
                       "\"explosion_decal_limit\":20,"
                       "\"blood_effect_limit\":25,"
                       "\"gore_effect_limit\":25") != std::string::npos);
    CHECK(product.find("\"gore_particle_visible_components\":4,"
                       "\"gore_particle_lod_total\":8,"
                       "\"gore_particle_bounded_components\":6") !=
          std::string::npos);
    CHECK(product.find("\"world_particle_visible_components\":12,"
                       "\"world_particle_lod_total\":21,"
                       "\"world_particle_bounded_components\":18,"
                       "\"ground_fire_particle_components\":4,"
                       "\"ground_fire_particles\":222,"
                       "\"impact_particle_components\":5,"
                       "\"impact_particles\":333,"
                       "\"gore_particle_pool_capacity\":30,"
                       "\"world_particle_pool_capacity\":200,"
                       "\"ground_fire_particle_pool_capacity\":100,"
                       "\"impact_particle_pool_capacity\":60,"
                       "\"particle_constant_spawn_emitters\":14,"
                       "\"particle_dynamic_spawn_emitters\":10,"
                       "\"particle_constant_spawn_rate_milli\":125500,"
                       "\"particle_burst_entries\":19,"
                       "\"particle_peak_capacity\":2048,"
                       "\"particle_flex_components\":5,"
                       "\"particle_flex_fluid_components\":3,"
                       "\"particle_flex_nonfluid_components\":1,"
                       "\"particle_flex_mixed_components\":1,"
                       "\"particle_nonflex_components\":17,"
                       "\"particle_unclassified_components\":2,"
                       "\"flex_surrogate_particles\":72,"
                       "\"flex_surrogate_lod\":2,"
                       "\"flex_surrogate_active\":true,"
                       "\"flex_surrogate_visible\":true,"
                       "\"zed_time_active\":true,\"snapshot_fresh\":true,"
                       "\"oldest_snapshot_age_ms\":250}") !=
          std::string::npos);
    CHECK(product.find("\"performance_analysis\":\"p95 16.2 ms") !=
          std::string::npos);
    CHECK(product.find("\"events\":[") != std::string::npos);
    CHECK(product.find("\"event_log_stats\":{\"appended\":7") !=
          std::string::npos);
    CHECK(product.find("\"crash_records\":{\"retained\":3,\"content_included\":false}") !=
          std::string::npos);
    CHECK(product.find("\"game_log_stats\":{\"bytes_received\":100") !=
          std::string::npos);
    const auto support = kf2::diagnostics::serialize_support_bundle_json(
        report, "{\"schema\":\"KF2_ISSUE72_INVENTORY_V3\"}");
    CHECK(support.find("KF2_OPTIMIZER_SUPPORT_BUNDLE_V1") != std::string::npos);
    CHECK(support.find("\"issue72_inventory\":{\"schema\":") !=
          std::string::npos);
    CHECK(support.find("command line") != std::string::npos);
    CHECK(kf2::diagnostics::serialize_support_bundle_json(report, "invalid")
              .find("\"issue72_inventory\":null") != std::string::npos);

    EventLog concurrent{200};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&concurrent, worker] {
            for (int item = 0; item < 50; ++item) {
                concurrent.append(Event{0, Severity::info,
                                        "W" + std::to_string(worker),
                                        L"item " + std::to_wstring(item),
                                        L"concurrency-test"});
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto concurrent_events = concurrent.snapshot();
    CHECK(concurrent_events.size() == 200);
    std::set<std::uint64_t> sequences;
    for (const auto& event : concurrent_events) {
        sequences.insert(event.sequence);
    }
    CHECK(sequences.size() == 200);

    concurrent.clear();
    CHECK(concurrent.snapshot().empty());

    bool zero_rejected = false;
    try {
        EventLog invalid{0};
    } catch (const std::invalid_argument&) {
        zero_rejected = true;
    }
    CHECK(zero_rejected);
    return EXIT_SUCCESS;
}
