#include <Windows.h>

#include <filesystem>
#include <iostream>

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

int fail(int code, const char* message) {
    std::cerr << message << " (" << code << ")\n";
    return code;
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
    if (!update || !last || !calls || !active_count || !active_calls ||
        !create || !destroy || !create_calls || !destroy_calls || !last_capacity ||
        !set_fence || !wait_fence || !fence_set_calls || !fence_wait_calls ||
        !get_particles || !get_phases || !get_velocities || !set_particles ||
        !set_phases || !set_velocities || !particle_upload_calls ||
        !particle_download_calls || !phase_upload_calls || !phase_download_calls ||
        !velocity_upload_calls || !velocity_download_calls ||
        !last_transfer_elements || !last_transfer_memory || !get_bounds ||
        !set_params || !bounds_calls || !params_calls)
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

    set_fence();
    wait_fence();
    if (fence_set_calls() != 1 || fence_wait_calls() != 1 ||
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

    InterlockedExchange(&shared->desired_substeps, 5);
    InterlockedExchange64(&shared->control_heartbeat_tick,
        static_cast<LONGLONG>(GetTickCount64()));
    for (int index = 1; index < 180; ++index) {
        update(solver, 1.0F / 60.0F, 2, nullptr);
    }
    if (calls() != 180 || last() != 2 || shared->constrained_updates != 0)
        return fail(10, "warmup changed original substeps");

    update(solver, 1.0F / 60.0F, 2, nullptr);
    if (calls() != 181 || last() != 5 || shared->last_substeps != 2 ||
        shared->last_forwarded_substeps != 5 || shared->constrained_updates != 1)
        return fail(11, "fresh control did not change forwarded substeps");

    InterlockedExchange64(&shared->control_heartbeat_tick,
        static_cast<LONGLONG>(GetTickCount64() - 1600));
    update(solver, 1.0F / 60.0F, 2, nullptr);
    if (calls() != 182 || last() != 2 || shared->last_forwarded_substeps != 2 ||
        shared->constrained_updates != 1)
        return fail(12, "stale control did not restore original substeps");

    InterlockedExchange(&shared->desired_substeps, 0);
    InterlockedExchange64(&shared->control_heartbeat_tick,
        static_cast<LONGLONG>(GetTickCount64()));
    update(solver, 1.0F / 60.0F, 3, nullptr);
    if (calls() != 183 || last() != 3 || shared->last_forwarded_substeps != 3 ||
        shared->successful_updates != shared->update_calls)
        return fail(13, "Off mode did not preserve original substeps");

    destroy(second_solver);
    if (destroy_calls() != 1 || shared->destroy_calls != 1 ||
        shared->live_solvers != 1 || shared->aggregate_particle_capacity != 1024 ||
        shared->aggregate_active_particles != 37 ||
        shared->aggregate_free_particles != 987)
        return fail(17, "solver destruction did not update the aggregate");
    destroy(solver);
    if (destroy_calls() != 2 || shared->destroy_calls != 2 ||
        shared->live_solvers != 0 || shared->aggregate_capacity_valid != 0 ||
        shared->aggregate_counts_valid != 0)
        return fail(18, "final solver retirement was not observed");

    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    FreeLibrary(forwarder_module);
    FreeLibrary(original_module);
    return 0;
}
