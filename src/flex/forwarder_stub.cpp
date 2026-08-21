#include <Windows.h>
#include <limits.h>

#include "kf2/flex/flex_observation_shared.hpp"

extern "C" int _fltused = 0;
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}
extern "C" void* memset(void* destination, int value, size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<unsigned char>(value);
    return destination;
}

namespace {
using kf2::flex::ObservationShared;
INIT_ONCE observation_once{};
HANDLE observation_mapping{};
ObservationShared* observation{};
struct SolverSlot {
    void* solver{};
    unsigned calls{};
    ULONGLONG last_tick{};
    int capacity{};
    int active_particles{};
    ULONGLONG active_count_tick{};
    bool active_particles_valid{};
};
SRWLOCK solver_lock = SRWLOCK_INIT;
SolverSlot solver_slots[64]{};
volatile LONG solver_quarantine{};

void saturated_increment(volatile LONGLONG* value) noexcept;

void saturated_add(volatile LONGLONG* value, int amount) noexcept {
    if (amount <= 0) return;
    LONGLONG current = InterlockedCompareExchange64(value, 0, 0);
    while (current != LLONG_MAX) {
        const auto room = LLONG_MAX - current;
        const auto increment = static_cast<LONGLONG>(amount);
        const auto next = increment > room ? LLONG_MAX : current + increment;
        const auto observed = InterlockedCompareExchange64(value, next, current);
        if (observed == current) return;
        current = observed;
    }
}

void record_transfer(ObservationShared* shared, bool upload,
                     volatile LONGLONG* calls, int elements,
                     int memory) noexcept {
    if (!shared) return;
    saturated_increment(calls);
    if (elements < 0) return;
    if (upload) {
        saturated_add(&shared->upload_elements, elements);
        InterlockedExchange(&shared->last_upload_elements, elements);
        InterlockedExchange(&shared->last_upload_memory, memory);
    } else {
        saturated_add(&shared->download_elements, elements);
        InterlockedExchange(&shared->last_download_elements, elements);
        InterlockedExchange(&shared->last_download_memory, memory);
    }
}

void publish_quarantine(ObservationShared* shared) noexcept {
    InterlockedExchange(&solver_quarantine, 1);
    if (shared) InterlockedExchange(&shared->solver_tracking_quarantined, 1);
}

void publish_solver_snapshot(ObservationShared* shared) noexcept {
    if (!shared) return;
    InterlockedIncrement(&shared->aggregate_snapshot_sequence);
    MemoryBarrier();
    int live = 0;
    int capacity = 0;
    int active = 0;
    bool capacity_valid = true;
    bool counts_valid = true;
    ULONGLONG oldest_active_tick = ULLONG_MAX;
    for (const auto& slot : solver_slots) {
        if (!slot.solver) continue;
        ++live;
        if (slot.capacity <= 0 || capacity > INT_MAX - slot.capacity) {
            capacity_valid = false;
        } else {
            capacity += slot.capacity;
        }
        if (!slot.active_particles_valid || slot.active_particles < 0 ||
            slot.active_particles > slot.capacity || active > INT_MAX - slot.active_particles) {
            counts_valid = false;
        } else {
            active += slot.active_particles;
            if (slot.active_count_tick < oldest_active_tick)
                oldest_active_tick = slot.active_count_tick;
        }
    }
    if (live == 0) {
        capacity_valid = false;
        counts_valid = false;
    }
    counts_valid = counts_valid && capacity_valid;
    InterlockedExchange(&shared->live_solvers, live);
    LONG previous_max = InterlockedCompareExchange(&shared->max_live_solvers, 0, 0);
    while (live > previous_max &&
           InterlockedCompareExchange(&shared->max_live_solvers,
                                      live, previous_max) != previous_max) {
        previous_max = InterlockedCompareExchange(&shared->max_live_solvers, 0, 0);
    }
    InterlockedExchange(&shared->aggregate_particle_capacity,
                        capacity_valid ? capacity : 0);
    InterlockedExchange(&shared->aggregate_active_particles,
                        counts_valid ? active : 0);
    InterlockedExchange(&shared->aggregate_free_particles,
                        counts_valid ? capacity - active : 0);
    InterlockedExchange(&shared->aggregate_capacity_valid, capacity_valid ? 1 : 0);
    InterlockedExchange(&shared->aggregate_counts_valid, counts_valid ? 1 : 0);
    InterlockedExchange64(&shared->oldest_active_count_tick,
        counts_valid ? static_cast<LONGLONG>(oldest_active_tick) : 0);
    MemoryBarrier();
    InterlockedIncrement(&shared->aggregate_snapshot_sequence);
}

bool register_solver(void* solver, int capacity, ULONGLONG tick,
                     ObservationShared* shared) noexcept {
    if (!solver || !TryAcquireSRWLockExclusive(&solver_lock)) {
        publish_quarantine(shared);
        if (shared) saturated_increment(&shared->tracking_drop_calls);
        if (shared) InterlockedExchange(&shared->aggregate_capacity_valid, 0);
        return false;
    }
    SolverSlot* destination = nullptr;
    for (auto& slot : solver_slots) {
        if (slot.solver == solver) {
            destination = &slot;
            break;
        }
        if (!destination && !slot.solver) destination = &slot;
    }
    if (destination) {
        *destination = {};
        destination->solver = solver;
        destination->capacity = capacity;
        destination->last_tick = tick;
    } else {
        publish_quarantine(shared);
        if (shared) saturated_increment(&shared->tracking_drop_calls);
    }
    publish_solver_snapshot(shared);
    ReleaseSRWLockExclusive(&solver_lock);
    return destination != nullptr;
}

unsigned track_solver(void* solver, ULONGLONG tick, bool& tracked,
                      ObservationShared* shared) noexcept {
    tracked = false;
    if (!solver || InterlockedCompareExchange(&solver_quarantine, 0, 0) != 0 ||
        !TryAcquireSRWLockExclusive(&solver_lock)) {
        if (shared) saturated_increment(&shared->tracking_drop_calls);
        return 0;
    }
    SolverSlot* free_slot = nullptr;
    for (auto& slot : solver_slots) {
        if (slot.solver == solver) {
            if (slot.last_tick && tick >= slot.last_tick && tick - slot.last_tick >= 5000)
                slot.calls = 1;
            else if (slot.calls != UINT_MAX)
                ++slot.calls;
            slot.last_tick = tick;
            const auto calls = slot.calls;
            tracked = true;
            ReleaseSRWLockExclusive(&solver_lock);
            return calls;
        }
        if (!free_slot && !slot.solver) free_slot = &slot;
    }
    if (free_slot) {
        free_slot->solver = solver;
        free_slot->calls = 1;
        free_slot->last_tick = tick;
        tracked = true;
        publish_solver_snapshot(shared);
    }
    if (!tracked) {
        publish_quarantine(shared);
        if (shared) saturated_increment(&shared->tracking_drop_calls);
    }
    ReleaseSRWLockExclusive(&solver_lock);
    return tracked ? 1 : 0;
}

void retire_solver(void* solver, ObservationShared* shared) noexcept {
    if (!solver) return;
    if (!TryAcquireSRWLockExclusive(&solver_lock)) {
        publish_quarantine(shared);
        if (shared) saturated_increment(&shared->tracking_drop_calls);
        return;
    }
    for (auto& slot : solver_slots) if (slot.solver == solver) slot = {};
    publish_solver_snapshot(shared);
    ReleaseSRWLockExclusive(&solver_lock);
}

void record_active_count(void* solver, int count, ULONGLONG tick,
                         ObservationShared* shared) noexcept {
    if (!solver || !TryAcquireSRWLockExclusive(&solver_lock)) {
        publish_quarantine(shared);
        if (shared) saturated_increment(&shared->tracking_drop_calls);
        if (shared) InterlockedExchange(&shared->aggregate_counts_valid, 0);
        return;
    }
    for (auto& slot : solver_slots) {
        if (slot.solver != solver) continue;
        slot.active_particles = count;
        slot.active_count_tick = tick;
        slot.active_particles_valid = true;
        publish_solver_snapshot(shared);
        ReleaseSRWLockExclusive(&solver_lock);
        return;
    }
    publish_quarantine(shared);
    if (shared) saturated_increment(&shared->tracking_drop_calls);
    if (shared) InterlockedExchange(&shared->aggregate_counts_valid, 0);
    ReleaseSRWLockExclusive(&solver_lock);
}

void saturated_increment(volatile LONGLONG* value) noexcept {
    LONGLONG current = InterlockedCompareExchange64(value, 0, 0);
    while (current != LLONG_MAX) {
        const auto next = current + 1;
        const auto observed = InterlockedCompareExchange64(value, next, current);
        if (observed == current) return;
        current = observed;
    }
}

BOOL CALLBACK initialize_observation(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t name[96]{};
    const wchar_t prefix[] = L"Local\\KF2OptimizerNext_FlexObservation_v1_";
    unsigned index = 0;
    for (; prefix[index] && index + 1 < _countof(name); ++index) name[index] = prefix[index];
    wchar_t digits[12]{}; unsigned count = 0; DWORD pid = GetCurrentProcessId();
    do { digits[count++] = static_cast<wchar_t>(L'0' + pid % 10); pid /= 10; }
    while (pid && count < _countof(digits));
    while (count && index + 1 < _countof(name)) name[index++] = digits[--count];
    name[index] = 0;
    auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(ObservationShared), name);
    if (!mapping) return FALSE;
    auto* shared = static_cast<ObservationShared*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ObservationShared)));
    if (!shared) { CloseHandle(mapping); return FALSE; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        UnmapViewOfFile(shared); CloseHandle(mapping); return FALSE;
    }
    auto* raw = reinterpret_cast<volatile unsigned char*>(shared);
    for (SIZE_T i = 0; i < sizeof(*shared); ++i) raw[i] = 0;
    shared->version = kf2::flex::observation_version;
    shared->size = sizeof(*shared);
    shared->pid = GetCurrentProcessId();
    shared->process_start_low = created.dwLowDateTime;
    shared->process_start_high = created.dwHighDateTime;
    shared->min_substeps = LONG_MAX;
    shared->min_forwarded_substeps = LONG_MAX;
    shared->min_active_particles = LONG_MAX;
    shared->state = 1;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&shared->magic),
                        static_cast<LONG>(kf2::flex::observation_magic));
    observation_mapping = mapping;
    observation = shared;
    return TRUE;
}

