#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
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
#include "kf2/platform/windows/atomic_file.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

struct OwnedHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~OwnedHandle() { close(); }
    void close() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};

template <typename Predicate>
bool await(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return true;
}

}  // namespace

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

    EventLog retained{3};
    retained.append(Event{0, Severity::info,
                          "ADAPTIVE_RUNTIME_QUALITY_APPLIED",
                          L"verified", L"test"});
    retained.append(Event{0, Severity::info, "ADAPTIVE_DECISION",
                          L"first", L"test"});
    retained.append(Event{0, Severity::info, "ADAPTIVE_DECISION",
                          L"second", L"test"});
    retained.append(Event{0, Severity::info, "ADAPTIVE_DECISION",
                          L"third", L"test"});
    const auto retained_events = retained.snapshot();
    CHECK(retained_events.size() == 3);
    CHECK(retained_events.front().code ==
          "ADAPTIVE_RUNTIME_QUALITY_APPLIED");
    CHECK(retained_events.front().retained_audit);
    CHECK(retained_events[1].message == L"second");
    CHECK(retained_events[2].message == L"third");

    EventLog retained_failures{2};
    retained_failures.append(Event{0, Severity::warning, "QUALITY_FAILED",
                                   L"failed", L"test"});
    retained_failures.append(Event{0, Severity::info, "SESSION_CONFIG_RESTORED",
                                   L"restored", L"test"});
    retained_failures.append(Event{0, Severity::info, "ADAPTIVE_DECISION",
                                   L"measurement", L"test"});
    const auto retained_failure_events = retained_failures.snapshot();
    CHECK(retained_failure_events.size() == 2);
    CHECK(retained_failure_events[0].code == "QUALITY_FAILED");
    CHECK(retained_failure_events[1].code == "SESSION_CONFIG_RESTORED");

    EventLog long_session{512};
    long_session.append(Event{0, Severity::info,
                              "ADAPTIVE_RUNTIME_QUALITY_APPLIED",
                              L"exact APPLIED readback", L"optimizer"});
    for (int index = 0; index < 600; ++index) {
        long_session.append(Event{0, Severity::info, "ADAPTIVE_DECISION",
                                  L"measurement " + std::to_wstring(index),
                                  L"optimizer"});
    }
    const auto long_session_events = long_session.snapshot();
    CHECK(long_session_events.size() == 512);
    CHECK(long_session_events.front().code ==
          "ADAPTIVE_RUNTIME_QUALITY_APPLIED");
    const auto long_session_json =
        kf2::diagnostics::serialize_events_json(long_session_events);
    CHECK(long_session_json.find("retained_audit") == std::string::npos);
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
    CHECK(persistent.flush(std::chrono::seconds{2}));
    {
        std::ifstream persisted(persistent_path, std::ios::binary);
        const std::string persisted_json{std::istreambuf_iterator<char>{persisted},
                                         std::istreambuf_iterator<char>{}};
        CHECK(persisted_json.find("\"repeat_count\":2") != std::string::npos);
    }
    persistent.clear();
    CHECK(persistent.flush(std::chrono::seconds{2}));
    CHECK(std::filesystem::file_size(persistent_path) > 0);

    // Ordinary appends share one bounded batch, not one full file rewrite each.
    const auto batch_path = persistent_root / L"batch.json";
    std::atomic<int> batch_writes{0};
    std::mutex batch_mutex;
    std::condition_variable batch_changed;
    const auto batch_writer = [&](const std::filesystem::path& path,
                                  std::string_view bytes) {
        auto result = kf2::platform::windows::atomic_replace_utf8(path, bytes);
        {
            std::scoped_lock lock{batch_mutex};
            ++batch_writes;
        }
        batch_changed.notify_all();
        return result;
    };
    {
        EventLog batched{128, batch_path, batch_writer};
        CHECK(batch_writes == 1);  // Initial empty atomic document.
        for (int index = 0; index < 100; ++index) {
            batched.append(Event{0, Severity::info, "BATCH",
                std::to_wstring(index), L"test"});
        }
        {
            std::unique_lock lock{batch_mutex};
            CHECK(!batch_changed.wait_for(lock, std::chrono::milliseconds{100},
                                          [&] { return batch_writes > 1; }));
        }
        CHECK(batched.flush(std::chrono::seconds{2}));
        CHECK(batch_writes == 2);
        std::ifstream input(batch_path, std::ios::binary);
        const std::string saved{std::istreambuf_iterator<char>{input}, {}};
        CHECK(saved == kf2::diagnostics::serialize_events_json(batched.snapshot()));
        input.close();

        // An explicit flush bypasses the fresh batch deadline, including clear.
        batched.clear();
        std::barrier start_flush{3};
        bool first_flushed = false;
        bool second_flushed = false;
        std::jthread first_flush{[&] {
            start_flush.arrive_and_wait();
            first_flushed = batched.flush(std::chrono::milliseconds{100});
        }};
        std::jthread second_flush{[&] {
            start_flush.arrive_and_wait();
            second_flushed = batched.flush(std::chrono::milliseconds{100});
        }};
        start_flush.arrive_and_wait();
        first_flush.join();
        second_flush.join();
        CHECK(first_flushed && second_flushed);
        CHECK(batch_writes == 3);
        CHECK(std::filesystem::file_size(batch_path) ==
              std::string_view{"{\"version\":1,\"events\":[]}"}.size());
        batched.append(Event{0, Severity::info, "DUP", L"same", L"test"});
        batched.append(Event{0, Severity::info, "DUP", L"same", L"test"});
        CHECK(batched.flush(std::chrono::milliseconds{100}));
        CHECK(batch_writes == 4);
        CHECK(batched.snapshot().front().repeat_count == 2);
    }
    batch_writes = 0;
    {
        EventLog scheduled{4, batch_path, batch_writer};
        scheduled.append(Event{0, Severity::info, "DEADLINE", L"latest", L"test"});
        CHECK(await([&] { return batch_writes == 2; }));
        CHECK(scheduled.flush(std::chrono::seconds{2}));
        {
            std::unique_lock lock{batch_mutex};
            CHECK(!batch_changed.wait_for(lock, std::chrono::milliseconds{350},
                                          [&] { return batch_writes > 2; }));
        }
        CHECK(batch_writes == 2);  // Idle time must not generate more writes.
    }
    batch_writes = 0;
    {
        EventLog continuous{4, batch_path, batch_writer};
        std::jthread producer{[&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                continuous.append(Event{0, Severity::info, "CONTINUOUS",
                    L"same", L"test"});
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
        }};
        // Later appends cannot keep postponing the first pending deadline.
        CHECK(await([&] { return batch_writes >= 2; }));
        producer.request_stop();
        producer.join();
        CHECK(continuous.flush(std::chrono::seconds{2}));
        std::ifstream input(batch_path, std::ios::binary);
        const std::string saved{std::istreambuf_iterator<char>{input}, {}};
        CHECK(saved == kf2::diagnostics::serialize_events_json(continuous.snapshot()));
    }

    const auto asynchronous_path = persistent_root / L"asynchronous.json";
    std::mutex writer_mutex;
    std::condition_variable writer_changed;
    bool writer_blocked = false;
    bool release_writer = false;
    std::atomic<int> writes{0};
    {
        EventLog asynchronous{
            4, asynchronous_path,
            [&](const std::filesystem::path& path, std::string_view bytes) {
                if (writes.fetch_add(1) > 0) {
                    std::unique_lock lock{writer_mutex};
                    writer_blocked = true;
                    writer_changed.notify_all();
                    writer_changed.wait(lock, [&] { return release_writer; });
                }
                return kf2::platform::windows::atomic_replace_utf8(path, bytes);
            }};
        asynchronous.append(
            Event{0, Severity::info, "ASYNC", L"first", L"test"});
        {
            std::unique_lock lock{writer_mutex};
            CHECK(writer_changed.wait_for(lock, std::chrono::seconds{2},
                                          [&] { return writer_blocked; }));
        }
        const auto append_started = std::chrono::steady_clock::now();
        asynchronous.append(
            Event{0, Severity::info, "ASYNC", L"second", L"test"});
        const auto append_elapsed = std::chrono::steady_clock::now() -
                                    append_started;
        CHECK(append_elapsed < std::chrono::milliseconds{100});
        {
            std::scoped_lock lock{writer_mutex};
            release_writer = true;
        }
        writer_changed.notify_all();
        CHECK(asynchronous.flush(std::chrono::seconds{2}));
        std::ifstream persisted(asynchronous_path, std::ios::binary);
        const std::string persisted_json{
            std::istreambuf_iterator<char>{persisted},
            std::istreambuf_iterator<char>{}};
        CHECK(persisted_json.find("\"message\":\"second\"") !=
              std::string::npos);
    }
    // Real Windows sharing failures must not permanently disable persistence.
    const auto retry_path = persistent_root / L"retry.json";
    {
        std::ofstream output(retry_path);
        output << "original evidence";
    }
    {
        OwnedHandle blocked{CreateFileW(retry_path.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(blocked.value != INVALID_HANDLE_VALUE);
        EventLog retry{4, retry_path};
        CHECK(!retry.persistence_ready());
        CHECK(retry.stats().persistence_failures >= 1);
        retry.append(Event{0, Severity::info, "LATEST", L"startup retry", L"test"});
        blocked.close();
        CHECK(retry.flush(std::chrono::seconds{2}));
        CHECK(retry.persistence_ready());
        std::ifstream input(retry_path);
        const std::string saved{std::istreambuf_iterator<char>{input}, {}};
        CHECK(saved.find("startup retry") != std::string::npos);
    }
    {
        EventLog retry{4, retry_path};
        OwnedHandle blocked{CreateFileW(retry_path.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(blocked.value != INVALID_HANDLE_VALUE);
        retry.append(Event{0, Severity::info, "LATEST", L"locked", L"test"});
        CHECK(await([&] { return !retry.persistence_ready(); }));
        for (int index = 0; index < 2000; ++index) {
            retry.append(Event{0, Severity::info, "LATEST",
                std::to_wstring(index), L"test"});
        }
        CHECK(retry.snapshot().size() == 4);
        blocked.close();
        CHECK(retry.flush(std::chrono::seconds{2}));
        CHECK(retry.persistence_ready());
        std::ifstream input(retry_path);
        const std::string saved{std::istreambuf_iterator<char>{input}, {}};
        CHECK(saved.find("1999") != std::string::npos);
        CHECK(saved.find("\"events\":[]") == std::string::npos);
    }
    // The existing writer seam exercises false results, errors and exceptions.
    // Recovery must not require another append, nor persist the failed copy.
    for (const int failure : {0, 1, 2}) {
        std::atomic<bool> fail{true};
        std::string saved;
        EventLog retry{4, retry_path,
            [&](const std::filesystem::path&, std::string_view bytes) {
                if (fail.load()) {
                    if (failure == 0) return kf2::Result<bool>::success(false);
                    if (failure == 1) return kf2::Result<bool>::failure(
                        {kf2::ErrorCode::io_failure, L"test write failure", 0});
                    throw std::runtime_error{"test write failure"};
                }
                saved = std::string{bytes};
                return kf2::Result<bool>::success(true);
            }};
        CHECK(!retry.persistence_ready());
        retry.append(Event{0, Severity::info, "LATEST", L"newest", L"test"});
        fail.store(false);
        CHECK(retry.flush(std::chrono::seconds{2}));
        CHECK(retry.persistence_ready());
        CHECK(saved.find("newest") != std::string::npos);
        fail.store(true);
        retry.clear();
        CHECK(await([&] { return !retry.persistence_ready(); }));
        fail.store(false);
        CHECK(retry.flush(std::chrono::seconds{2}));
        CHECK(saved == "{\"version\":1,\"events\":[]}");
    }
    std::atomic<int> failed_writes{0};
    auto stop_started = std::chrono::steady_clock::now();
    {
        EventLog unavailable{4, retry_path,
            [&](const std::filesystem::path&, std::string_view) {
                ++failed_writes;
                return kf2::Result<bool>::success(false);
            }};
        for (int index = 0; index < 2000; ++index) {
            unavailable.append(Event{0, Severity::info, "LATEST",
                std::to_wstring(index), L"test"});
        }
        const auto flush_started = std::chrono::steady_clock::now();
        CHECK(!unavailable.flush(std::chrono::milliseconds{550}));
        CHECK(std::chrono::steady_clock::now() - flush_started < std::chrono::seconds{2});
        CHECK(failed_writes >= 2 && failed_writes <= 4);
        CHECK(!unavailable.persistence_ready());
        CHECK(unavailable.snapshot().size() == 4);
        stop_started = std::chrono::steady_clock::now();
    }
    CHECK(std::chrono::steady_clock::now() - stop_started < std::chrono::seconds{2});
    {
        EventLog final_drain{4, retry_path};
        for (int index = 0; index < 100; ++index) {
            final_drain.append(Event{0, Severity::info, "FINAL",
                L"last " + std::to_wstring(index), L"test"});
        }
        // Healthy destruction retains the existing final-drain behavior.
    }
    {
        std::ifstream input(retry_path);
        const std::string saved{std::istreambuf_iterator<char>{input}, {}};
        CHECK(saved.find("last 99") != std::string::npos);
    }
    int unsafe_writes = 0;
    EventLog unsafe{4, L"relative.json",
        [&](const std::filesystem::path&, std::string_view) {
            ++unsafe_writes;
            return kf2::Result<bool>::success(true);
        }};
    unsafe.append(Event{0, Severity::info, "LATEST", L"unsafe", L"test"});
    CHECK(!unsafe.flush(std::chrono::milliseconds{10}));
    CHECK(unsafe_writes == 0 && !unsafe.persistence_ready());
    std::filesystem::remove_all(persistent_root);

    const auto rotation_root = std::filesystem::temp_directory_path() /
        (L"kf2-event-log-rotation-" +
         std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(rotation_root);
    std::filesystem::create_directories(rotation_root);
    const auto current_log = rotation_root / L"session-events.json";
    const auto previous_log = rotation_root / L"previous-session-events.json";
    const auto recovery_log = rotation_root / L"session-events-recovery.json";
    const auto write_rotation_log = [](const std::filesystem::path& path,
                                       std::string_view bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << bytes;
    };
    const auto read_rotation_log = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};
    };

    write_rotation_log(current_log, "successful rotation");
    const auto successful_rotation =
        kf2::diagnostics::prepare_event_log_rotation(rotation_root);
    CHECK(successful_rotation.disposition ==
          kf2::diagnostics::PreviousEventLogDisposition::archived);
    CHECK(successful_rotation.persistence_path == current_log);
    CHECK(read_rotation_log(current_log) == "successful rotation");
    CHECK(read_rotation_log(previous_log) == "successful rotation");

    write_rotation_log(current_log, "retry after read failure");
    HANDLE blocked_current = CreateFileW(
        current_log.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(blocked_current != INVALID_HANDLE_VALUE);
    const auto read_failure =
        kf2::diagnostics::prepare_event_log_rotation(rotation_root);
    CHECK(read_failure.disposition ==
          kf2::diagnostics::PreviousEventLogDisposition::deferred);
    CHECK(read_failure.persistence_path == recovery_log);
    CHECK(read_failure.warning.has_value());
    CloseHandle(blocked_current);
    CHECK(read_rotation_log(current_log) == "retry after read failure");
    const auto recovered_rotation =
        kf2::diagnostics::prepare_event_log_rotation(rotation_root);
    CHECK(recovered_rotation.disposition ==
          kf2::diagnostics::PreviousEventLogDisposition::archived);
    CHECK(read_rotation_log(previous_log) == "retry after read failure");

    write_rotation_log(current_log, "retained after archive failure");
    HANDLE blocked_archive = CreateFileW(
        previous_log.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(blocked_archive != INVALID_HANDLE_VALUE);
    const auto archive_failure =
        kf2::diagnostics::prepare_event_log_rotation(rotation_root);
    CHECK(archive_failure.disposition ==
          kf2::diagnostics::PreviousEventLogDisposition::retained);
    CHECK(archive_failure.persistence_path == current_log);
    CHECK(archive_failure.preserved_path.has_value());
    CHECK(archive_failure.warning.has_value());
    CHECK(read_rotation_log(*archive_failure.preserved_path) ==
          "retained after archive failure");
    CHECK(!std::filesystem::exists(current_log));
    CloseHandle(blocked_archive);

    write_rotation_log(current_log, "unsafe linked evidence");
    const auto linked_log = rotation_root / L"linked-session-events.json";
    CHECK(CreateHardLinkW(linked_log.c_str(), current_log.c_str(), nullptr) !=
          FALSE);
    const auto unsafe_rotation =
        kf2::diagnostics::prepare_event_log_rotation(rotation_root);
    CHECK(unsafe_rotation.disposition ==
          kf2::diagnostics::PreviousEventLogDisposition::deferred);
    CHECK(unsafe_rotation.persistence_path == recovery_log);
    CHECK(read_rotation_log(current_log) == "unsafe linked evidence");
    CHECK(read_rotation_log(linked_log) == "unsafe linked evidence");
    std::filesystem::remove_all(rotation_root);

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
        .optimizer_profile = L"user settings",
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
        .living_updates_skeleton_offscreen = 7,
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
                           .oversized_line_drops = 2,
                           .session_snapshot_copies = 3},
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
                       "\"living_updates_skeleton_offscreen\":7,"
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
    CHECK(product.find("\"quality_policy\"") == std::string::npos);
    CHECK(product.find("\"optimizer\":{\"profile\":\"user settings\","
                       "\"target_fps\":62") != std::string::npos);
    CHECK(product.find("\"performance_analysis\":\"p95 16.2 ms") !=
          std::string::npos);
    CHECK(product.find("\"events\":[") != std::string::npos);
    CHECK(product.find("\"event_log_stats\":{\"appended\":7") !=
          std::string::npos);
    CHECK(product.find("\"crash_records\":{\"retained\":3,\"content_included\":false}") !=
          std::string::npos);
    CHECK(product.find("\"game_log_stats\":{\"bytes_received\":100") !=
          std::string::npos);
    CHECK(product.find("\"session_snapshot_copies\":3") !=
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
