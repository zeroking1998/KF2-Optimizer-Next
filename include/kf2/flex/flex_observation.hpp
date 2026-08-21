#pragma once

#include <cstdint>
#include <optional>

#include "kf2/game/game_session.hpp"

namespace kf2::flex {

struct ObservationSnapshot {
    std::uint64_t update_calls{0};
    std::uint64_t successful_updates{0};
    std::uint64_t destroy_calls{0};
    std::uint64_t last_update_tick{0};
    std::uint64_t constrained_updates{0};
    std::uint64_t control_heartbeat_tick{0};
    std::uint64_t active_count_calls{0};
    std::uint64_t last_active_count_tick{0};
    std::uint64_t create_calls{0};
    std::uint64_t last_create_tick{0};
    std::uint64_t oldest_active_count_tick{0};
    std::uint64_t missing_original_calls{0};
    std::uint64_t tracking_drop_calls{0};
    std::uint64_t invalid_argument_calls{0};
    std::uint64_t fence_set_calls{0};
    std::uint64_t fence_wait_calls{0};
    std::uint64_t last_fence_set_tick{0};
    std::uint64_t last_fence_wait_tick{0};
    std::uint64_t particle_upload_calls{0};
    std::uint64_t particle_download_calls{0};
    std::uint64_t phase_upload_calls{0};
    std::uint64_t phase_download_calls{0};
    std::uint64_t velocity_upload_calls{0};
    std::uint64_t velocity_download_calls{0};
    std::uint64_t upload_elements{0};
    std::uint64_t download_elements{0};
    std::uint64_t bounds_calls{0};
    std::uint64_t params_calls{0};
    int requested_substeps{0};
    int last_substeps{0};
    int min_substeps{0};
    int max_substeps{0};
    int last_forwarded_substeps{0};
    int min_forwarded_substeps{0};
    int max_forwarded_substeps{0};
    int active_particles{0};
    int min_active_particles{0};
    int max_active_particles{0};
    int live_solvers{0};
    int max_live_solvers{0};
    int particle_capacity{0};
    int aggregate_active_particles{0};
    int free_particles{0};
    int last_upload_elements{0};
    int last_download_elements{0};
    int last_upload_memory{0};
    int last_download_memory{0};
    float last_delta_time{0.0F};
    bool fresh{false};
    bool pass_through_healthy{false};
    bool control_fresh{false};
    bool active_particles_fresh{false};
    bool particle_capacity_available{false};
    bool aggregate_particles_fresh{false};
    bool solver_tracking_quarantined{false};
    double solver_updates_per_second{0.0};
};

[[nodiscard]] std::optional<ObservationSnapshot> read_observation(
    const game::GameProcessIdentity& process) noexcept;
[[nodiscard]] bool write_adaptive_control(
    const game::GameProcessIdentity& process, int maximum_substeps) noexcept;

}  // namespace kf2::flex
