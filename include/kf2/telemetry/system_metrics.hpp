#pragma once
#include <cstdint>
#include <optional>
#include <unordered_map>
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
class ProcessMetricSampler final {
public:
    explicit ProcessMetricSampler(game::GameProcessIdentity identity);
    [[nodiscard]] Result<ProcessMetrics> sample();
private:
    game::GameProcessIdentity identity_;
    std::optional<CpuTimes> previous_;
    std::optional<std::uint64_t> previous_thread_sample_ms_;
    std::unordered_map<std::uint32_t, std::uint64_t> previous_thread_ticks_;
    std::optional<double> cached_critical_core_percent_;
    std::optional<double> cached_effective_core_usage_;
    std::optional<double> cached_dominant_thread_share_percent_;
    std::optional<std::uint32_t> cached_active_cpu_threads_;
};
}  // namespace kf2::telemetry
