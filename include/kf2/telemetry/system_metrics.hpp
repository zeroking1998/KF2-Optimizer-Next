#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "kf2/core/result.hpp"
#include "kf2/game/game_session.hpp"

namespace kf2::telemetry {
struct HardwareInventory {
    std::uint32_t physical_cores{0};
    std::uint32_t logical_processors{0};
    std::uint16_t processor_groups{0};
    std::uint64_t installed_memory_bytes{0};
    std::uint64_t available_memory_bytes{0};
};
struct CpuTimes {
    std::uint64_t system_ticks{0};
    std::uint64_t process_ticks{0};
    std::uint64_t idle_ticks{0};
};
struct ProcessMetrics {
    std::optional<double> cpu_percent;
    // Whole-system processor occupancy over the same interval. Adaptive uses
    // it only as shared pressure when KF2 also misses its frame budget.
    std::optional<double> system_cpu_percent;
    // Occupancy of the busiest thread in the bound process over the most
    // recent sampling interval. Unlike cpu_percent this is not diluted by the
    // machine's logical-processor count, so a saturated KF2 game thread stays
    // visible to the Adaptive classifier.
    std::optional<double> critical_core_percent;
    // Total process CPU time expressed as fully occupied logical processors.
    // For example, 1.5 means that the process consumed the equivalent of one
    // and a half logical processors during the sampling interval.
    std::optional<double> effective_core_usage;
    // Share of the process' sampled CPU time owned by its busiest thread.
    std::optional<double> dominant_thread_share_percent;
    // Threads which performed measurable CPU work during the interval.
    std::optional<std::uint32_t> active_cpu_threads;
    // Current process-affinity capacity. These values are observations only;
    // the optimizer never changes another process' affinity.
    std::optional<std::uint32_t> affinity_logical_processors;
    std::optional<std::uint32_t> affinity_physical_cores;
    std::optional<std::uint32_t> system_logical_processors;
    std::uint64_t working_set_bytes{0};
    std::uint64_t private_bytes{0};
};
struct SystemMemoryMetrics {
    std::uint64_t total_physical_bytes{0};
    std::uint64_t available_physical_bytes{0};
    std::uint64_t commit_limit_bytes{0};
    std::uint64_t available_commit_bytes{0};
    double used_percent{0.0};
};
[[nodiscard]] Result<HardwareInventory> query_hardware_inventory();
[[nodiscard]] Result<SystemMemoryMetrics> query_system_memory_metrics();
[[nodiscard]] std::optional<double> calculate_cpu_percent(CpuTimes previous,
                                                          CpuTimes current);
[[nodiscard]] std::optional<double> calculate_system_cpu_percent(
    CpuTimes previous, CpuTimes current);
[[nodiscard]] std::optional<double> calculate_thread_cpu_percent(
    std::uint64_t previous_thread_ticks,
    std::uint64_t current_thread_ticks,
    std::uint64_t elapsed_ms);
namespace detail {
// Discovers only this process's threads. Callers may use Toolhelp when process
// snapshotting is unavailable, but never after identity/exit rejection.
[[nodiscard]] Result<std::vector<std::uint32_t>> query_process_thread_ids(
    const game::GameProcessIdentity& identity);

struct ThreadCpuTimes {
    std::uint64_t creation_ticks{0};
    std::uint64_t cpu_ticks{0};
};
[[nodiscard]] std::optional<double> calculate_thread_cpu_percent(
    ThreadCpuTimes previous, ThreadCpuTimes current, std::uint64_t elapsed_ms);

struct ProcessorGroupMask {
    std::uint16_t group{0};
    std::uintptr_t mask{0};
    bool operator==(const ProcessorGroupMask&) const = default;
};
enum class CpuSetQueryState { unavailable, succeeded, failed };
struct CpuCapacityObservation {
    std::vector<ProcessorGroupMask> system_group_masks;
    std::optional<std::vector<std::uint16_t>> process_groups;
    std::optional<ProcessorGroupMask> primary_group_affinity;
    bool primary_group_affinity_is_full{false};
    bool default_affinity_spans_groups{false};
    CpuSetQueryState cpu_set_query{CpuSetQueryState::unavailable};
    std::vector<ProcessorGroupMask> default_cpu_set_masks;
};
[[nodiscard]] std::optional<std::vector<ProcessorGroupMask>>
resolve_process_capacity_masks(const CpuCapacityObservation& observation);

struct ThreadPressureMetrics {
    double critical_core_percent{0.0};
    double effective_core_usage{0.0};
    double dominant_thread_share_percent{0.0};
    std::uint32_t active_cpu_threads{0};
};

class ThreadPressureCache final {
public:
    void observe(ThreadPressureMetrics metrics,
                 std::uint64_t now_ms) noexcept;
    void miss(std::uint64_t now_ms) noexcept;
    [[nodiscard]] const std::optional<ThreadPressureMetrics>& current()
        const noexcept { return current_; }
private:
    std::optional<ThreadPressureMetrics> current_;
    std::optional<std::uint64_t> last_observation_ms_;
    std::uint32_t consecutive_misses_{0};
};
}  // namespace detail
#ifdef KF2_PROCESS_METRICS_TESTING
namespace detail {
[[nodiscard]] std::uint32_t process_metric_opens_for_testing() noexcept;
void fail_next_process_thread_snapshot_walk_for_testing() noexcept;
void fail_next_toolhelp_thread_walk_for_testing(bool after_matching_entry) noexcept;
}
#endif
class ProcessMetricSampler final {
public:
    explicit ProcessMetricSampler(game::GameProcessIdentity identity);
    ~ProcessMetricSampler();
    ProcessMetricSampler(const ProcessMetricSampler&) = delete;
    ProcessMetricSampler& operator=(const ProcessMetricSampler&) = delete;
    ProcessMetricSampler(ProcessMetricSampler&&) noexcept;
    ProcessMetricSampler& operator=(ProcessMetricSampler&&) noexcept;
    [[nodiscard]] Result<ProcessMetrics> sample();
private:
    class NativeHandles;
    game::GameProcessIdentity identity_;
    std::optional<CpuTimes> previous_;
    std::optional<std::uint64_t> previous_thread_sample_ms_;
    std::optional<std::uint64_t> previous_thread_refresh_ms_;
    std::unique_ptr<NativeHandles> native_handles_;
    detail::ThreadPressureCache thread_pressure_cache_;
    bool cpu_capacity_sampled_{false};
    std::optional<std::uint32_t> cached_affinity_logical_processors_;
    std::optional<std::uint32_t> cached_affinity_physical_cores_;
    std::optional<std::uint32_t> cached_system_logical_processors_;
};
}  // namespace kf2::telemetry
