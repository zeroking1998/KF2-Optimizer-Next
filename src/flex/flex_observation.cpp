#include "kf2/flex/flex_observation.hpp"

#include <Windows.h>
#include <cmath>
#include <cstring>
#include <string>

#include "kf2/flex/flex_observation_shared.hpp"

namespace kf2::flex {

std::optional<ObservationSnapshot> read_observation(
    const game::GameProcessIdentity& process) noexcept {
    if (process.pid == 0 || process.process_start_id == 0) return std::nullopt;
    const auto name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
                      std::to_wstring(process.pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!mapping) return std::nullopt;
    const auto* shared = static_cast<const ObservationShared*>(MapViewOfFile(
        mapping, FILE_MAP_READ, 0, 0, sizeof(ObservationShared)));
    if (!shared) { CloseHandle(mapping); return std::nullopt; }
    const std::uint64_t start =
        (static_cast<std::uint64_t>(shared->process_start_high) << 32U) |
        shared->process_start_low;
    const bool valid = shared->magic == observation_magic &&
        shared->version == observation_version && shared->size == sizeof(*shared) &&
        shared->pid == process.pid && start == process.process_start_id;
    ObservationSnapshot result{};
    if (valid) {
        result.update_calls = static_cast<std::uint64_t>(shared->update_calls);
        result.successful_updates = static_cast<std::uint64_t>(shared->successful_updates);
        result.destroy_calls = static_cast<std::uint64_t>(shared->destroy_calls);
        result.last_update_tick = static_cast<std::uint64_t>(shared->last_update_tick);
        result.constrained_updates = static_cast<std::uint64_t>(shared->constrained_updates);
        result.control_heartbeat_tick =
            static_cast<std::uint64_t>(shared->control_heartbeat_tick);
        result.active_count_calls =
            static_cast<std::uint64_t>(shared->active_count_calls);
        result.last_active_count_tick =
            static_cast<std::uint64_t>(shared->last_active_count_tick);
        result.create_calls = static_cast<std::uint64_t>(shared->create_calls);
        result.last_create_tick = static_cast<std::uint64_t>(shared->last_create_tick);
        result.requested_substeps = shared->desired_substeps;
        result.last_substeps = shared->last_substeps;
        result.min_substeps = shared->min_substeps == LONG_MAX ? 0 : shared->min_substeps;
        result.max_substeps = shared->max_substeps;
        result.last_forwarded_substeps = shared->last_forwarded_substeps;
        result.min_forwarded_substeps = shared->min_forwarded_substeps == LONG_MAX
            ? 0 : shared->min_forwarded_substeps;
        result.max_forwarded_substeps = shared->max_forwarded_substeps;
        result.active_particles = shared->last_active_particles;
        result.min_active_particles = shared->min_active_particles == LONG_MAX
            ? 0 : shared->min_active_particles;
        result.max_active_particles = shared->max_active_particles;
        bool aggregate_snapshot_valid = false;
        for (int attempt = 0; attempt < 4; ++attempt) {
            const LONG before = shared->aggregate_snapshot_sequence;
            if ((before & 1) != 0) continue;
            MemoryBarrier();
            const int live_solvers = shared->live_solvers;
            const int max_live_solvers = shared->max_live_solvers;
            const int particle_capacity = shared->aggregate_particle_capacity;
            const int aggregate_active = shared->aggregate_active_particles;
            const int aggregate_free = shared->aggregate_free_particles;
            const bool capacity_valid = shared->aggregate_capacity_valid != 0;
            const bool counts_valid = shared->aggregate_counts_valid != 0;
            const auto oldest_tick =
                static_cast<std::uint64_t>(shared->oldest_active_count_tick);
            MemoryBarrier();
            const LONG after = shared->aggregate_snapshot_sequence;
            if (before != after || (after & 1) != 0) continue;
            result.live_solvers = live_solvers;
            result.max_live_solvers = max_live_solvers;
            result.particle_capacity = particle_capacity;
            result.aggregate_active_particles = aggregate_active;
            result.free_particles = aggregate_free;
            result.particle_capacity_available = capacity_valid;
        result.oldest_active_count_tick = oldest_tick;
            aggregate_snapshot_valid = counts_valid;
            break;
        }
        result.missing_original_calls =
            static_cast<std::uint64_t>(shared->missing_original_calls);
        result.tracking_drop_calls =
            static_cast<std::uint64_t>(shared->tracking_drop_calls);
        result.invalid_argument_calls =
            static_cast<std::uint64_t>(shared->invalid_argument_calls);
        result.fence_set_calls =
            static_cast<std::uint64_t>(shared->fence_set_calls);
        result.fence_wait_calls =
            static_cast<std::uint64_t>(shared->fence_wait_calls);
        result.last_fence_set_tick =
            static_cast<std::uint64_t>(shared->last_fence_set_tick);
        result.last_fence_wait_tick =
            static_cast<std::uint64_t>(shared->last_fence_wait_tick);
        result.particle_upload_calls =
            static_cast<std::uint64_t>(shared->particle_upload_calls);
        result.particle_download_calls =
            static_cast<std::uint64_t>(shared->particle_download_calls);
        result.phase_upload_calls =
            static_cast<std::uint64_t>(shared->phase_upload_calls);
        result.phase_download_calls =
            static_cast<std::uint64_t>(shared->phase_download_calls);
        result.velocity_upload_calls =
            static_cast<std::uint64_t>(shared->velocity_upload_calls);
        result.velocity_download_calls =
            static_cast<std::uint64_t>(shared->velocity_download_calls);
        result.upload_elements =
            static_cast<std::uint64_t>(shared->upload_elements);
        result.download_elements =
            static_cast<std::uint64_t>(shared->download_elements);
        result.bounds_calls = static_cast<std::uint64_t>(shared->bounds_calls);
        result.params_calls = static_cast<std::uint64_t>(shared->params_calls);
        result.last_upload_elements = shared->last_upload_elements;
        result.last_download_elements = shared->last_download_elements;
        result.last_upload_memory = shared->last_upload_memory;
        result.last_download_memory = shared->last_download_memory;
        result.solver_tracking_quarantined =
            shared->solver_tracking_quarantined != 0;
        const LONG bits = shared->last_delta_time_bits;
        std::memcpy(&result.last_delta_time, &bits, sizeof(bits));
        const auto now = GetTickCount64();
        result.fresh = result.last_update_tick != 0 && now >= result.last_update_tick &&
                       now - result.last_update_tick <= 3000;
        const bool counters_match =
            result.update_calls == result.successful_updates;
        const bool one_update_is_in_flight =
            result.fresh && result.update_calls > result.successful_updates &&
            result.update_calls - result.successful_updates == 1;
        result.pass_through_healthy =
            (counters_match || one_update_is_in_flight) &&
            result.missing_original_calls == 0 &&
            result.tracking_drop_calls == 0 &&
            result.invalid_argument_calls == 0 &&
            !result.solver_tracking_quarantined;
        result.control_fresh = result.control_heartbeat_tick != 0 &&
            now >= result.control_heartbeat_tick &&
            now - result.control_heartbeat_tick <= 1500;
        result.active_particles_fresh = shared->active_particles_valid != 0 &&
            result.active_count_calls > 0 && result.last_active_count_tick != 0 &&
            now >= result.last_active_count_tick &&
            now - result.last_active_count_tick <= 3000;
        result.aggregate_particles_fresh = aggregate_snapshot_valid &&
            result.oldest_active_count_tick != 0 &&
            now >= result.oldest_active_count_tick &&
            now - result.oldest_active_count_tick <= 3000;
        if (result.last_delta_time > 0.0F)
            result.solver_updates_per_second = 1.0 / result.last_delta_time;
        if (result.update_calls > 0 &&
            (result.successful_updates > result.update_calls ||
             result.last_substeps < 0 || result.last_substeps > 64 ||
             result.min_substeps < 0 || result.min_substeps > 64 ||
             result.max_substeps < result.min_substeps || result.max_substeps > 64 ||
             result.last_forwarded_substeps < 0 || result.last_forwarded_substeps > 5 ||
             result.min_forwarded_substeps < 0 || result.min_forwarded_substeps > 5 ||
             result.max_forwarded_substeps < result.min_forwarded_substeps ||
             result.max_forwarded_substeps > 5 ||
             result.requested_substeps < 0 || result.requested_substeps > 5 ||
             result.active_particles < 0 || result.min_active_particles < 0 ||
             result.max_active_particles < result.min_active_particles ||
             result.active_particles > result.max_active_particles ||
             result.live_solvers < 0 || result.live_solvers > 64 ||
             result.max_live_solvers < result.live_solvers ||
             result.max_live_solvers > 64 ||
             result.particle_capacity < 0 ||
             result.last_upload_elements < 0 ||
             result.last_download_elements < 0 ||
             result.aggregate_active_particles < 0 || result.free_particles < 0 ||
             (result.particle_capacity_available && result.live_solvers == 0) ||
             (result.particle_capacity_available &&
              result.aggregate_active_particles + result.free_particles !=
                  result.particle_capacity) ||
             !std::isfinite(result.last_delta_time) ||
             result.last_delta_time <= 0.0F || result.last_delta_time > 1.0F)) {
            UnmapViewOfFile(shared);
            CloseHandle(mapping);
            return std::nullopt;
        }
    }
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    if (!valid) return std::nullopt;
    return result;
}

bool write_adaptive_control(const game::GameProcessIdentity& process,
                            int maximum_substeps) noexcept {
    if (process.pid == 0 || process.process_start_id == 0 ||
        maximum_substeps < 0 || maximum_substeps > 5) return false;
    const auto name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
                      std::to_wstring(process.pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
    if (!mapping) return false;
    auto* shared = static_cast<ObservationShared*>(MapViewOfFile(
        mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(ObservationShared)));
    if (!shared) { CloseHandle(mapping); return false; }
    const std::uint64_t start =
        (static_cast<std::uint64_t>(shared->process_start_high) << 32U) |
        shared->process_start_low;
    const bool valid = shared->magic == observation_magic &&
        shared->version == observation_version && shared->size == sizeof(*shared) &&
        shared->pid == process.pid && start == process.process_start_id;
    if (valid) {
        InterlockedExchange(&shared->desired_substeps, maximum_substeps);
        InterlockedExchange64(&shared->control_heartbeat_tick,
                              static_cast<LONGLONG>(GetTickCount64()));
    }
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return valid;
}

}  // namespace kf2::flex
