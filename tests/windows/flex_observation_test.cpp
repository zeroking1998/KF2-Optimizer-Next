#include <Windows.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "kf2/flex/flex_observation.hpp"
#include "kf2/flex/flex_observation_shared.hpp"

namespace {

bool fail_allocations = false;

bool reuses_session_mapping(const kf2::game::GameProcessIdentity& identity,
                           kf2::flex::ObservationShared& shared) {
    DWORD before{}, count{};
    if (!GetProcessHandleCount(GetCurrentProcess(), &before)) return false;
    {
        kf2::flex::ObservationReader reader;
        static_assert(noexcept(reader.read(identity)) && noexcept(reader.reset()));
        fail_allocations = true;
        bool current = true;
        for (unsigned i = 0; i < 256 && current; ++i) {
            ++shared.update_calls;
            ++shared.successful_updates;
            shared.last_forwarded_substeps = 2 + (i % 3);
            const auto observed = reader.read(identity);
            current = observed && observed->fresh && observed->pass_through_healthy &&
                observed->update_calls == static_cast<std::uint64_t>(shared.update_calls) &&
                observed->last_forwarded_substeps == shared.last_forwarded_substeps &&
                GetProcessHandleCount(GetCurrentProcess(), &count) && count == before + 1;
        }
        fail_allocations = false;
        if (!current) return false;
        shared.last_update_tick = 1;
        const auto stale = reader.read(identity);
        if (!stale || stale->fresh) return false;
        shared.last_update_tick = GetTickCount64();
        auto wrong_identity = identity;
        ++wrong_identity.process_start_id;
        if (reader.read(wrong_identity) ||
            !GetProcessHandleCount(GetCurrentProcess(), &count) || count != before ||
            !reader.read(identity)) return false;
        shared.magic = 0;
        if (reader.read(identity) ||
            !GetProcessHandleCount(GetCurrentProcess(), &count) || count != before)
            return false;
        shared.magic = kf2::flex::observation_magic;
        if (!reader.read(identity)) return false;
        reader.reset();
        if (!GetProcessHandleCount(GetCurrentProcess(), &count) || count != before ||
            !reader.read(identity)) return false;
    }
    return GetProcessHandleCount(GetCurrentProcess(), &count) && count == before;
}

bool rejects_exited_producer(const kf2::flex::ObservationShared& sample,
                            const kf2::game::GameProcessIdentity& previous) {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    // Only this owned suspended child is terminated; it never runs test code.
    if (!CreateProcessW(executable, nullptr, nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &child)) return false;
    CloseHandle(child.hThread);
    const auto close_child = [](void* process) {
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 2000);
        }
        CloseHandle(process);
    };
    const std::unique_ptr<void, decltype(close_child)> owned{
        child.hProcess, close_child};
    const auto bound = kf2::game::bind_game_process(child.dwProcessId, executable);
    if (!bound.has_value()) return false;
    const auto& identity = bound.value();
    const auto name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
                      std::to_wstring(identity.pid);
    const std::unique_ptr<void, decltype(&CloseHandle)> mapping{
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(sample), name.c_str()), &CloseHandle};
    if (!mapping) return false;
    const std::unique_ptr<void, decltype(&UnmapViewOfFile)> view{
        MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(sample)),
        &UnmapViewOfFile};
    if (!view) return false;
    auto* shared = static_cast<kf2::flex::ObservationShared*>(view.get());
    *shared = sample;
    shared->pid = identity.pid;
    shared->process_start_low = static_cast<LONG>(identity.process_start_id);
    shared->process_start_high =
        static_cast<LONG>(identity.process_start_id >> 32U);
    shared->last_update_tick = GetTickCount64();
    kf2::flex::ObservationReader reader;
    if (!reader.read(previous) || !reader.read(identity) ||
        !kf2::flex::write_fixed_control(identity, false)) return false;
    const auto heartbeat = shared->control_heartbeat_tick;
    if (!TerminateProcess(owned.get(), 0) ||
        WaitForSingleObject(owned.get(), 2000) != WAIT_OBJECT_0) return false;
    // The retained mapping still has matching identity bytes and a fresh tick.
    return !reader.read(identity) && !kf2::flex::read_observation(identity) &&
           !kf2::flex::write_fixed_control(identity, true) &&
           shared->diagnostics_enabled == 0 &&
           shared->control_heartbeat_tick == heartbeat && reader.read(previous);
}

}  // namespace

