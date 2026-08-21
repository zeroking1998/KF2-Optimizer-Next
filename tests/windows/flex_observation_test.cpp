#include <Windows.h>

#include <cmath>
#include <cstring>

#include "kf2/flex/flex_observation.hpp"
#include "kf2/flex/flex_observation_shared.hpp"

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
    const float dt = 1.0F / 60.0F;
    std::memcpy(const_cast<LONG*>(&shared->last_delta_time_bits), &dt, sizeof(dt));
    shared->last_update_tick = GetTickCount64();
    kf2::game::GameProcessIdentity identity{pid, start, {}};
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
        std::abs(result->solver_updates_per_second - 60.0) > 0.01) return 4;

    // A sampler can run after the forwarder records a started update but before
    // the original FleX call returns. That single fresh in-flight call is healthy,
    // while a larger or stale completion gap remains a real relay error.
    shared->update_calls = 121;
    shared->successful_updates = 120;
    shared->last_update_tick = GetTickCount64();
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
    if (!kf2::flex::write_adaptive_control(identity, 1) ||
        shared->desired_substeps != 1 || shared->control_heartbeat_tick == 0) return 6;
    if (!kf2::flex::write_adaptive_control(identity, 5) || shared->desired_substeps != 5) return 7;
    if (kf2::flex::write_adaptive_control(identity, 6)) return 9;
    identity.process_start_id++;
    if (kf2::flex::read_observation(identity)) return 5;
    if (kf2::flex::write_adaptive_control(identity, 1)) return 8;
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}
