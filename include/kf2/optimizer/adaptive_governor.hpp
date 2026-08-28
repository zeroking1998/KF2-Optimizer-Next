#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "kf2/config/settings.hpp"
#include "kf2/optimizer/adaptive_registry.hpp"
#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/optimizer/resource_pressure.hpp"

namespace kf2::optimizer {

enum class AdaptiveAggressiveness { conservative, balanced, aggressive };
enum class AdaptiveSessionClass { unknown, verified_offline, verified_online,
                                  host_or_listen_server };
enum class AdaptiveTelemetryFreshness { fresh, aging, stale, invalid };
enum class AdaptiveDataQuality { not_available, degraded, valid };
enum class AdaptivePressure { observing, healthy, warning, intervention,
                              emergency };
enum class AdaptiveControllerState { disabled, observing, stable,
                                     warning, intervention, emergency,
                                     target_unreachable, frozen };
enum class AdaptiveDisposition { none, hold, shadow, proposed, keep, rollback,
                                 blocked, skipped_unavailable, pending, applied,
                                 failed, restart_required };
enum class AdaptiveStabilityState { stable, watch, correcting, hold,
                                    recovering, target_unreachable };
enum class AdaptiveCpuWorkload {
    unknown,
    idle_or_frame_limited,
    main_thread_dominant,
    partially_parallel,
    broadly_parallel,
};
enum class AdaptiveBottleneck {
    cpu,
    gpu,
    vram,
    ram,
    paging,
    rendering,
    animation,
    physics,
    ragdoll,
    particles,
    gore,
    flex,
    streaming,
    io_pressure,
    thermal_power,
    mixed,
    unknown,
};

struct AdaptivePolicy {
    int target_fps{60};
    AdaptiveAggressiveness aggressiveness{AdaptiveAggressiveness::balanced};
    int minimum_quality{10};
    int maximum_quality{100};
    int quality_change_budget{2};
    double performance_headroom{0.08};
    bool emergency_enabled{true};
    bool quality_recovery_enabled{true};
    bool manual_locks_enabled{true};
    bool shadow_mode{true};
    bool calibration_enabled{true};
    bool adaptive_logging{true};
    std::uint64_t freshness_limit_ns{2'000'000'000ULL};
    std::uint64_t controller_iteration_budget_ns{2'000'000ULL};
};

struct AdaptiveCapabilities final {
    AdaptiveCapabilityState frame_timing{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState cpu_telemetry{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState gpu_telemetry{AdaptiveCapabilityState::unavailable};

    AdaptiveCapabilityState corpse_telemetry{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState corpse_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState ragdoll_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState corpse_lod_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState skeleton_update_control{AdaptiveCapabilityState::unavailable};

    AdaptiveCapabilityState gore_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState particle_control{AdaptiveCapabilityState::unavailable};

    AdaptiveCapabilityState flex_telemetry{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_particle_budget_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_particle_spawn_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_particle_lifetime_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_fluid_particle_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_nonfluid_particle_control{AdaptiveCapabilityState::unavailable};
    AdaptiveCapabilityState flex_solver_substep_control{AdaptiveCapabilityState::unavailable};
};

struct AdaptiveSample {
    std::uint32_t pid{0};
    std::uint64_t process_start_id{0};
    std::uint64_t timestamp_ns{0};
    std::uint64_t session_generation{0};
    std::uint64_t map_generation{0};
    std::optional<std::uint64_t> adapter_luid;
    AdaptiveSessionClass session_class{AdaptiveSessionClass::unknown};
    AdaptiveCapabilities capabilities;

    std::optional<double> fps;
    std::optional<double> average_fps;
    std::optional<double> frame_time_ms;
    std::optional<double> median_frame_time_ms;
    std::optional<double> p95_frame_time_ms;
    std::optional<double> p99_frame_time_ms;
    std::optional<double> sustained_one_percent_low_fps;
    std::optional<double> one_percent_low_fps;
    std::optional<double> point_one_percent_low_fps;
    std::optional<double> frame_time_variance;
    std::uint64_t stutter_count{0};

    std::optional<double> cpu_percent;
    std::optional<double> system_cpu_percent;
    std::optional<double> critical_core_percent;
    std::optional<double> effective_core_usage;
    std::optional<double> dominant_thread_share_percent;
    std::optional<std::uint32_t> active_cpu_threads;
    std::optional<std::uint32_t> affinity_logical_processors;
    std::optional<std::uint32_t> affinity_physical_cores;
    std::optional<std::uint32_t> system_logical_processors;
    std::optional<double> process_gpu_percent;
    std::optional<double> gpu_percent;
    std::optional<double> graphics_engine_percent;
    std::optional<double> compute_engine_percent;
    std::optional<double> copy_engine_percent;
    std::optional<double> present_queue_pressure;
    std::optional<double> vram_used_bytes;
    std::optional<double> vram_budget_bytes;
    std::optional<double> ram_used_bytes;
    std::optional<double> ram_budget_bytes;
    std::optional<double> commit_used_bytes;
    std::optional<double> commit_budget_bytes;
    std::optional<double> process_private_bytes;
    std::optional<double> paging_pressure;
    std::optional<double> io_pressure;
    std::optional<double> thermal_power_pressure;

    std::optional<double> rendering_pressure;
    std::optional<double> animation_pressure;
    std::optional<double> physics_pressure;
    std::optional<double> ragdoll_pressure;
    std::optional<double> particle_pressure;
    std::optional<double> gore_pressure;
    std::optional<double> flex_pressure;
    std::optional<double> streaming_pressure;

    std::optional<int> live_corpse_burden;
    std::optional<int> user_max_dead_bodies;
    std::optional<int> adaptive_corpse_runtime_limit;
    bool zed_time_protected{false};

    // Controller-owned quality state. It is never inferred from graphics
    // telemetry; callers may provide it only from a verified applied state.
    std::optional<double> quality_score;
    bool minimum_quality_reached{false};

    bool gameplay_context_fresh{false};
    bool visibility_context_fresh{false};
    bool sample_loss{false};
    bool duplicate_sample{false};
    bool discontinuity{false};
    bool session_changed{false};
    bool map_changed{false};
};

struct AdaptiveManualLock {
    std::string_view setting;
    ManualLockState state{ManualLockState::automatic};
    std::optional<double> value;
};

struct AdaptiveDataQualityReport {
    AdaptiveDataQuality quality{AdaptiveDataQuality::not_available};
    double confidence_factor{0.0};
    std::string_view reason{"telemetry_not_available"};
    std::uint64_t sample_age_ns{0};
};

struct AdaptiveBottleneckReport {
    AdaptiveBottleneck type{AdaptiveBottleneck::unknown};
    double confidence{0.0};
    std::array<std::string_view, 8> supporting_signals{};
    std::size_t supporting_count{0};
    std::array<std::string_view, 8> contradicting_signals{};
    std::size_t contradicting_count{0};
    AdaptiveDataQuality data_quality{AdaptiveDataQuality::not_available};
    std::uint64_t sample_age_ns{0};
    std::optional<double> last_confirmed_ab_effect;
};

struct AdaptiveCpuReport {
    AdaptiveCpuWorkload workload{AdaptiveCpuWorkload::unknown};
    std::optional<double> effective_core_usage;
    std::optional<double> critical_thread_percent;
    std::optional<double> dominant_thread_share_percent;
    std::optional<std::uint32_t> active_threads;
    std::optional<std::uint32_t> affinity_logical_processors;
    std::optional<std::uint32_t> affinity_physical_cores;
    bool affinity_limited{false};
};

struct AdaptiveDecision {
    AdaptiveControllerState state{AdaptiveControllerState::disabled};
    AdaptiveDisposition disposition{AdaptiveDisposition::none};
    AdaptiveStabilityState stability_state{AdaptiveStabilityState::hold};
    AdaptiveDataQualityReport data;
    AdaptivePressure pressure{AdaptivePressure::observing};
    AdaptiveBottleneckReport bottleneck;
    AdaptiveCpuReport cpu;
    ResourcePressureSnapshot resources;
    Profile recommended_profile{Profile::balanced};
    std::string_view selected_setting;
    std::string_view reason{"disabled"};
    std::optional<double> old_value;
    std::optional<double> proposed_value;
    std::optional<double> effective_value;
    std::optional<double> predicted_frame_time_ms;
    double target_frame_time_ms{0.0};
    double warning_frame_time_ms{0.0};
    double corrective_frame_time_ms{0.0};
    double critical_frame_time_ms{0.0};
    double prediction_confidence{0.0};
    double drop_risk{0.0};
    double quality_score{100.0};
    double headroom{0.0};
    bool current_frame_pressure{false};
    bool current_resource_pressure{false};
    bool quality_recovery_eligible{false};
    bool rollback_available{false};
    bool target_unreachable{false};
    bool watchdog_frozen{false};
    std::uint64_t restore_generation{0};
    std::uint64_t settings_generation{0};
};

[[nodiscard]] AdaptiveDataQualityReport validate_adaptive_sample(
    const AdaptivePolicy& policy, const AdaptiveSample& sample,
    std::uint64_t now_ns) noexcept;

class AdaptiveGovernor final {
public:
    [[nodiscard]] AdaptiveDecision evaluate(
        const AdaptivePolicy& policy, const AdaptiveSample& sample,
        std::uint64_t now_ns,
        std::span<const AdaptiveManualLock> manual_locks = {}) noexcept;
    void reset() noexcept;
    [[nodiscard]] std::size_t quality_debt_count() const noexcept;

private:
    struct HistorySample {
        std::uint64_t timestamp_ns{0};
        double frame_time_ms{0.0};
        double p95_frame_time_ms{0.0};
    };
    struct QualityDebt {
        std::string_view setting;
        std::uint64_t created_ns{0};
        double visual_cost{0.0};
        double measured_benefit{0.0};
        bool recovery_eligible{false};
    };

    std::array<HistorySample, 128> history_{};
    std::size_t history_size_{0};
    std::size_t history_next_{0};
    std::array<QualityDebt, 32> quality_debt_{};
    std::size_t quality_debt_size_{0};
    std::optional<double> smoothed_frame_time_ms_;
    std::optional<double> smoothed_p95_ms_;
    ResourcePressureEstimator resource_pressure_estimator_;
    AdaptivePressure active_pressure_{AdaptivePressure::observing};
    AdaptivePressure candidate_pressure_{AdaptivePressure::observing};
    std::uint64_t candidate_since_ns_{0};
    std::uint64_t low_percentile_pressure_since_ns_{0};
    std::uint64_t last_direction_change_ns_{0};
    std::uint64_t last_evaluation_ns_{0};
    std::uint64_t identity_start_id_{0};
    std::uint64_t session_generation_{0};
    std::uint64_t map_generation_{0};
    std::uint64_t restore_generation_{0};
    std::uint64_t settings_generation_{0};
    std::uint64_t stabilization_until_ns_{0};
    int target_fps_{0};
    AdaptiveBottleneck held_bottleneck_{AdaptiveBottleneck::unknown};
    std::uint64_t bottleneck_hold_until_ns_{0};
    std::uint32_t direction_changes_{0};
    bool frozen_{false};
};

[[nodiscard]] std::wstring_view adaptive_controller_state_name(
    AdaptiveControllerState state) noexcept;
[[nodiscard]] std::wstring_view adaptive_bottleneck_name(
    AdaptiveBottleneck bottleneck) noexcept;
[[nodiscard]] std::wstring_view adaptive_disposition_name(
    AdaptiveDisposition disposition) noexcept;
[[nodiscard]] std::wstring_view adaptive_stability_state_name(
    AdaptiveStabilityState state) noexcept;
[[nodiscard]] std::string_view adaptive_capability_state_name(
    AdaptiveCapabilityState state) noexcept;
[[nodiscard]] std::wstring_view adaptive_cpu_workload_name(
    AdaptiveCpuWorkload workload) noexcept;

}  // namespace kf2::optimizer
