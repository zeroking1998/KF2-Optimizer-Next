#pragma once

#include <Windows.h>

namespace kf2::flex {

inline constexpr DWORD observation_magic = 0x314F464B; // KFO1
inline constexpr DWORD observation_version = 9;

[[nodiscard]] constexpr int adaptive_substeps(int original, int requested,
                                               bool control_fresh) noexcept {
    return control_fresh && requested >= 1 && requested <= 5 ? requested : original;
}

[[nodiscard]] constexpr bool adaptive_warmup_complete(unsigned calls,
                                                       bool tracked,
                                                       bool quarantined) noexcept {
    return tracked && !quarantined && calls > 180;
}

struct alignas(8) ObservationShared {
    DWORD magic;
    DWORD version;
    DWORD size;
    DWORD pid;
    DWORD process_start_low;
    DWORD process_start_high;
    volatile LONG state;
    volatile LONG last_substeps;
    volatile LONG min_substeps;
    volatile LONG max_substeps;
    volatile LONG last_delta_time_bits;
    volatile LONG desired_substeps;
    volatile LONG last_forwarded_substeps;
    volatile LONG min_forwarded_substeps;
    volatile LONG max_forwarded_substeps;
    volatile LONGLONG update_calls;
    volatile LONGLONG destroy_calls;
    volatile LONGLONG successful_updates;
    volatile LONGLONG last_update_tick;
    volatile LONGLONG control_heartbeat_tick;
    volatile LONGLONG constrained_updates;
    volatile LONG last_active_particles;
    volatile LONG min_active_particles;
    volatile LONG max_active_particles;
    volatile LONG active_particles_valid;
    volatile LONGLONG active_count_calls;
    volatile LONGLONG last_active_count_tick;
    volatile LONGLONG create_calls;
    volatile LONGLONG last_create_tick;
    volatile LONG live_solvers;
    volatile LONG max_live_solvers;
    volatile LONG aggregate_particle_capacity;
    volatile LONG aggregate_active_particles;
    volatile LONG aggregate_free_particles;
    volatile LONG aggregate_capacity_valid;
    volatile LONG aggregate_counts_valid;
    volatile LONG aggregate_snapshot_sequence;
    volatile LONGLONG oldest_active_count_tick;
    volatile LONGLONG missing_original_calls;
    volatile LONGLONG tracking_drop_calls;
    volatile LONGLONG invalid_argument_calls;
    volatile LONGLONG fence_set_calls;
    volatile LONGLONG fence_wait_calls;
    volatile LONGLONG last_fence_set_tick;
    volatile LONGLONG last_fence_wait_tick;
    volatile LONGLONG particle_upload_calls;
    volatile LONGLONG particle_download_calls;
    volatile LONGLONG phase_upload_calls;
    volatile LONGLONG phase_download_calls;
    volatile LONGLONG velocity_upload_calls;
    volatile LONGLONG velocity_download_calls;
    volatile LONGLONG upload_elements;
    volatile LONGLONG download_elements;
    volatile LONGLONG bounds_calls;
    volatile LONGLONG params_calls;
    volatile LONG last_upload_elements;
    volatile LONG last_download_elements;
    volatile LONG last_upload_memory;
    volatile LONG last_download_memory;
    volatile LONG solver_tracking_quarantined;
};

static_assert(sizeof(ObservationShared) == 360);

}  // namespace kf2::flex