void* operator new(std::size_t size) {
    if (fail_allocations) throw std::bad_alloc{};
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

int wmain() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 1;
    const auto pid = GetCurrentProcessId();
    const auto start = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) |
                       created.dwLowDateTime;
    const auto name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
                      std::to_wstring(pid);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(kf2::flex::ObservationShared), name.c_str());
    if (!mapping) return 2;
    auto* shared = static_cast<kf2::flex::ObservationShared*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(kf2::flex::ObservationShared)));
    if (!shared) { CloseHandle(mapping); return 3; }
    *shared = {};
    shared->magic = kf2::flex::observation_magic;
    shared->version = kf2::flex::observation_version;
    shared->size = sizeof(*shared);
    shared->pid = pid;
    shared->process_start_low = created.dwLowDateTime;
    shared->process_start_high = created.dwHighDateTime;
    shared->update_calls = 120;
    shared->successful_updates = 120;
    shared->last_substeps = 3;
    shared->min_substeps = 2;
    shared->max_substeps = 4;
    shared->last_forwarded_substeps = 4;
    shared->min_forwarded_substeps = 2;
    shared->max_forwarded_substeps = 4;
    shared->active_count_calls = 18;
    shared->last_active_particles = 37;
    shared->min_active_particles = 0;
    shared->max_active_particles = 42;
    shared->active_particles_valid = 1;
    shared->last_active_count_tick = GetTickCount64();
    shared->create_calls = 2;
    shared->last_create_tick = GetTickCount64();
    shared->live_solvers = 2;
    shared->max_live_solvers = 3;
    shared->aggregate_particle_capacity = 1280;
    shared->aggregate_active_particles = 74;
    shared->aggregate_free_particles = 1206;
    shared->aggregate_capacity_valid = 1;
    shared->aggregate_counts_valid = 1;
    shared->aggregate_snapshot_sequence = 2;
    shared->oldest_active_count_tick = GetTickCount64();
    shared->fence_set_calls = 12;
    shared->fence_wait_calls = 11;
    shared->last_fence_set_tick = GetTickCount64();
    shared->last_fence_wait_tick = GetTickCount64();
    shared->particle_upload_calls = 8;
    shared->particle_download_calls = 7;
    shared->phase_upload_calls = 6;
    shared->phase_download_calls = 5;
    shared->velocity_upload_calls = 4;
    shared->velocity_download_calls = 3;
    shared->upload_elements = 1800;
    shared->download_elements = 1400;
    shared->last_upload_elements = 256;
    shared->last_download_elements = 128;
    shared->last_upload_memory = 1;
    shared->last_download_memory = 2;
    shared->bounds_calls = 9;
    shared->params_calls = 10;
    shared->diagnostics_enabled = 1;
    const float dt = 1.0F / 60.0F;
    std::memcpy(const_cast<LONG*>(&shared->last_delta_time_bits), &dt, sizeof(dt));
    shared->last_update_tick = GetTickCount64();
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 21;
    auto bound = kf2::game::bind_game_process(pid, executable);
    if (!bound.has_value()) return 22;
    auto identity = bound.value();
    if (identity.process_start_id != start) return 23;
    // Opt-in work uses the existing session view and leaves detailed diagnostics alone.
    shared->own_work_frequency = 10'000'000;
    shared->own_work_ticks = 120'000;
    kf2::flex::ObservationReader own_work_reader;
    const auto measured = own_work_reader.read(identity, true);
    if (!measured || measured->own_work_ns != 12'000'000 ||
        shared->own_work_lease_tick <= static_cast<LONGLONG>(GetTickCount64()) ||
        shared->diagnostics_enabled != 1) return 27;
    const auto disabled = own_work_reader.read(identity);
    if (!disabled || disabled->own_work_ns || shared->own_work_lease_tick != 0)
        return 28;
    auto wrong_work_identity = identity;
    ++wrong_work_identity.process_start_id;
    if (own_work_reader.read(wrong_work_identity, true) ||
        shared->own_work_lease_tick != 0) return 29;
    static_assert(noexcept(kf2::flex::read_observation(identity)));
    static_assert(noexcept(kf2::flex::write_fixed_control(identity, false)));
    const auto result = kf2::flex::read_observation(identity);
    if (!result || !result->fresh || !result->pass_through_healthy ||
        result->last_substeps != 3 || result->min_substeps != 2 ||
        result->max_substeps != 4 || result->last_forwarded_substeps != 4 ||
        !result->active_particles_fresh || result->active_count_calls != 18 ||
        result->active_particles != 37 || result->min_active_particles != 0 ||
        result->max_active_particles != 42 ||
        result->create_calls != 2 || result->live_solvers != 2 ||
        result->max_live_solvers != 3 || !result->particle_capacity_available ||
        !result->aggregate_particles_fresh || result->particle_capacity != 1280 ||
        result->aggregate_active_particles != 74 || result->free_particles != 1206 ||
        result->fence_set_calls != 12 || result->fence_wait_calls != 11 ||
        result->last_fence_set_tick == 0 || result->last_fence_wait_tick == 0 ||
        result->particle_upload_calls != 8 || result->particle_download_calls != 7 ||
        result->phase_upload_calls != 6 || result->phase_download_calls != 5 ||
        result->velocity_upload_calls != 4 || result->velocity_download_calls != 3 ||
        result->upload_elements != 1800 || result->download_elements != 1400 ||
        result->last_upload_elements != 256 || result->last_download_elements != 128 ||
        result->last_upload_memory != 1 || result->last_download_memory != 2 ||
        result->bounds_calls != 9 || result->params_calls != 10 ||
        result->missing_original_calls != 0 || result->tracking_drop_calls != 0 ||
        result->invalid_argument_calls != 0 || result->solver_tracking_quarantined ||
        !result->diagnostics_enabled ||
        std::abs(result->solver_updates_per_second - 60.0) > 0.01) return 4;

    // Static capacity is known before particle counts have been observed.
    // Missing counts must not invalidate the relay or become fresh zero counts.
    shared->aggregate_counts_valid = 0;
    shared->aggregate_active_particles = 0;
    shared->aggregate_free_particles = 0;
    for (int diagnostics = 0; diagnostics <= 1; ++diagnostics) {
        shared->diagnostics_enabled = diagnostics;
        const auto unobserved_counts = kf2::flex::read_observation(identity);
        if (!unobserved_counts || !unobserved_counts->fresh ||
            !unobserved_counts->pass_through_healthy ||
            !unobserved_counts->particle_capacity_available ||
            unobserved_counts->particle_capacity != 1280 ||
            unobserved_counts->aggregate_particles_fresh) return 16;
    }
    shared->aggregate_counts_valid = 1;
    if (kf2::flex::read_observation(identity)) return 17;
    shared->oldest_active_count_tick = 1;
    if (kf2::flex::read_observation(identity)) return 18;
    shared->aggregate_active_particles = INT_MAX;
    shared->aggregate_free_particles = INT_MAX;
    if (kf2::flex::read_observation(identity)) return 19;
    shared->aggregate_active_particles = 74;
    shared->aggregate_free_particles = 1206;
    const auto stale_counts = kf2::flex::read_observation(identity);
    if (!stale_counts || !stale_counts->pass_through_healthy ||
        stale_counts->aggregate_particles_fresh) return 20;
    shared->oldest_active_count_tick = GetTickCount64();

    // Detailed solver tracking is optional. A diagnostics-only tracking failure
    // must not make the fixed one-substep relay unavailable while diagnostics
    // are off, but it remains visible as unhealthy while diagnostics are on.
    shared->tracking_drop_calls = 1;
    shared->diagnostics_enabled = 0;
    const auto diagnostics_off = kf2::flex::read_observation(identity);
    if (!diagnostics_off || !diagnostics_off->pass_through_healthy) return 14;
    shared->diagnostics_enabled = 1;
    const auto diagnostics_on = kf2::flex::read_observation(identity);
    if (!diagnostics_on || diagnostics_on->pass_through_healthy) return 15;
    shared->tracking_drop_calls = 0;

    // A sampler can run after the forwarder records a started update but before
    // the original FleX call returns. That single fresh in-flight call is healthy,
    // while a larger or stale completion gap remains a real relay error.
    shared->update_calls = 121;
    shared->successful_updates = 120;
    shared->last_update_tick = GetTickCount64();
    const auto before_queries = kf2::game::detail::process_query_counts_for_testing();
    const auto in_flight = kf2::flex::read_observation(identity);
    if (!in_flight || !in_flight->fresh || !in_flight->pass_through_healthy) return 10;

    shared->update_calls = 122;
    const auto excessive_gap = kf2::flex::read_observation(identity);
    if (!excessive_gap || excessive_gap->pass_through_healthy) return 11;

    shared->update_calls = 121;
    shared->last_update_tick = 1;
    const auto stale_gap = kf2::flex::read_observation(identity);
    if (!stale_gap || stale_gap->fresh || stale_gap->pass_through_healthy) return 12;

    shared->update_calls = 120;
    shared->successful_updates = 120;
    shared->last_update_tick = GetTickCount64();
    fail_allocations = true;
    const auto allocation_free_read = kf2::flex::read_observation(identity);
    const bool allocation_free_write =
        kf2::flex::write_fixed_control(identity, false);
    fail_allocations = false;
    if (!allocation_free_read || !allocation_free_write ||
        shared->desired_substeps != 1 || shared->diagnostics_enabled != 0)
        return 13;
    if (!kf2::flex::write_fixed_control(identity, true) ||
        shared->desired_substeps != 1 || shared->diagnostics_enabled != 1 ||
        shared->control_heartbeat_tick == 0) return 6;
    const auto after_queries = kf2::game::detail::process_query_counts_for_testing();
    if (before_queries.opens != after_queries.opens ||
        before_queries.creation_queries != after_queries.creation_queries) return 24;
    if (!reuses_session_mapping(identity, *shared)) return 26;
    if (!rejects_exited_producer(*shared, identity)) return 25;
    identity.process_start_id++;
    if (kf2::flex::read_observation(identity)) return 5;
    if (kf2::flex::write_fixed_control(identity, true)) return 8;
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}