ObservationShared* observation_state() noexcept {
    return InitOnceExecuteOnce(&observation_once, initialize_observation,
                               nullptr, nullptr) ? observation : nullptr;
}

HMODULE original_module() noexcept {
    // The PE loader has already loaded this sibling because the remaining 37
    // exports are direct forwarders. Never load a DLL or perform file I/O from
    // the game call path.
    return GetModuleHandleW(L"flexRelease_original.dll");
}
}  // namespace

extern "C" void flexDestroySolver(void* solver) noexcept {
    using Function = void (*)(void*);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexDestroySolver")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && !solver) saturated_increment(&shared->invalid_argument_calls);
    retire_solver(solver, shared);
    if (shared) saturated_increment(&shared->destroy_calls);
    if (function) function(solver);
}

// The pinned KF2 1.0.5 binary at ordinal 9 consumes only the first integer
// argument. Its entry point stores ECX and overwrites RDX/R8 without reading
// them, so this relay deliberately follows that verified one-argument ABI.
extern "C" void* flexCreateSolver(int max_particles) noexcept {
    using Function = void* (*)(int);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexCreateSolver")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && max_particles <= 0)
        saturated_increment(&shared->invalid_argument_calls);
    void* const solver = function ? function(max_particles) : nullptr;
    if (function) {
        if (shared) {
            saturated_increment(&shared->create_calls);
            const auto tick = GetTickCount64();
            InterlockedExchange64(&shared->last_create_tick,
                                  static_cast<LONGLONG>(tick));
            if (solver) register_solver(solver, max_particles, tick, shared);
        }
    }
    return solver;
}

