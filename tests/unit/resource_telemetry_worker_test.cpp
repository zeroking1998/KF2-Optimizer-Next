#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <pdhmsg.h>

#include "kf2/telemetry/resource_telemetry_worker.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

using namespace std::chrono_literals;
using kf2::telemetry::ResourceSampleBatch;
using kf2::telemetry::ResourceSampleGroup;
using kf2::telemetry::ResourceSampleRequest;
using kf2::telemetry::ResourceTelemetryBinding;
using kf2::telemetry::ResourceTelemetryWorker;

std::filesystem::path replacement_game_log;
std::atomic_bool game_log_replaced{false};
std::string game_log_append_during_read;
ResourceTelemetryWorker* rebinding_log_worker{nullptr};
ResourceTelemetryBinding rebinding_log_binding;

void rebind_game_log_during_read(const std::filesystem::path&) {
    rebinding_log_worker->clear();
    static_cast<void>(rebinding_log_worker->bind(rebinding_log_binding));
}

void append_game_log_during_read(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << game_log_append_during_read;
}

void replace_game_log_during_read(const std::filesystem::path& path) {
    game_log_replaced.store(
        ReplaceFileW(path.c_str(), replacement_game_log.c_str(), nullptr,
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE,
        std::memory_order_release);
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::shared_ptr<const kf2::telemetry::ResourceTelemetrySnapshot>
wait_for_generation(ResourceTelemetryWorker& worker, std::uint64_t generation) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = worker.latest();
        if (snapshot && snapshot->generation == generation) return snapshot;
        std::this_thread::sleep_for(1ms);
    }
    return {};
}

ResourceTelemetryBinding binding(std::uint32_t pid,
                                 std::uint64_t process_start_id,
                                 std::uint64_t adapter_luid = 0) {
    ResourceTelemetryBinding result;
    result.identity = {pid, process_start_id};
    if (adapter_luid != 0) result.adapter_luid = adapter_luid;
    result.adapter_name = L"Test adapter";
    result.adapter_vendor_id = 0x10DE;
    return result;
}

void truncate_game_log_during_read(const std::filesystem::path& path) {
    write_file(path, {});
}

std::uint32_t pdh_opens = 0;
std::uint32_t pdh_closes = 0;
std::uint32_t pdh_failures = 0;
std::uint32_t nvidia_creates = 0;
std::uint32_t nvidia_failures = 0;

PDH_STATUS WINAPI open_gpu_query(LPCWSTR, DWORD_PTR, PDH_HQUERY* query) {
    ++pdh_opens;
    if (pdh_opens <= pdh_failures) return PDH_INVALID_HANDLE;
    *query = reinterpret_cast<PDH_HQUERY>(100);
    return ERROR_SUCCESS;
}
PDH_STATUS WINAPI add_gpu_counter(PDH_HQUERY, LPCWSTR, DWORD_PTR,
                                  PDH_HCOUNTER* counter) {
    *counter = reinterpret_cast<PDH_HCOUNTER>(1);
    return ERROR_SUCCESS;
}
PDH_STATUS WINAPI collect_gpu_query(PDH_HQUERY) { return ERROR_SUCCESS; }
PDH_STATUS WINAPI read_gpu_counter(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD,
                                   PPDH_FMT_COUNTERVALUE_ITEM_W) {
    return PDH_NO_DATA;
}
PDH_STATUS WINAPI close_gpu_query(PDH_HQUERY) {
    ++pdh_closes;
    return ERROR_SUCCESS;
}
kf2::Result<kf2::telemetry::NvidiaGpuSampler> create_nvidia_gpu(
    std::wstring_view) {
    using kf2::telemetry::NvidiaGpuSampler;
    if (++nvidia_creates <= nvidia_failures) {
        return kf2::Result<NvidiaGpuSampler>::failure(
            {kf2::ErrorCode::platform_failure, L"Test driver is not ready", 17});
    }
    return kf2::Result<NvidiaGpuSampler>::success(
        NvidiaGpuSampler::create_for_testing(62.0));
}

struct GpuProviderFixture final {
    GpuProviderFixture(std::uint32_t pdh_fail_count,
                       std::uint32_t nvidia_fail_count) {
        pdh_opens = pdh_closes = nvidia_creates = 0;
        pdh_failures = pdh_fail_count;
        nvidia_failures = nvidia_fail_count;
        kf2::telemetry::detail::PdhGpuApi api;
        api.open = open_gpu_query;
        api.add = add_gpu_counter;
        api.collect = collect_gpu_query;
        api.array = read_gpu_counter;
        api.close = close_gpu_query;
        kf2::telemetry::detail::set_pdh_gpu_api_for_testing(api);
        kf2::telemetry::detail::set_nvidia_gpu_create_hook_for_testing(
            create_nvidia_gpu);
    }
    ~GpuProviderFixture() {
        kf2::telemetry::detail::set_pdh_gpu_api_for_testing({});
        kf2::telemetry::detail::set_nvidia_gpu_create_hook_for_testing(nullptr);
    }
};

bool sample_gpu(ResourceTelemetryWorker& worker, std::uint64_t now_ns) {
    // Keep the production alternating schedule; wait before each request so
    // request coalescing cannot skip either group in a deterministic fixture.
    worker.request(now_ns);
    if (!worker.wait_until_idle(2s)) return false;
    worker.request(now_ns);
    if (!worker.wait_until_idle(2s)) return false;
    const auto snapshot = worker.latest();
    return snapshot && snapshot->gpu_sampled_at_ns == now_ns;
}

