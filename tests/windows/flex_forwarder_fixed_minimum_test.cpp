#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>

#include "kf2/flex/flex_observation.hpp"
#include "kf2/flex/flex_observation_shared.hpp"

namespace {
using Update = void (*)(void*, float, int, void*);
using LastSubsteps = int (*)();
using UpdateCalls = long long (*)();
using GetActiveCount = int (*)(void*);
using Create = void* (*)(int);
using Destroy = void (*)(void*);
using Fence = void (*)();
using BufferTransfer = void (*)(void*, void*, int, int);
using GetBounds = void (*)(void*, float*, float*);
using SetParams = void (*)(void*, const void*);
using LockHook = void (*)();

template <typename Predicate>
bool wait_until(Predicate predicate) {
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (predicate()) return true;
        Sleep(1);
    }
    return false;
}

int fail(int code, const char* message) {
    std::cerr << message << " (" << code << ")\n";
    return code;
}

int test_late_original(const std::filesystem::path& forwarder,
                       const std::filesystem::path& original) {
    const auto relay = LoadLibraryExW(forwarder.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!relay) return fail(50, "early forwarder load failed");
    const auto version = reinterpret_cast<int (*)()>(
        GetProcAddress(relay, "flexGetVersion"));
    if (!version || version() != 0 || version() != 0)
        return fail(51, "missing original was not retried safely");
    const auto native = LoadLibraryExW(original.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!native) return fail(52, "late original load failed");
    std::atomic<bool> start{false};
    std::atomic<bool> valid{true};
    std::thread callers[4];
    for (auto& caller : callers) caller = std::thread([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned call = 0; call < 256; ++call)
            if (version() != 31) valid.store(false, std::memory_order_relaxed);
    });
    start.store(true, std::memory_order_release);
    for (auto& caller : callers) caller.join();
    if (!valid.load(std::memory_order_relaxed))
        return fail(53, "concurrent late export resolution failed");
    if (!FreeLibrary(native)) return fail(54, "host original release failed");
    // Check lifetime before calling a potentially cached address. The loader
    // dependency must outlive the host's own reference, without a permanent pin.
    if (!GetModuleHandleW(L"flexRelease_original.dll"))
        return fail(55, "relay did not retain the resolved original dependency");
    const auto lookups = reinterpret_cast<long long (*)()>(
        GetProcAddress(relay, "flexTestExportLookups"));
    if (!lookups) return fail(56, "lookup test seam missing");
    const auto before = lookups();
    for (unsigned call = 0; call < 256; ++call)
        if (version() != 31) return fail(57, "cached late version changed");
    if (lookups() != before) return fail(58, "warm version repeated export lookup");
    if (!FreeLibrary(relay) || GetModuleHandleW(L"flexRelease_original.dll"))
        return fail(59, "dependency outlived its forwarder");
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return fail(2, "expected sandbox, forwarder and test double");
    const auto sandbox = std::filesystem::absolute(std::filesystem::path{argv[1]});
    std::error_code error;
    std::filesystem::remove_all(sandbox, error);
    error.clear();
    std::filesystem::create_directories(sandbox, error);
    if (error) return fail(3, "sandbox creation failed");

    const auto original = sandbox / L"flexRelease_original.dll";
    const auto forwarder = sandbox / L"flexRelease_x64.dll";
    std::filesystem::copy_file(std::filesystem::absolute(argv[3]), original,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) return fail(4, "test double copy failed");
    std::filesystem::copy_file(std::filesystem::absolute(argv[2]), forwarder,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) return fail(5, "forwarder copy failed");

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    const auto cookie = AddDllDirectory(sandbox.c_str());
    if (const auto result = test_late_original(forwarder, original); result != 0)
        return result;
    HMODULE original_module = LoadLibraryExW(original.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE forwarder_module = LoadLibraryExW(forwarder.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (cookie) RemoveDllDirectory(cookie);
    if (!original_module || !forwarder_module) return fail(6, "DLL load failed");

    const auto update = reinterpret_cast<Update>(
        GetProcAddress(forwarder_module, "flexUpdateSolver"));
    const auto last = reinterpret_cast<LastSubsteps>(
        GetProcAddress(original_module, "flexTestLastSubsteps"));
    const auto calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestUpdateCalls"));
    const auto active_count = reinterpret_cast<GetActiveCount>(
        GetProcAddress(forwarder_module, "flexGetActiveCount"));
    const auto active_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestActiveCountCalls"));
    const auto create = reinterpret_cast<Create>(
        GetProcAddress(forwarder_module, "flexCreateSolver"));
    const auto destroy = reinterpret_cast<Destroy>(
        GetProcAddress(forwarder_module, "flexDestroySolver"));
    const auto create_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestCreateCalls"));
    const auto destroy_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestDestroyCalls"));
    const auto last_capacity = reinterpret_cast<LastSubsteps>(
        GetProcAddress(original_module, "flexTestLastCapacity"));
    const auto set_fence = reinterpret_cast<Fence>(
        GetProcAddress(forwarder_module, "flexSetFence"));
    const auto wait_fence = reinterpret_cast<Fence>(
        GetProcAddress(forwarder_module, "flexWaitFence"));
    const auto fence_set_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestFenceSetCalls"));
    const auto fence_wait_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestFenceWaitCalls"));
    const auto get_particles = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexGetParticles"));
    const auto get_phases = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexGetPhases"));
    const auto get_velocities = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexGetVelocities"));
    const auto set_particles = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexSetParticles"));
    const auto set_phases = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexSetPhases"));
    const auto set_velocities = reinterpret_cast<BufferTransfer>(
        GetProcAddress(forwarder_module, "flexSetVelocities"));
    const auto particle_upload_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestParticleUploadCalls"));
    const auto particle_download_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestParticleDownloadCalls"));
    const auto phase_upload_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestPhaseUploadCalls"));
    const auto phase_download_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestPhaseDownloadCalls"));
    const auto velocity_upload_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestVelocityUploadCalls"));
    const auto velocity_download_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestVelocityDownloadCalls"));
    const auto last_transfer_elements = reinterpret_cast<LastSubsteps>(
        GetProcAddress(original_module, "flexTestLastTransferElements"));
    const auto last_transfer_memory = reinterpret_cast<LastSubsteps>(
        GetProcAddress(original_module, "flexTestLastTransferMemory"));
    const auto get_bounds = reinterpret_cast<GetBounds>(
        GetProcAddress(forwarder_module, "flexGetBounds"));
    const auto set_params = reinterpret_cast<SetParams>(
        GetProcAddress(forwarder_module, "flexSetParams"));
    const auto bounds_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestBoundsCalls"));
    const auto params_calls = reinterpret_cast<UpdateCalls>(
        GetProcAddress(original_module, "flexTestParamsCalls"));
    const auto acquire_solver_lock = reinterpret_cast<LockHook>(
        GetProcAddress(forwarder_module, "flexTestAcquireSolverLock"));
    const auto release_solver_lock = reinterpret_cast<LockHook>(
        GetProcAddress(forwarder_module, "flexTestReleaseSolverLock"));
    if (!update || !last || !calls || !active_count || !active_calls ||
        !create || !destroy || !create_calls || !destroy_calls || !last_capacity ||
        !set_fence || !wait_fence || !fence_set_calls || !fence_wait_calls ||
        !get_particles || !get_phases || !get_velocities || !set_particles ||
        !set_phases || !set_velocities || !particle_upload_calls ||
        !particle_download_calls || !phase_upload_calls || !phase_download_calls ||
        !velocity_upload_calls || !velocity_download_calls ||
        !last_transfer_elements || !last_transfer_memory || !get_bounds ||
        !set_params || !bounds_calls || !params_calls ||
        !acquire_solver_lock || !release_solver_lock)
        return fail(7, "test exports missing");

    void* const solver = create(1024);
    void* const second_solver = create(256);
    if (!solver || !second_solver || solver == second_solver ||
        create_calls() != 2 || last_capacity() != 256)
        return fail(15, "solver creation was not relayed exactly");
    update(solver, 1.0F / 60.0F, 2, nullptr);
    const auto mapping_name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
        std::to_wstring(GetCurrentProcessId());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name.c_str());
    if (!mapping) return fail(8, "observation mapping missing");
    auto* shared = static_cast<kf2::flex::ObservationShared*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(kf2::flex::ObservationShared)));
    if (!shared || shared->magic != kf2::flex::observation_magic ||
        shared->version != kf2::flex::observation_version) {
        return fail(9, "observation mapping invalid");
    }
    if (shared->diagnostics_enabled != 0 ||
        shared->min_substeps != LONG_MAX ||
        shared->min_forwarded_substeps != LONG_MAX)
        return fail(23, "detailed diagnostics were not disabled by default");
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return fail(48, "process identity unavailable");
    const kf2::game::GameProcessIdentity identity{GetCurrentProcessId(),
        (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) |
            created.dwLowDateTime, {}};
    const auto minimal_readback = kf2::flex::read_observation(identity);
    if (!minimal_readback || !minimal_readback->fresh ||
        !minimal_readback->pass_through_healthy ||
        minimal_readback->last_forwarded_substeps != 1 ||
        !minimal_readback->particle_capacity_available ||
        minimal_readback->particle_capacity != 1280 ||
        minimal_readback->aggregate_particles_fresh ||
        shared->active_count_calls != 0 || active_calls() != 0)
        return fail(49, "minimal relay readback requires unrequested particle counts");
    set_fence();
    if (fence_set_calls() != 1 || shared->fence_set_calls != 0)
        return fail(24, "disabled diagnostics recorded a detailed fence sample");
    InterlockedExchange(&shared->diagnostics_enabled, 1);

    if (shared->create_calls != 2 || shared->live_solvers != 2 ||
        shared->max_live_solvers != 2 || shared->aggregate_particle_capacity != 1280 ||
        shared->aggregate_capacity_valid != 1 ||
        shared->missing_original_calls != 0 || shared->tracking_drop_calls != 0 ||
        shared->invalid_argument_calls != 0 ||
        shared->solver_tracking_quarantined != 0)
        return fail(16, "solver capacity was not observed");

    if (active_count(solver) != 37 || active_count(second_solver) != 37 ||
        active_calls() != 2 || shared->active_count_calls != 2 ||
        shared->last_active_particles != 37 ||
        shared->min_active_particles != 37 || shared->max_active_particles != 37 ||
        shared->active_particles_valid != 1 || shared->last_active_count_tick == 0 ||
        shared->aggregate_active_particles != 74 ||
        shared->aggregate_free_particles != 1206 ||
        shared->aggregate_counts_valid != 1 || shared->oldest_active_count_tick == 0) {
        return fail(14, "read-only active-particle relay was not observed");
    }

    // flexUpdateSolver must remain completely independent of the solver lock.
    // Holding the lock cannot delay an update or weaken the fixed clamp.
    acquire_solver_lock();
    std::atomic<bool> update_returned{false};
    std::thread contended_update([&] {
        update(solver, 1.0F / 60.0F, 2, nullptr);
        update_returned.store(true, std::memory_order_release);
    });
    const bool update_completed = wait_until([&] {
        return update_returned.load(std::memory_order_acquire);
    });
    release_solver_lock();
    contended_update.join();
    if (!update_completed || calls() != 2 || last() != 1 ||
        shared->tracking_drop_calls != 0 ||
        shared->solver_tracking_quarantined != 0)
        return fail(23, "solver lock delayed or weakened an update");

    // Lifecycle and active-count observations cannot be reconstructed after
    // the native call. They wait behind the tiny fixed-storage critical
    // section while the update path above remains non-blocking.
    acquire_solver_lock();
    std::atomic<bool> create_returned{false};
    void* third_solver = nullptr;
    std::thread contended_create([&] {
        third_solver = create(128);
        create_returned.store(true, std::memory_order_release);
    });
    const bool create_entered = wait_until([&] { return create_calls() == 3; });
    const bool create_blocked = !create_returned.load(std::memory_order_acquire);
    release_solver_lock();
    contended_create.join();
    if (!create_entered || !create_blocked || !third_solver ||
        shared->live_solvers != 3 || shared->aggregate_particle_capacity != 1408 ||
        shared->solver_tracking_quarantined != 0)
        return fail(25, "contended solver creation was not preserved");

    acquire_solver_lock();
    std::atomic<bool> active_returned{false};
    int third_active = -1;
    std::thread contended_active([&] {
        third_active = active_count(third_solver);
        active_returned.store(true, std::memory_order_release);
    });
    const bool active_entered = wait_until([&] { return active_calls() == 3; });
    const bool active_blocked = !active_returned.load(std::memory_order_acquire);
    release_solver_lock();
    contended_active.join();
    if (!active_entered || !active_blocked || third_active != 37 ||
        shared->aggregate_active_particles != 111 ||
        shared->aggregate_free_particles != 1297 ||
        shared->aggregate_counts_valid != 1)
        return fail(26, "contended active count was not preserved");

    acquire_solver_lock();
    std::atomic<bool> destroy_started{false};
    std::atomic<bool> destroy_returned{false};
    std::thread contended_destroy([&] {
        destroy_started.store(true, std::memory_order_release);
        destroy(third_solver);
        destroy_returned.store(true, std::memory_order_release);
    });
    const bool destroy_entered = wait_until([&] {
        return destroy_started.load(std::memory_order_acquire);
    });
    Sleep(10);
    const bool destroy_blocked = !destroy_returned.load(std::memory_order_acquire);
    release_solver_lock();
    contended_destroy.join();
    if (!destroy_entered || !destroy_blocked || destroy_calls() != 1 ||
        shared->live_solvers != 2 || shared->aggregate_particle_capacity != 1280 ||
        shared->aggregate_active_particles != 74 ||
        shared->aggregate_free_particles != 1206)
        return fail(27, "contended solver destruction was not preserved");

    set_fence();
    wait_fence();
    if (fence_set_calls() != 2 || fence_wait_calls() != 1 ||
        shared->fence_set_calls != 1 || shared->fence_wait_calls != 1 ||
        shared->last_fence_set_tick == 0 || shared->last_fence_wait_tick == 0)
        return fail(19, "fence synchronization was not relayed exactly");

    float particle_buffer[4]{};
    int phase_buffer[4]{};
    float velocity_buffer[4]{};
    set_particles(solver, particle_buffer, 4, 1);
    set_phases(solver, phase_buffer, 3, 2);
    set_velocities(solver, velocity_buffer, 2, 3);
    get_particles(solver, particle_buffer, 5, 0);
    get_phases(solver, phase_buffer, 6, 1);
    get_velocities(solver, velocity_buffer, 7, 2);
    if (particle_upload_calls() != 1 || phase_upload_calls() != 1 ||
        velocity_upload_calls() != 1 || particle_download_calls() != 1 ||
        phase_download_calls() != 1 || velocity_download_calls() != 1 ||
        last_transfer_elements() != 7 || last_transfer_memory() != 2 ||
        shared->particle_upload_calls != 1 || shared->phase_upload_calls != 1 ||
        shared->velocity_upload_calls != 1 || shared->particle_download_calls != 1 ||
        shared->phase_download_calls != 1 || shared->velocity_download_calls != 1 ||
        shared->upload_elements != 9 || shared->download_elements != 18 ||
        shared->last_upload_elements != 2 || shared->last_upload_memory != 3 ||
        shared->last_download_elements != 7 || shared->last_download_memory != 2)
        return fail(20, "buffer transfers were not relayed and observed exactly");

    const auto upload_before_invalid = shared->upload_elements;
    set_particles(solver, particle_buffer, -1, 0);
    if (particle_upload_calls() != 2 || shared->particle_upload_calls != 2 ||
        shared->upload_elements != upload_before_invalid ||
        shared->invalid_argument_calls != 1)
        return fail(21, "invalid transfer arguments were not diagnosed safely");

    float lower[3]{};
    float upper[3]{};
    int opaque_params = 42;
    get_bounds(solver, lower, upper);
    set_params(solver, &opaque_params);
    if (bounds_calls() != 1 || params_calls() != 1 ||
        shared->bounds_calls != 1 || shared->params_calls != 1 ||
        lower[0] != -1.0F || lower[2] != -3.0F ||
        upper[0] != 1.0F || upper[2] != 3.0F)
        return fail(22, "bounds or parameter calls were not relayed exactly");

    if (calls() != 2 || last() != 1 || shared->last_substeps != 2 ||
        shared->last_forwarded_substeps != 1 || shared->constrained_updates != 2)
        return fail(10, "the fixed minimum was not applied immediately");

    InterlockedExchange(&shared->desired_substeps, 5);
    InterlockedExchange64(&shared->control_heartbeat_tick,
        static_cast<LONGLONG>(GetTickCount64()));
    update(solver, 1.0F / 60.0F, 4, nullptr);
    if (calls() != 3 || last() != 1 || shared->last_substeps != 4 ||
        shared->last_forwarded_substeps != 1 || shared->constrained_updates != 3)
        return fail(11, "legacy adaptive control overrode the fixed minimum");

    InterlockedExchange64(&shared->control_heartbeat_tick,
        static_cast<LONGLONG>(GetTickCount64() - 1600));
    update(solver, 1.0F / 60.0F, 2, nullptr);
    if (calls() != 4 || last() != 1 || shared->last_forwarded_substeps != 1 ||
        shared->constrained_updates != 4)
        return fail(12, "stale control released the fixed minimum");

    InterlockedExchange(&shared->desired_substeps, 0);
    update(solver, 1.0F / 60.0F, 3, nullptr);
    update(solver, 1.0F / 60.0F, 1, nullptr);
    update(solver, 1.0F / 60.0F, 0, nullptr);
    if (calls() != 7 || last() != 0 || shared->last_forwarded_substeps != 0 ||
        shared->constrained_updates != 5 ||
        shared->successful_updates != shared->update_calls)
        return fail(13, "the fixed minimum did not preserve values at or below one");

    destroy(second_solver);
    if (destroy_calls() != 2 || shared->destroy_calls != 2 ||
        shared->live_solvers != 1 || shared->aggregate_particle_capacity != 1024 ||
        shared->aggregate_active_particles != 37 ||
        shared->aggregate_free_particles != 987)
        return fail(17, "solver destruction did not update the aggregate");

    const auto version = reinterpret_cast<int (*)()>(
        GetProcAddress(forwarder_module, "flexGetVersion"));
    const auto lookups = reinterpret_cast<long long (*)()>(
        GetProcAddress(forwarder_module, "flexTestExportLookups"));
    if (!version || version() != 31 || !lookups)
        return fail(60, "warm-cache test exports unavailable");
    const auto lookups_before = lookups();
    for (unsigned call = 0; call < 256; ++call) {
        if (version() != 31 || active_count(solver) != 37)
            return fail(61, "cached return value changed");
        get_bounds(solver, lower, upper);
        set_params(solver, &opaque_params);
        set_particles(solver, particle_buffer, 4, 1);
        set_phases(solver, phase_buffer, 3, 2);
        set_velocities(solver, velocity_buffer, 2, 3);
        get_particles(solver, particle_buffer, 5, 0);
        get_phases(solver, phase_buffer, 6, 1);
        get_velocities(solver, velocity_buffer, 7, 2);
        set_fence(); wait_fence();
        update(solver, 1.0F / 60.0F, 4, nullptr);
    }
    if (lookups() != lookups_before || last() != 1 ||
        particle_upload_calls() != 258 || phase_upload_calls() != 257 ||
        velocity_upload_calls() != 257 || particle_download_calls() != 257 ||
        phase_download_calls() != 257 || velocity_download_calls() != 257 ||
        fence_set_calls() != 258 || fence_wait_calls() != 257 ||
        bounds_calls() != 257 || params_calls() != 257 ||
        lower[0] != -1.0F || upper[2] != 3.0F)
        return fail(62, "warm relay repeated lookups or aliased an export");

    const auto gate = reinterpret_cast<void (*)(HANDLE, HANDLE)>(
        GetProcAddress(original_module, "flexTestSetUpdateGate"));
    if (!gate) return fail(64, "native completion gate unavailable");
    if (shared->own_work_ticks != 0)
        return fail(66, "disabled self-work measurement recorded native work");
    kf2::flex::ObservationReader work_reader;
    const auto work_start = work_reader.read(identity, true);
    if (!work_start || !work_start->own_work_ns)
        return fail(67, "self-work measurement could not start");
    const auto completed_publication = [&](int input, int before, int after) {
        HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!entered || !release) {
            if (entered) CloseHandle(entered);
            if (release) CloseHandle(release);
            return false;
        }
        gate(entered, release);
        const auto ticks_before = shared->own_work_ticks;
        std::thread native([&] { update(solver, 1.0F / 60.0F, input, nullptr); });
        const bool in_flight = WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0 &&
            shared->last_forwarded_substeps == before &&
            shared->update_calls == shared->successful_updates + 1;
        // The original call is blocked on purpose. Its native wait must not
        // appear in the Optimizer wrapper's elapsed-work counter.
        Sleep(250);
        const bool native_not_counted = shared->own_work_ticks == ticks_before;
        SetEvent(release);
        native.join();
        gate(nullptr, nullptr);
        CloseHandle(entered);
        CloseHandle(release);
        return in_flight && native_not_counted &&
            shared->own_work_ticks > ticks_before &&
            shared->own_work_ticks - ticks_before < shared->own_work_frequency / 8 &&
            shared->last_forwarded_substeps == after &&
            shared->update_calls == shared->successful_updates;
    };
    if (!completed_publication(0, 1, 0) || !completed_publication(4, 0, 1))
        return fail(65, "forwarded value was published before native completion");
    const auto work_end = work_reader.read(identity, true);
    if (!work_end || !work_end->own_work_ns ||
        *work_end->own_work_ns <= *work_start->own_work_ns)
        return fail(68, "completed wrapper work was not published");
    static_cast<void>(work_reader.read(identity));
    const auto ticks_off = shared->own_work_ticks;
    if (version() != 31 || shared->own_work_ticks != ticks_off)
        return fail(69, "disabled wrapper still measured work");
    InterlockedExchange64(&shared->own_work_lease_tick,
                          static_cast<LONGLONG>(GetTickCount64() - 1));
    if (version() != 31 || shared->own_work_ticks != ticks_off ||
        shared->own_work_lease_tick != 0)
        return fail(70, "expired self-work lease kept measuring");
    work_reader.reset();
    destroy(solver);
    if (destroy_calls() != 3 || shared->destroy_calls != 3 ||
        shared->live_solvers != 0 || shared->aggregate_capacity_valid != 0 ||
        shared->aggregate_counts_valid != 0)
        return fail(18, "final solver retirement was not observed");
    if (lookups() != lookups_before)
        return fail(63, "warm destroy repeated export lookup");

    if (!FreeLibrary(forwarder_module)) return fail(28, "forwarder unload failed");
    if (shared->magic != 0 || shared->state != 0)
        return fail(29, "retained reader kept an unloaded producer valid");
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
    if (mapping) {
        CloseHandle(mapping);
        return fail(30, "mapping survived producer and reader release");
    }
    FreeLibrary(original_module);
    return 0;
}