extern "C" int flexGetVersion() noexcept {
    using Function = int (*)();
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexGetVersion")) : nullptr;
    if (auto* shared = observation_state(); shared && !function)
        saturated_increment(&shared->missing_original_calls);
    return function ? function() : 0;
}

extern "C" int flexGetActiveCount(void* solver) noexcept {
    using Function = int (*)(void*);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexGetActiveCount")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && !solver) saturated_increment(&shared->invalid_argument_calls);
    const int count = function ? function(solver) : 0;
    if (function && count >= 0) {
        if (shared) {
            InterlockedExchange(&shared->last_active_particles, count);
            LONG minimum = InterlockedCompareExchange(
                &shared->min_active_particles, 0, 0);
            while (count < minimum &&
                   InterlockedCompareExchange(&shared->min_active_particles,
                       count, minimum) != minimum) {
                minimum = InterlockedCompareExchange(
                    &shared->min_active_particles, 0, 0);
            }
            LONG maximum = InterlockedCompareExchange(
                &shared->max_active_particles, 0, 0);
            while (count > maximum &&
                   InterlockedCompareExchange(&shared->max_active_particles,
                       count, maximum) != maximum) {
                maximum = InterlockedCompareExchange(
                    &shared->max_active_particles, 0, 0);
            }
            InterlockedExchange(&shared->active_particles_valid, 1);
            saturated_increment(&shared->active_count_calls);
            const auto tick = GetTickCount64();
            InterlockedExchange64(&shared->last_active_count_tick,
                                  static_cast<LONGLONG>(tick));
            record_active_count(solver, count, tick, shared);
        }
    }
    return count;
}