int test_gpu_provider_recovery() {
    GpuProviderFixture fixture{1, 0};
    ResourceTelemetryWorker worker;
    const auto generation = worker.bind(binding(41, 4100, 7));
    CHECK(sample_gpu(worker, 100));
    CHECK(pdh_opens == 1 && nvidia_creates == 1);
    CHECK(worker.latest()->driver_gpu_percent == 62.0);
    const auto failed = worker.latest()->gpu_provider_status;
    CHECK(failed && failed->pdh_attempts == 1 && failed->pdh_error);
    CHECK(failed->pdh_error->native_code == PDH_INVALID_HANDLE);
    CHECK(failed->nvidia_attempts == 1 && !failed->nvidia_error);
    CHECK(worker.bind(binding(41, 4100, 7)) == generation);
    CHECK(sample_gpu(worker, 999'999'999ULL));
    CHECK(pdh_opens == 1 && nvidia_creates == 1);
    CHECK(worker.latest()->gpu_provider_status == failed);
    CHECK(sample_gpu(worker, 1'000'000'100ULL));
    CHECK(pdh_opens == 2);
    CHECK(nvidia_creates == 1);
    CHECK(worker.latest()->gpu.has_value());
    const auto recovered = worker.latest()->gpu_provider_status;
    CHECK(recovered && recovered != failed);
    CHECK(recovered->pdh_attempts == 2 && !recovered->pdh_error);
    CHECK(failed->pdh_error);  // Published reports remain immutable.
    CHECK(sample_gpu(worker, 60'000'000'100ULL));
    CHECK(pdh_opens == 2 && nvidia_creates == 1);
    CHECK(worker.latest()->gpu_provider_status == recovered);
    worker.stop();
    CHECK(pdh_closes == 1);
    return EXIT_SUCCESS;
}

int test_nvidia_provider_recovery() {
    GpuProviderFixture fixture{0, 1};
    ResourceTelemetryWorker worker;
    const auto first_generation = worker.bind(binding(41, 4100, 7));
    worker.request(100);
    CHECK(worker.wait_until_idle(2s));
    CHECK(pdh_opens == 0 && nvidia_creates == 0);
    CHECK(sample_gpu(worker, 101));
    CHECK(pdh_opens == 1 && nvidia_creates == 1);
    CHECK(!worker.latest()->driver_gpu_percent);
    auto status = worker.latest()->gpu_provider_status;
    CHECK(status && !status->pdh_error && status->nvidia_error);
    CHECK(status->nvidia_error->native_code == 17);
    CHECK(sample_gpu(worker, 1'000'000'101ULL));
    CHECK(pdh_opens == 1 && nvidia_creates == 2);
    CHECK(worker.latest()->driver_gpu_percent == 62.0);
    status = worker.latest()->gpu_provider_status;
    CHECK(status && status->nvidia_attempts == 2 && !status->nvidia_error);
    CHECK(sample_gpu(worker, 40'000'000'000ULL));
    CHECK(pdh_opens == 1 && nvidia_creates == 2 && pdh_closes == 0);
    CHECK(worker.latest()->gpu_provider_status == status);

    // A changed adapter restarts only the binding's native GPU providers.
    CHECK(worker.bind(binding(41, 4100, 8)) != first_generation);
    CHECK(!worker.latest());
    CHECK(sample_gpu(worker, 40'000'000'001ULL));
    CHECK(pdh_opens == 2 && nvidia_creates == 3 && pdh_closes == 1);
    CHECK(worker.latest()->gpu_provider_status->pdh_attempts == 1);
    CHECK(worker.latest()->gpu_provider_status->nvidia_attempts == 1);
    CHECK(worker.bind(binding(42, 4200, 8)) != first_generation);
    CHECK(sample_gpu(worker, 40'000'000'002ULL));
    CHECK(pdh_opens == 3 && nvidia_creates == 4 && pdh_closes == 2);
    worker.stop();
    CHECK(pdh_closes == 3);
    return EXIT_SUCCESS;
}

int test_gpu_provider_backoff() {
    GpuProviderFixture fixture{UINT32_MAX, UINT32_MAX};
    ResourceTelemetryWorker worker;
    static_cast<void>(worker.bind(binding(41, 4100, 7)));
    CHECK(sample_gpu(worker, 0));
    CHECK(pdh_opens == 1 && nvidia_creates == 1);
    auto status = worker.latest()->gpu_provider_status;
    for (std::uint64_t now = 120'000'000; now < 1'000'000'000;
         now += 120'000'000) {
        CHECK(sample_gpu(worker, now));
        CHECK(worker.latest()->gpu_provider_status == status);
    }
    constexpr std::uint64_t attempts[] = {
        1, 3, 7, 15, 31, 61, 91};  // seconds; capped at 30 s per provider
    for (const auto seconds : attempts) {
        const auto before = pdh_opens;
        CHECK(sample_gpu(worker, seconds * 1'000'000'000ULL - 1));
        CHECK(pdh_opens == before && nvidia_creates == before);
        CHECK(worker.latest()->gpu_provider_status == status);
        CHECK(sample_gpu(worker, seconds * 1'000'000'000ULL));
        CHECK(pdh_opens == before + 1 && nvidia_creates == before + 1);
        status = worker.latest()->gpu_provider_status;
        CHECK(status && status->pdh_error && status->nvidia_error);
        CHECK(status->pdh_attempts == pdh_opens);
        CHECK(status->nvidia_attempts == nvidia_creates);
        CHECK(!worker.latest()->gpu && !worker.latest()->driver_gpu_percent);
    }
    CHECK(pdh_closes == 0);

    // A rolled-back clock rebases the wait rather than retrying every tick.
    CHECK(sample_gpu(worker, 2'000'000'000ULL));
    CHECK(sample_gpu(worker, 31'999'999'999ULL));
    CHECK(pdh_opens == 8 && nvidia_creates == 8);
    CHECK(sample_gpu(worker, 32'000'000'000ULL));
    CHECK(pdh_opens == 9 && nvidia_creates == 9);
    return EXIT_SUCCESS;
}

int test_gpu_provider_binding_boundaries() {
    GpuProviderFixture fixture{UINT32_MAX, UINT32_MAX};
    ResourceTelemetryWorker worker;
    auto current_binding = binding(41, 4100, 7);
    current_binding.adapter_vendor_id = 0x1002;  // AMD: no NVIDIA retry
    static_cast<void>(worker.bind(current_binding));
    CHECK(sample_gpu(worker, 1));
    CHECK(pdh_opens == 1 && nvidia_creates == 0);
    CHECK(sample_gpu(worker, 1'000'000'001ULL));
    CHECK(pdh_opens == 2 && nvidia_creates == 0);
    CHECK(worker.latest()->gpu_provider_status->nvidia_attempts == 0);
    current_binding.adapter_vendor_id = 0x8086;
    static_cast<void>(worker.bind(current_binding));
    CHECK(sample_gpu(worker, 1'000'000'002ULL));
    CHECK(pdh_opens == 3 && nvidia_creates == 0);

    current_binding.adapter_luid.reset();
    current_binding.adapter_name.clear();
    static_cast<void>(worker.bind(current_binding));
    CHECK(sample_gpu(worker, 2'000'000'000ULL));
    CHECK(pdh_opens == 3 && nvidia_creates == 0);
    CHECK(!worker.latest()->gpu_provider_status);

    current_binding = binding(42, 4200, 8);
    static_cast<void>(worker.bind(current_binding));
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    CHECK(sample_gpu(worker, maximum - 1'000'000'000ULL));
    CHECK(pdh_opens == 4 && nvidia_creates == 1);
    CHECK(sample_gpu(worker, maximum - 1));
    CHECK(pdh_opens == 4 && nvidia_creates == 1);
    CHECK(sample_gpu(worker, maximum));
    CHECK(pdh_opens == 5 && nvidia_creates == 2);
    CHECK(sample_gpu(worker, maximum));
    CHECK(pdh_opens == 5 && nvidia_creates == 2);
    worker.clear();
    CHECK(!worker.latest());
    return EXIT_SUCCESS;
}

std::string offline_telemetry_line() {
    return
        "ScriptLog: KF2OPT_TELEMETRY schema=7 sample=1 scan_diagnostics=1"
        " living=1 living_classes=1 living_bosses=0 living_visible=1"
        " living_offscreen=0 living_lod_total=0 living_anim_rate_total=60"
        " living_injured_zones=0 living_required_bones=80"
        " living_material_slots=4 living_attachments=1 living_anim_skipped=0"
        " living_bone_atoms_skipped=0 living_bone_interpolation=0"
        " living_kinematic_distance_skipped=0 living_ticks_offscreen=1"
        " living_updates_skeleton_offscreen=1 living_special_moves=0"
        " living_attack_moves=0 living_grapple_moves=0 living_stumbles=0"
        " living_knockdowns=0 living_hit_reactions=0"
        " living_other_special_moves=0 corpse_total=0 corpse_awake=0"
        " corpse_sleeping=0 corpse_other=0 corpse_final=0 corpse_visible=0"
        " corpse_offscreen=0 corpse_lod_total=0 corpse_injured_zones=0"
        " corpse_max_age_ms=0 corpse_limit=12 corpse_offscreen_time_ms=60000"
        " corpse_offscreen_distance=5000 dismembered=0 dismembered_limbs=0"
        " ragdoll_warned=0 ragdoll_warning_max=0 corpse_collide_dead=1"
        " corpse_collide_living=1 corpse_collide_dead_after_sleep=0"
        " corpse_collide_living_after_sleep=1 gibs=0 zed_time=0"
        " spray_actors=0 fire_spray_actors=0 toxic_spray_actors=0"
        " other_spray_actors=0 explosion_actors=0"
        " damaging_explosion_actors=0 fire_explosion_actors=0"
        " toxic_explosion_actors=0 other_damaging_explosion_actors=0"
        " unclassified_explosion_actors=0 lingering_explosion_actors=0"
        " smoke_explosion_actors=0 bloat_king_fart_explosion_actors=0"
        " smoke_grenade_projectiles=0 puke_mine_projectiles=0"
        " bloat_king_puke_mine_projectiles=0 wound_decals=0"
        " splatter_decals=0 pool_decals=0 impact_decals=0 explosion_decals=0"
        " wound_decal_limit=64 splatter_decal_limit=64 pool_decal_limit=20"
        " impact_decal_limit=40 explosion_decal_limit=20"
        " blood_effect_limit=25 gore_effect_limit=25"
        " wound_lifetime_ms=10000 splatter_lifetime_ms=15000"
        " pool_lifetime_ms=30000 gib_lifetime_ms=20000"
        " gore_particle_components=0 gore_particles=0"
        " gore_particle_visible_components=0 gore_particle_lod_total=0"
        " gore_particle_bounded_components=0 world_particle_components=0"
        " world_particles=0 world_particle_visible_components=0"
        " world_particle_lod_total=0 world_particle_bounded_components=0"
        " ground_fire_particle_components=0 ground_fire_particles=0"
        " impact_particle_components=0 impact_particles=0"
        " gore_particle_pool_capacity=30 world_particle_pool_capacity=200"
        " ground_fire_particle_pool_capacity=100"
        " impact_particle_pool_capacity=60 particle_constant_spawn_emitters=0"
        " particle_dynamic_spawn_emitters=0"
        " particle_constant_spawn_rate_milli=0 particle_burst_entries=0"
        " particle_peak_capacity=0 particle_flex_components=0"
        " particle_flex_fluid_components=0"
        " particle_flex_nonfluid_components=0"
        " particle_flex_mixed_components=0 particle_nonflex_components=0"
        " particle_unclassified_components=0 flex_surrogate_active=0"
        " flex_surrogate_particles=0 flex_surrogate_visible=0"
        " flex_surrogate_lod=0\n";
}

std::string graphics_readback_line() {
    return
        "[12.3] ScriptLog: KF2OPT_GFX_MENU schema=2 state=applied "
        "resx=2560 resy=1440 display_full=0 display_borderless=1 "
        "vsync=0 variable_fps=0 film_grain=25 environment=-1 character=-1 "
        "fx=1 texture_resolution=1 texture_filtering=-1 shadows=1 "
        "reflections=0 aa=1 bloom=1 motion_blur=0 ao=0 dof=0 volumetric=0 "
        "lens_flares=0 light_shafts=0 flex=0\n";
}

}  // namespace

int test_retained_game_log_handle() {
    namespace fs = std::filesystem;
    using namespace kf2::telemetry::detail;
    const auto root = fs::path{KF2_TEST_ROOT} / L"retained-log";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto log = root / L"Launch.log";
    write_file(log, "Log: LoadMap: KF-BioticsLab?"
        "Game=KFGameContent.KFGameInfo_Survival\n");
    wchar_t module[MAX_PATH + 1]{};
    const auto length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    CHECK(length > 0 && length < MAX_PATH);
    const auto process = kf2::game::bind_game_process(
        GetCurrentProcessId(), fs::path{module});
    CHECK(process.has_value());
    ResourceTelemetryBinding log_binding;
    log_binding.identity = {process.value().pid, process.value().process_start_id};
    log_binding.game_log_directory = root;
    const auto opens = game_log_handle_opens_for_testing();
    const auto closes = game_log_handle_closes_for_testing();
    ResourceTelemetryWorker worker;
    static_cast<void>(worker.bind(log_binding));
    worker.request(1'000'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    auto chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    for (unsigned int index = 0; index < 50; ++index) {
        worker.request(1'000'000'001ULL + index);
        CHECK(worker.wait_until_idle(2s));
        CHECK(worker.take_game_log_chunks(log_binding.identity).empty());
    }
    CHECK(game_log_handle_opens_for_testing() == opens + 1);
    CHECK(game_log_handle_closes_for_testing() == closes);

    // Ordinary writes, map travel and adapter changes retain the same file.
    HANDLE live_writer = CreateFileW(log.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(live_writer != INVALID_HANDLE_VALUE);
    LARGE_INTEGER end{};
    CHECK(SetFilePointerEx(live_writer, end, nullptr, FILE_END));
    const std::string map_line = "Log: LoadMap: KF-Paris?"
        "Game=KFGameContent.KFGameInfo_Survival\n";
    DWORD written = 0;
    CHECK(WriteFile(live_writer, map_line.data(),
        static_cast<DWORD>(map_line.size()), &written, nullptr));
    CHECK(written == map_line.size());
    log_binding.adapter_name = L"Updated adapter";
    static_cast<void>(worker.bind(log_binding));
    static_cast<void>(worker.invalidate_samples());
    worker.request(1'100'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && !chunks.front().reset_parser);
    CHECK(chunks.front().parsed_session &&
        chunks.front().parsed_session->map == "KF-Paris");
    CHECK(game_log_handle_opens_for_testing() == opens + 1);
    CHECK(CloseHandle(live_writer));

    // Losing process ownership of an already-bound file is also fail-closed.
    HANDLE timestamp_file = CreateFileW(log.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(timestamp_file != INVALID_HANDLE_VALUE);
    const FILETIME stale_time{1, 0};
    CHECK(SetFileTime(timestamp_file, nullptr, nullptr, &stale_time));
    CHECK(CloseHandle(timestamp_file));
    worker.request(1'150'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(!chunks.front().parsed_session);
    CHECK(game_log_handle_closes_for_testing() == closes + 1);
    write_file(log, "Log: LoadMap: KF-Paris?"
        "Game=KFGameContent.KFGameInfo_Survival\n");
    worker.request(1'160'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(log_binding.identity).size() == 1);

    // Rename must not keep tailing the old, still-readable handle forever.
    CHECK(MoveFileExW(log.c_str(), (root / L"retired.txt").c_str(), 0));
    write_file(log, "Log: LoadMap: KF-BurningParis?"
        "Game=KFGameContent.KFGameInfo_Survival\n");
    worker.request(1'200'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(!chunks.front().parsed_session && chunks.front().catching_up);
    CHECK(game_log_handle_closes_for_testing() == closes + 2);
    worker.request(1'300'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(chunks.front().parsed_session &&
        chunks.front().parsed_session->map == "KF-BurningParis");
    CHECK(game_log_handle_opens_for_testing() == opens + 3);

    // The retained handle must not conceal deletion or keep old context live.
    CHECK(DeleteFileW(log.c_str()));
    worker.request(1'400'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(!chunks.front().parsed_session);
    CHECK(game_log_handle_closes_for_testing() == closes + 3);
    worker.request(1'500'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(log_binding.identity).empty());
    CHECK(game_log_handle_opens_for_testing() == opens + 3);

    write_file(log, "Log: LoadMap: KF-Paris?"
        "Game=KFGameContent.KFGameInfo_Survival\n");
    HANDLE writer = CreateFileW(log.c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(writer != INVALID_HANDLE_VALUE);
    worker.request(1'600'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(log_binding.identity).empty());
    CHECK(game_log_handle_opens_for_testing() == opens + 3);
    CHECK(CloseHandle(writer));
    worker.request(1'700'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(game_log_handle_opens_for_testing() == opens + 4);

    const auto alias = root / L"alias.txt";
    CHECK(CreateHardLinkW(alias.c_str(), log.c_str(), nullptr));
    worker.request(1'800'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(!chunks.front().parsed_session);
    CHECK(game_log_handle_closes_for_testing() == closes + 4);
    CHECK(DeleteFileW(alias.c_str()));
    worker.request(1'900'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(log_binding.identity).size() == 1);

    // Rebinding closes on the worker even without another sample request.
    auto other_process = log_binding;
    other_process.identity.process_start_id += 36'000'000'000ULL;
    static_cast<void>(worker.bind(other_process));
    CHECK(worker.wait_until_idle(2s));
    CHECK(game_log_handle_opens_for_testing() == opens + 5);
    CHECK(game_log_handle_closes_for_testing() == closes + 5);
    worker.request(2'000'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(other_process.identity).empty());
    CHECK(game_log_handle_opens_for_testing() == opens + 5);
    static_cast<void>(worker.bind(log_binding));
    worker.request(2'100'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    CHECK(worker.take_game_log_chunks(log_binding.identity).size() == 1);
    worker.clear();
    CHECK(worker.wait_until_idle(2s));
    CHECK(game_log_handle_closes_for_testing() == closes + 6);

    for (unsigned int index = 0; index < 10; ++index) {
        static_cast<void>(worker.bind(log_binding));
        worker.request(2'200'000'000ULL + index);
        CHECK(worker.wait_until_idle(2s));
        CHECK(worker.take_game_log_chunks(log_binding.identity).size() == 1);
        worker.clear();
        CHECK(worker.wait_until_idle(2s));
    }
    static_cast<void>(worker.bind(log_binding));
    worker.request(2'300'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    static_cast<void>(worker.take_game_log_chunks(log_binding.identity));
    {
        std::ofstream output(log, std::ios::binary | std::ios::app);
        output << "Log: LoadMap: KF-BurningParis?"
            "Game=KFGameContent.KFGameInfo_Survival\n";
    }
    rebinding_log_worker = &worker;
    rebinding_log_binding = log_binding;
    set_game_log_read_hook_for_testing(&rebind_game_log_during_read);
    worker.request(2'400'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    set_game_log_read_hook_for_testing(nullptr);
    rebinding_log_worker = nullptr;
    CHECK(worker.take_game_log_chunks(log_binding.identity).empty());
    CHECK(game_log_handle_opens_for_testing() == opens + 17);
    CHECK(game_log_handle_closes_for_testing() == closes + 17);
    worker.request(2'500'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(chunks.front().parsed_session &&
        chunks.front().parsed_session->map == "KF-BurningParis");
    {
        std::ofstream output(log, std::ios::binary | std::ios::app);
        output << "Log: LoadMap: KF-Paris?"
            "Game=KFGameContent.KFGameInfo_Survival\n";
    }
    set_game_log_read_hook_for_testing(&truncate_game_log_during_read);
    worker.request(2'600'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    set_game_log_read_hook_for_testing(nullptr);
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(chunks.front().catching_up && !chunks.front().parsed_session);
    CHECK(game_log_handle_closes_for_testing() == closes + 18);
    write_file(log, "Log: LoadMap: KF-Paris?"
        "Game=KFGameContent.KFGameInfo_Survival\n");
    worker.request(2'700'000'000ULL);
    CHECK(worker.wait_until_idle(2s));
    chunks = worker.take_game_log_chunks(log_binding.identity);
    CHECK(chunks.size() == 1 && chunks.front().reset_parser);
    CHECK(chunks.front().parsed_session &&
        chunks.front().parsed_session->map == "KF-Paris");
    worker.stop();
    worker.clear();
    CHECK(worker.wait_until_idle(2s));
    CHECK(game_log_handle_opens_for_testing() == opens + 19);
    CHECK(game_log_handle_closes_for_testing() == closes + 19);
    fs::remove_all(root);
    return EXIT_SUCCESS;
}

int main() {
    if (test_retained_game_log_handle() != EXIT_SUCCESS) return EXIT_FAILURE;
    CHECK(test_gpu_provider_recovery() == EXIT_SUCCESS);
    CHECK(test_nvidia_provider_recovery() == EXIT_SUCCESS);
    CHECK(test_gpu_provider_backoff() == EXIT_SUCCESS);
    CHECK(test_gpu_provider_binding_boundaries() == EXIT_SUCCESS);
    std::chrono::microseconds request_batch_elapsed{};

    // Repeatedly exercise the complete lifetime boundary. The worker thread
    // must start only after its shared state exists and must join before that
    // state is destroyed.
    for (std::uint32_t iteration = 0; iteration < 32; ++iteration) {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                ++calls;
                ResourceSampleBatch batch;
                batch.group = request.group;
                return batch;
            }};
        CHECK(calls == 0);
        const auto generation = worker.bind(
            binding(100 + iteration, 10'000 + iteration));
        worker.request(100'000 + iteration);
        CHECK(wait_for_generation(worker, generation));
        CHECK(calls == 1);
        worker.clear();
        CHECK(!worker.latest());
    }

    {
        std::atomic<int> calls{0};
        std::atomic<int> priority{THREAD_PRIORITY_ERROR_RETURN};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                priority = GetThreadPriority(GetCurrentThread());
                ResourceSampleBatch batch;
                batch.group = request.group;
                if (request.group == ResourceSampleGroup::process_and_memory) {
                    kf2::telemetry::ProcessMetrics process;
                    process.private_bytes = request.binding.identity.pid;
                    batch.process = process;
                }
                ++calls;
                return batch;
            }};
        const auto generation = worker.bind(binding(41, 4100, 7));
        worker.request(1'000);
        const auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 41);
        CHECK(snapshot->identity.process_start_id == 4100);
        CHECK(snapshot->adapter_luid == 7);
        CHECK(snapshot->process);
        CHECK(snapshot->process->private_bytes == 41);
        CHECK(snapshot->process_sampled_at_ns == 1'000);
        CHECK(calls == 1);
        CHECK(priority == THREAD_PRIORITY_NORMAL);
    }

    // One worker serializes collection and coalesces repeated UI requests to
    // at most one pending sample while a sample is already running.
    {
        std::mutex mutex;
        std::condition_variable started;
        std::condition_variable release;
        bool first_started = false;
        bool allow_first_to_finish = false;
        std::atomic<int> active{0};
        std::atomic<int> maximum_active{0};
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token stop) {
                const int now_active = ++active;
                maximum_active = (std::max)(maximum_active.load(), now_active);
                const int call = ++calls;
                if (call == 1) {
                    std::unique_lock lock{mutex};
                    first_started = true;
                    started.notify_all();
                    release.wait(lock, [&] {
                        return allow_first_to_finish || stop.stop_requested();
                    });
                }
                --active;
                ResourceSampleBatch batch;
                batch.group = request.group;
                return batch;
            }};
        static_cast<void>(worker.bind(binding(42, 4200)));
        worker.request(2'000);
        {
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return first_started; }));
        }
        const auto before = std::chrono::steady_clock::now();
        for (std::uint64_t timestamp = 2'001; timestamp < 2'100; ++timestamp) {
            worker.request(timestamp);
        }
        request_batch_elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - before);
        CHECK(request_batch_elapsed < 50ms);
        {
            std::scoped_lock lock{mutex};
            allow_first_to_finish = true;
        }
        release.notify_all();
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (calls.load() < 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        CHECK(calls == 2);
        CHECK(maximum_active == 1);
    }

    // A completed result from an old process generation must never be
    // published after a restart or PID reuse.
    {
        std::mutex mutex;
        std::condition_variable started;
        std::condition_variable release;
        bool old_started = false;
        bool release_old = false;
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token stop) {
                if (request.binding.identity.process_start_id == 4300) {
                    std::unique_lock lock{mutex};
                    old_started = true;
                    started.notify_all();
                    release.wait(lock, [&] {
                        return release_old || stop.stop_requested();
                    });
                }
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = request.binding.identity.process_start_id;
                batch.process = process;
                return batch;
            }};
        const auto old_generation = worker.bind(binding(43, 4300));
        worker.request(3'000);
        {
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return old_started; }));
        }
        const auto new_generation = worker.bind(binding(44, 4400));
        CHECK(new_generation != old_generation);
        worker.request(4'000);
        {
            std::scoped_lock lock{mutex};
            release_old = true;
        }
        release.notify_all();
        const auto snapshot = wait_for_generation(worker, new_generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 44);
        CHECK(snapshot->identity.process_start_id == 4400);
        CHECK(snapshot->process);
        CHECK(snapshot->process->private_bytes == 4400);
    }

    // A map/session transition invalidates immutable samples without replacing
    // the single worker or losing its process binding.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = ++calls;
                batch.process = process;
                return batch;
            }};
        const auto first_generation = worker.bind(binding(49, 4900));
        worker.request(4'900);
        CHECK(wait_for_generation(worker, first_generation));
        const auto map_generation = worker.invalidate_samples();
        CHECK(map_generation != 0);
        CHECK(map_generation != first_generation);
        CHECK(!worker.latest());
        worker.request(4'901);
        const auto snapshot = wait_for_generation(worker, map_generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 49);
        CHECK(snapshot->identity.process_start_id == 4900);
        CHECK(calls == 2);
    }

    // Clearing a session invalidates an in-flight result. A later binding can
    // recover normally without creating a second worker.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                ++calls;
                ResourceSampleBatch batch;
                batch.group = request.group;
                if (calls == 1) std::this_thread::sleep_for(20ms);
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = request.binding.identity.pid;
                batch.process = process;
                return batch;
            }};
        static_cast<void>(worker.bind(binding(45, 4500)));
        worker.request(5'000);
        worker.clear();
        std::this_thread::sleep_for(30ms);
        CHECK(!worker.latest());
        const auto generation = worker.bind(binding(46, 4600));
        worker.request(6'000);
        const auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot && snapshot->identity.pid == 46);
    }

    // A provider exception clears only that sample and the same worker
    // recovers on a later request.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                if (++calls == 1) throw std::runtime_error{"test failure"};
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::GpuMetrics gpu;
                gpu.adapter_gpu_percent = 54.0;
                batch.gpu = gpu;
                return batch;
            }};
        const auto generation = worker.bind(binding(48, 4800));
        worker.request(8'000);
        auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot);
        CHECK(snapshot->process_sampled_at_ns == 8'000);
        CHECK(!snapshot->process);
        worker.request(8'100);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            snapshot = worker.latest();
            if (snapshot && snapshot->gpu_sampled_at_ns == 8'100) break;
            std::this_thread::sleep_for(1ms);
        }
        CHECK(snapshot && snapshot->gpu);
        CHECK(snapshot->gpu->adapter_gpu_percent == 54.0);
    }

    // Publication failures must release the active state and preserve the
    // single worker for a later successful sample.
    {
        ResourceTelemetryWorker worker{
            [](const ResourceSampleRequest& request, std::stop_token) {
                ResourceSampleBatch batch;
                batch.group = request.group;
                return batch;
            }};
        const auto generation = worker.bind(binding(58, 5800));
        worker.request(8'400);
        CHECK(wait_for_generation(worker, generation));
        kf2::telemetry::detail::fail_next_resource_telemetry_publication();
        worker.request(8'500);
        CHECK(worker.wait_until_idle(2s));
        CHECK(!worker.latest());
        worker.request(8'600);
        CHECK(wait_for_generation(worker, generation));
    }

    // Shutdown cancellation is cooperative and joins the only worker thread.
    {
        std::mutex mutex;
        std::condition_variable started;
        bool callback_started = false;
        const auto before = std::chrono::steady_clock::now();
        {
            ResourceTelemetryWorker worker{
                [&](const ResourceSampleRequest& request, std::stop_token stop) {
                    {
                        std::scoped_lock lock{mutex};
                        callback_started = true;
                    }
                    started.notify_all();
                    while (!stop.stop_requested()) {
                        std::this_thread::sleep_for(1ms);
                    }
                    ResourceSampleBatch batch;
                    batch.group = request.group;
                    return batch;
                }};
            static_cast<void>(worker.bind(binding(47, 4700)));
            worker.request(7'000);
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return callback_started; }));
        }
        CHECK(std::chrono::steady_clock::now() - before < 2s);
    }

    // A path replacement after identity validation cannot inject bytes from a
    // different Launch.log into the authenticated parser session.
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::path{KF2_TEST_ROOT} / L"log-replacement";
        fs::remove_all(root);
        fs::create_directories(root);
        const auto log = root / L"Launch.log";
        const std::string initial =
            "[0053.20] Log: LoadMap: KF-BioticsLab?"
            "Game=KFGameContent.KFGameInfo_Survival\n"
            "[0053.21] ScriptLog: WI.NetMode:  NM_Standalone\n";
        write_file(log, initial);
        wchar_t module[MAX_PATH + 1]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        CHECK(length > 0 && length < MAX_PATH);
        const auto identity = kf2::game::bind_game_process(
            GetCurrentProcessId(), fs::path{module});
        CHECK(identity.has_value());
        ResourceTelemetryBinding log_binding;
        log_binding.identity = {
            identity.value().pid, identity.value().process_start_id};
        log_binding.game_log_directory = root;
        ResourceTelemetryWorker worker;
        static_cast<void>(worker.bind(log_binding));
        worker.request(900'000'000ULL);
        CHECK(worker.wait_until_idle(2s));
        auto chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        const std::string trusted =
            "[0060.11] ScriptLog: @@@@ ZED COUNT DEBUG: "
            "AIAliveCount = 24\n";
        const std::string foreign =
            "[0060.11] ScriptLog: @@@@ ZED COUNT DEBUG: "
            "AIAliveCount = 99\n";
        {
            std::ofstream output(log, std::ios::binary | std::ios::app);
            output << trusted;
        }
        replacement_game_log = root / L"Launch.replacement.log";
        write_file(replacement_game_log, initial + foreign);
        game_log_replaced.store(false, std::memory_order_release);
        kf2::telemetry::detail::set_game_log_read_hook_for_testing(
            &replace_game_log_during_read);
        worker.request(950'000'000ULL);
        CHECK(worker.wait_until_idle(2s));
        kf2::telemetry::detail::set_game_log_read_hook_for_testing(nullptr);
        CHECK(game_log_replaced.load(std::memory_order_acquire));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().parsed_session);
        CHECK(chunks.front().parsed_session->zeds_alive == 24);
        // The raced read used the original inspected file, but the next poll
        // must discard that handle and explicitly reset before reading anew.
        worker.request(960'000'000ULL);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1 && chunks.front().reset_parser);
        CHECK(!chunks.front().parsed_session);
        worker.request(970'000'000ULL);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1 && chunks.front().reset_parser);
        CHECK(chunks.front().parsed_session);
        CHECK(!chunks.front().parsed_session->zeds_alive);
        worker.stop();
        fs::remove_all(root);
    }

    // The production worker performs incremental Launch.log I/O away from the
    // caller and reports truncation as an explicit parser reset.
    {
        namespace fs = std::filesystem;
        const fs::path root{KF2_TEST_ROOT};
        fs::remove_all(root);
        fs::create_directories(root);
        const auto log = root / L"Launch.log";
        {
            std::ofstream output(log, std::ios::binary);
            output << "[0053.20] Log: LoadMap: KF-BioticsLab?"
                      "Game=KFGameContent.KFGameInfo_Survival\n";
        }
        wchar_t module[MAX_PATH + 1]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        CHECK(length > 0 && length < MAX_PATH);
        const auto identity = kf2::game::bind_game_process(
            GetCurrentProcessId(), fs::path{module});
        CHECK(identity.has_value());
        ResourceTelemetryBinding log_binding;
        log_binding.identity = {
            identity.value().pid, identity.value().process_start_id};
        log_binding.game_log_directory = root;
        {
            ResourceTelemetryWorker worker;
            static_cast<void>(worker.bind(log_binding));
            worker.request(1'000'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            auto chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().reset_parser);
            CHECK(chunks.front().boundaries.load_map_started);
            CHECK(chunks.front().bytes.empty());
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session->map == "KF-BioticsLab");
            CHECK(chunks.front().parser_stats.lines_processed == 1);
            const auto initial_snapshot = chunks.front().parsed_session;

            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "[0048.42] ScriptLog: WI.NetMode:  NM_Standalone\n"
                          "[0060.11] ScriptLog: @@@@ ZED COUNT DEBUG: "
                          "AIAliveCount = 24\n";
            }
            worker.request(2'000'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(!chunks.front().reset_parser);
            CHECK(chunks.front().bytes.empty());
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session.get() !=
                  initial_snapshot.get());
            CHECK(initial_snapshot->map == "KF-BioticsLab");
            CHECK(!initial_snapshot->zeds_alive);
            CHECK(chunks.front().parsed_session->zeds_alive == 24);
            CHECK(chunks.front().parser_stats.lines_processed == 3);

            // Structured boundary lines may be split across worker samples.
            // No partial line is exposed to the UI boundary.
            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "ScriptLog: KF2OPT_MAP_SELECTION schema=1 "
                          "state=menu map=KF-Burn";
            }
            worker.request(2'025'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().bytes.empty());
            CHECK(!chunks.front().boundaries.map_prewarm_selection);

            // Multiple boundary families in one chunk are reduced to a small,
            // immutable handoff. Startup-ready retains precedence over an exit
            // marker in the same chunk, matching the UI's existing behavior.
            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "ingParis\n"
                       << graphics_readback_line()
                       << "Log: Restarting by request\n"
                          "Log: WidgetInitialized - WidgetName:  StartMenu\n"
                          "] Exit: Exiting.\n";
            }
            worker.request(2'050'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().bytes.empty());
            const auto& boundaries = chunks.front().boundaries;
            CHECK(boundaries.graphics_readback.has_value());
            CHECK(boundaries.graphics_readback->resolution.width == 2560);
            CHECK(boundaries.graphics_readback->film_grain_percent == 25);
            CHECK(boundaries.map_prewarm_selection ==
                  std::optional<std::wstring>{L"KF-BurningParis"});
            CHECK(boundaries.new_settings_restart_requested);
            CHECK(boundaries.startup_ready);
            CHECK(boundaries.verified_engine_exit);

            // Invalid structured lines and an oversized record cannot create
            // a boundary event or leak an unbounded tail into the next chunk.
            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "ScriptLog: KF2OPT_GFX_MENU schema=1 state=applied\n"
                          "ScriptLog: KF2OPT_MAP_SELECTION schema=1 "
                          "state=menu map=../unsafe\n"
                       << std::string(4097, 'x')
                       << "WidgetInitialized - WidgetName:  StartMenu\n";
            }
            worker.request(2'075'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(!chunks.front().boundaries.graphics_readback);
            CHECK(!chunks.front().boundaries.map_prewarm_selection);
            CHECK(!chunks.front().boundaries.startup_ready);

            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "[0060.12] ScriptLog: "
                          "KFAISpawnManager.SetupNextWave() NextWave: 0 "
                          "WaveTotalAI: 93\n"
                       << offline_telemetry_line();
            }
            worker.request(2'100'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session->wave_number == 1);
            CHECK(chunks.front().parsed_session->wave_total_ai == 93);
            CHECK(chunks.front().parsed_session->telemetry_sample == 1);

            // Repeating the same valid values after the old publication would
            // have crossed its freshness window must publish the refreshed
            // observation times to the application boundary.
            constexpr std::uint64_t repeated_at_ns = 17'100'000'001ULL;
            // Establish a current EOF before these newly written receipts.
            // A first read after a long pause must not freshen old history.
            worker.request(repeated_at_ns - 1);
            CHECK(worker.wait_until_idle(2s));
            static_cast<void>(worker.take_game_log_chunks(log_binding.identity));
            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "[0075.11] ScriptLog: @@@@ ZED COUNT DEBUG: "
                          "AIAliveCount = 24\n"
                          "[0075.12] ScriptLog: "
                          "KFAISpawnManager.SetupNextWave() NextWave: 0 "
                          "WaveTotalAI: 93\n"
                       << offline_telemetry_line();
            }
            worker.request(repeated_at_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().parsed_session);
            const auto& refreshed = *chunks.front().parsed_session;
            CHECK(refreshed.zeds_alive == 24);
            CHECK(refreshed.zeds_alive_observed_ns == repeated_at_ns);
            CHECK(refreshed.wave_number == 1);
            CHECK(refreshed.wave_total_ai == 93);
            CHECK(refreshed.wave_observed_ns == repeated_at_ns);
            CHECK(refreshed.telemetry_sample == 1);
            CHECK(refreshed.telemetry_observed_ns == repeated_at_ns);
            CHECK(kf2::game::game_log_observation_is_fresh(
                refreshed.zeds_alive, refreshed.zeds_alive_observed_ns,
                repeated_at_ns + 1));
            CHECK(kf2::game::game_log_observation_is_fresh(
                refreshed.wave_number, refreshed.wave_observed_ns,
                repeated_at_ns + 1));

            worker.request(32'100'000'002ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().observations_expired);
            CHECK(chunks.front().bytes.empty());
            CHECK(chunks.front().parsed_session);
            CHECK(!chunks.front().parsed_session->zeds_alive.has_value());
            CHECK(!chunks.front().parsed_session->wave_number.has_value());
            CHECK(!chunks.front().parsed_session->telemetry_sample.has_value());

            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "[0061.00] ScriptLog: KF2OPT_SESSION_CONTEXT schema=1 "
                          "state=online_client_read_only net_mode=NM_Client "
                          "map=KF-BioticsLab\n"
                          "[0061.01] ScriptLog: KF2OPT_ONLINE_CORPSE "
                          "state=available pool=0 maximum=20 local_only=true "
                          "readback=verified\n";
            }
            worker.request(32'200'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session->online_corpse_pool == 0);
            CHECK(chunks.front().parsed_session->online_corpse_maximum == 20);

            // An identical one-shot receipt refreshes its timestamp without
            // changing the parser model. The worker must still publish the
            // current authenticated snapshot to the UI boundary.
            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "[0061.02] ScriptLog: KF2OPT_ONLINE_CORPSE "
                          "state=available pool=0 maximum=20 local_only=true "
                          "readback=verified\n";
            }
            worker.request(32'300'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session->online_corpse_pool == 0);
            CHECK(chunks.front().parsed_session->online_corpse_maximum == 20);

            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "ScriptLog: KF2OPT_MAP_SELECTION schema=1 "
                          "state=menu map=KF-Partial";
            }
            worker.request(32'400'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(!chunks.front().boundaries.map_prewarm_selection);

            {
                std::ofstream output(log, std::ios::binary | std::ios::trunc);
                output << "new\n";
            }
            worker.request(33'000'000'000ULL);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().reset_parser);
            CHECK(chunks.front().bytes.empty());
            CHECK(!chunks.front().boundaries.map_prewarm_selection);
            CHECK(!chunks.front().parsed_session);
            CHECK(chunks.front().parser_stats.lines_processed == 1);
        }
        fs::remove_all(root);
    }

    // A large existing log must not expose its historical first map as live
    // gameplay while the bounded reader is still catching up to the tail.
    {
        namespace fs = std::filesystem;
        const auto root = fs::path{KF2_TEST_ROOT} / L"log-backlog";
        fs::remove_all(root);
        fs::create_directories(root);
        const auto log = root / L"Launch.log";
        const std::string old_map =
            "Log: LoadMap: KF-BioticsLab?"
            "Game=KFGameContent.KFGameInfo_Survival\n"
            "ScriptLog: WI.NetMode:  NM_Standalone\n";
        std::string history = old_map + offline_telemetry_line() +
            "Log: WidgetInitialized - WidgetName:  StartMenu\n" +
            "ScriptLog: KF2OPT_MAP_SELECTION schema=1 "
            "state=menu map=KF-BioticsLab\n";
        while (history.size() < 2 * 1024 * 1024) {
            history += "Log: historical diagnostic record\n";
        }
        history += "Log: LoadMap: KF-BurningParis?"
                   "Game=KFGameContent.KFGameInfo_Survival\n"
                   "ScriptLog: WI.NetMode:  NM_Client\n"
                   "ScriptLog: KF2OPT_SESSION_CONTEXT schema=1 "
                   "state=online_client_read_only net_mode=NM_Client "
                   "map=KF-BurningParis\n"
                   "ScriptLog: KF2OPT_ONLINE_CORPSE state=available "
                   "pool=0 maximum=20 local_only=true readback=verified\n" +
                   graphics_readback_line();
        write_file(log, history);
        wchar_t module[MAX_PATH + 1]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        CHECK(length > 0 && length < MAX_PATH);
        const auto process = kf2::game::bind_game_process(
            GetCurrentProcessId(), fs::path{module});
        CHECK(process.has_value());
        ResourceTelemetryBinding log_binding;
        log_binding.identity = {
            process.value().pid, process.value().process_start_id};
        log_binding.game_log_directory = root;
        ResourceTelemetryWorker worker;
        static_cast<void>(worker.bind(log_binding));
        auto now_ns = 50'000'000'000ULL;
        worker.request(now_ns);
        CHECK(worker.wait_until_idle(2s));
        auto chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(!chunks.front().parsed_session);
        CHECK(chunks.front().catching_up);
        CHECK(chunks.front().bytes.empty());
        CHECK(chunks.front().parser_stats.bytes_received == 512 * 1024);
        CHECK(chunks.front().parser_stats.backlog_bytes ==
              history.size() - 512 * 1024);
        CHECK(chunks.front().parser_stats.catch_up_age_ns == 0);
        CHECK(!chunks.front().boundaries.startup_ready);
        unsigned int samples = 1;
        while (chunks.front().catching_up && samples < 6) {
            now_ns += 200'000'000ULL;
            worker.request(now_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().parser_stats.oversized_input_resets == 0);
            if (chunks.front().catching_up) {
                CHECK(!chunks.front().parsed_session);
                CHECK(chunks.front().parser_stats.catch_up_age_ns ==
                      now_ns - 50'000'000'000ULL);
            }
            ++samples;
        }
        CHECK(samples == 5);
        CHECK(!chunks.front().catching_up);
        CHECK(chunks.front().parser_stats.backlog_bytes == 0);
        CHECK(chunks.front().parsed_session);
        CHECK(chunks.front().parsed_session->map == "KF-BurningParis");
        CHECK(chunks.front().parsed_session->net_mode == "NM_Client");
        CHECK(!chunks.front().parsed_session->telemetry_sample);
        CHECK(chunks.front().parsed_session->telemetry_observed_ns == 0);
        CHECK(chunks.front().parsed_session->online_corpse_maximum == 20);
        CHECK(chunks.front().parsed_session->online_corpse_capability_observed_ns != 0);
        CHECK(chunks.front().boundaries.startup_ready);
        CHECK(chunks.front().boundaries.graphics_readback);
        CHECK(!chunks.front().boundaries.map_prewarm_selection);

        const auto append = [&](std::string_view bytes) {
            std::ofstream output(log, std::ios::binary | std::ios::app);
            output << bytes;
        };
        append("ScriptLog: WI.NetMode:  NM_Standalone\n" +
               offline_telemetry_line());
        now_ns += 200'000'000ULL;
        worker.request(now_ns);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().parsed_session);
        CHECK(chunks.front().parsed_session->telemetry_observed_ns == now_ns);
        CHECK(chunks.front().parsed_session->telemetry_sample == 1);

        // Sustained writes six times the old read budget still reach EOF in
        // each bounded sample and retain newly produced, current measurements.
        std::string burst;
        while (burst.size() < 192 * 1024) burst += "Log: live diagnostic record\n";
        for (int index = 0; index < 12; ++index) {
            append(burst + offline_telemetry_line());
            now_ns += 200'000'000ULL;
            worker.request(now_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(!chunks.front().catching_up);
            CHECK(!chunks.front().historical);
            CHECK(chunks.front().parser_stats.backlog_bytes == 0);
            CHECK(chunks.front().parsed_session);
            CHECK(chunks.front().parsed_session->telemetry_observed_ns == now_ns);
        }

        // Growth during ReadFile must use the new EOF, not the pre-read size.
        append("Log: triggering concurrent append\n");
        game_log_append_during_read = burst + burst + burst + offline_telemetry_line();
        kf2::telemetry::detail::set_game_log_read_hook_for_testing(
            &append_game_log_during_read);
        now_ns += 200'000'000ULL;
        worker.request(now_ns);
        CHECK(worker.wait_until_idle(2s));
        kf2::telemetry::detail::set_game_log_read_hook_for_testing(nullptr);
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().catching_up);
        CHECK(!chunks.front().parsed_session);
        CHECK(chunks.front().parser_stats.backlog_bytes ==
              game_log_append_during_read.size());
        for (int index = 0; index < 2; ++index) {
            now_ns += 200'000'000ULL;
            worker.request(now_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
        }
        CHECK(!chunks.front().catching_up);
        CHECK(chunks.front().parsed_session);
        CHECK(!chunks.front().parsed_session->telemetry_sample);

        // Unread records accumulated during a long pause are history even if
        // the entire tail fits in one sample; newly written data works again.
        append(offline_telemetry_line());
        now_ns += kf2::game::kGameLogObservationFreshnessNs + 1;
        worker.request(now_ns);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().historical);
        CHECK(!chunks.front().catching_up);
        CHECK(chunks.front().parsed_session);
        CHECK(!chunks.front().parsed_session->telemetry_sample);
        append(offline_telemetry_line());
        worker.request(++now_ns);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().parsed_session);
        CHECK(chunks.front().parsed_session->telemetry_observed_ns == now_ns);
        // An overloaded writer cannot force an unbounded read or make a
        // behind-tail snapshot current. Stop writing and bounded catch-up
        // must recover instead of starving permanently.
        const auto overloaded_burst = burst + burst + burst + burst +
            offline_telemetry_line();
        auto previous_received = chunks.front().parser_stats.bytes_received;
        for (int index = 0; index < 5; ++index) {
            append(overloaded_burst);
            now_ns += 200'000'000ULL;
            worker.request(now_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().catching_up);
            CHECK(!chunks.front().parsed_session);
            CHECK(chunks.front().parser_stats.bytes_received - previous_received ==
                  512 * 1024);
            previous_received = chunks.front().parser_stats.bytes_received;
        }
        for (int index = 0; index < 4 && chunks.front().catching_up; ++index) {
            now_ns += 200'000'000ULL;
            worker.request(now_ns);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
        }
        CHECK(!chunks.front().catching_up);
        CHECK(chunks.front().parsed_session);
        CHECK(!chunks.front().parsed_session->telemetry_sample);
        CHECK(chunks.front().parser_stats.oversized_input_resets == 0);

        append(overloaded_burst);
        worker.request(++now_ns);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1 && chunks.front().catching_up);
        // Truncation during catch-up resets both context and deferred events.
        write_file(log, old_map + offline_telemetry_line());
        worker.request(++now_ns);
        CHECK(worker.wait_until_idle(2s));
        chunks = worker.take_game_log_chunks(log_binding.identity);
        CHECK(chunks.size() == 1);
        CHECK(chunks.front().reset_parser);
        CHECK(!chunks.front().catching_up);
        CHECK(chunks.front().parser_stats.backlog_bytes == 0);
        CHECK(chunks.front().parsed_session);
        CHECK(chunks.front().parsed_session->map == "KF-BioticsLab");
        CHECK(!chunks.front().parsed_session->online_corpse_maximum);
        CHECK(!chunks.front().parsed_session->telemetry_sample);
        CHECK(!chunks.front().boundaries.startup_ready);
        worker.stop();
        fs::remove_all(root);
    }

    std::cout << "nonblocking_request_batch_us="
              << request_batch_elapsed.count() << '\n';
    return EXIT_SUCCESS;
}