extern "C" void flexGetBounds(void* solver, void* lower, void* upper) noexcept {
    using Function = void (*)(void*, void*, void*);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexGetBounds")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && (!solver || !lower || !upper))
        saturated_increment(&shared->invalid_argument_calls);
    if (function) function(solver, lower, upper);
    if (function && shared) saturated_increment(&shared->bounds_calls);
}

extern "C" void flexSetParams(void* solver, const void* params) noexcept {
    using Function = void (*)(void*, const void*);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexSetParams")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && (!solver || !params))
        saturated_increment(&shared->invalid_argument_calls);
    if (function) function(solver, params);
    if (function && shared) saturated_increment(&shared->params_calls);
}

template <bool Upload>
void relay_buffer_transfer(const char* export_name, void* solver,
                           void* buffer, int elements, int memory,
                           volatile LONGLONG kf2::flex::ObservationShared::* counter) noexcept {
    using Function = void (*)(void*, void*, int, int);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, export_name)) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && (!solver || (!buffer && elements > 0) || elements < 0))
        saturated_increment(&shared->invalid_argument_calls);
    if (function) function(solver, buffer, elements, memory);
    if (function && shared)
        record_transfer(shared, Upload, &(shared->*counter), elements, memory);
}

extern "C" void flexGetParticles(void* solver, void* particles,
                                  int elements, int memory) noexcept {
    relay_buffer_transfer<false>("flexGetParticles", solver, particles,
        elements, memory, &kf2::flex::ObservationShared::particle_download_calls);
}

extern "C" void flexGetPhases(void* solver, void* phases,
                               int elements, int memory) noexcept {
    relay_buffer_transfer<false>("flexGetPhases", solver, phases,
        elements, memory, &kf2::flex::ObservationShared::phase_download_calls);
}

extern "C" void flexGetVelocities(void* solver, void* velocities,
                                   int elements, int memory) noexcept {
    relay_buffer_transfer<false>("flexGetVelocities", solver, velocities,
        elements, memory, &kf2::flex::ObservationShared::velocity_download_calls);
}

extern "C" void flexSetParticles(void* solver, const void* particles,
                                  int elements, int memory) noexcept {
    relay_buffer_transfer<true>("flexSetParticles", solver,
        const_cast<void*>(particles), elements, memory,
        &kf2::flex::ObservationShared::particle_upload_calls);
}

extern "C" void flexSetPhases(void* solver, const void* phases,
                               int elements, int memory) noexcept {
    relay_buffer_transfer<true>("flexSetPhases", solver,
        const_cast<void*>(phases), elements, memory,
        &kf2::flex::ObservationShared::phase_upload_calls);
}

extern "C" void flexSetVelocities(void* solver, const void* velocities,
                                   int elements, int memory) noexcept {
    relay_buffer_transfer<true>("flexSetVelocities", solver,
        const_cast<void*>(velocities), elements, memory,
        &kf2::flex::ObservationShared::velocity_upload_calls);
}

extern "C" void flexSetFence() noexcept {
    using Function = void (*)();
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexSetFence")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (!function) return;
    function();
    if (shared) {
        saturated_increment(&shared->fence_set_calls);
        InterlockedExchange64(&shared->last_fence_set_tick,
                              static_cast<LONGLONG>(GetTickCount64()));
    }
}

extern "C" void flexWaitFence() noexcept {
    using Function = void (*)();
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexWaitFence")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (!function) return;
    // This relay never adds a wait. It observes only the exact wait KF2 requested.
    function();
    if (shared) {
        saturated_increment(&shared->fence_wait_calls);
        InterlockedExchange64(&shared->last_fence_wait_tick,
                              static_cast<LONGLONG>(GetTickCount64()));
    }
}

extern "C" void flexUpdateSolver(void* solver, float delta_time,
                                  int substeps, void* timers) noexcept {
    using Function = void (*)(void*, float, int, void*);
    const auto module = original_module();
    const auto function = module ? reinterpret_cast<Function>(
        GetProcAddress(module, "flexUpdateSolver")) : nullptr;
    auto* shared = observation_state();
    if (shared && !function) saturated_increment(&shared->missing_original_calls);
    if (shared && (!solver || substeps < 0 || substeps > 64))
        saturated_increment(&shared->invalid_argument_calls);
    int forwarded_substeps = substeps;
    bool solver_tracked = false;
    const auto tick = GetTickCount64();
    const auto solver_calls = track_solver(solver, tick, solver_tracked, shared);
    if (shared) {
        const auto heartbeat = static_cast<ULONGLONG>(InterlockedCompareExchange64(
            &shared->control_heartbeat_tick, 0, 0));
        const LONG cap = InterlockedCompareExchange(&shared->desired_substeps, 0, 0);
        const bool fresh = heartbeat != 0 && tick >= heartbeat && tick - heartbeat <= 1500 &&
            kf2::flex::adaptive_warmup_complete(solver_calls, solver_tracked,
                InterlockedCompareExchange(&solver_quarantine, 0, 0) != 0);
        forwarded_substeps = kf2::flex::adaptive_substeps(substeps, cap, fresh);
        if (forwarded_substeps != substeps) {
            saturated_increment(&shared->constrained_updates);
        }
        InterlockedExchange(&shared->last_forwarded_substeps, forwarded_substeps);
        LONG forwarded_minimum = InterlockedCompareExchange(
            &shared->min_forwarded_substeps, 0, 0);
        while (forwarded_substeps < forwarded_minimum &&
               InterlockedCompareExchange(&shared->min_forwarded_substeps,
                   forwarded_substeps, forwarded_minimum) != forwarded_minimum)
            forwarded_minimum = InterlockedCompareExchange(
                &shared->min_forwarded_substeps, 0, 0);
        LONG forwarded_maximum = InterlockedCompareExchange(
            &shared->max_forwarded_substeps, 0, 0);
        while (forwarded_substeps > forwarded_maximum &&
               InterlockedCompareExchange(&shared->max_forwarded_substeps,
                   forwarded_substeps, forwarded_maximum) != forwarded_maximum)
            forwarded_maximum = InterlockedCompareExchange(
                &shared->max_forwarded_substeps, 0, 0);
    }
    if (shared) {
        saturated_increment(&shared->update_calls);
        InterlockedExchange(&shared->last_substeps, substeps);
        LONG bits{}; static_assert(sizeof(bits) == sizeof(delta_time));
        CopyMemory(&bits, &delta_time, sizeof(bits));
        InterlockedExchange(&shared->last_delta_time_bits, bits);
        LONG minimum = InterlockedCompareExchange(&shared->min_substeps, 0, 0);
        while (substeps < minimum &&
               InterlockedCompareExchange(&shared->min_substeps, substeps, minimum) != minimum)
            minimum = InterlockedCompareExchange(&shared->min_substeps, 0, 0);
        LONG maximum = InterlockedCompareExchange(&shared->max_substeps, 0, 0);
        while (substeps > maximum &&
               InterlockedCompareExchange(&shared->max_substeps, substeps, maximum) != maximum)
            maximum = InterlockedCompareExchange(&shared->max_substeps, 0, 0);
        InterlockedExchange64(&shared->last_update_tick,
                              static_cast<LONGLONG>(GetTickCount64()));
    }
    if (function) {
        function(solver, delta_time, forwarded_substeps, timers);
        if (shared) saturated_increment(&shared->successful_updates);
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_DETACH) {
        if (observation) {
            UnmapViewOfFile(observation);
            observation = nullptr;
        }
        if (observation_mapping) {
            CloseHandle(observation_mapping);
            observation_mapping = nullptr;
        }
    }
    return TRUE;
}
