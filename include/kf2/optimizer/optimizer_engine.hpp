#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kf2/config/config_preview.hpp"

namespace kf2::optimizer {

enum class QualityPolicy { exact, invisible, performance };
enum class Profile { balanced, stability, high_performance, custom };
enum class Bottleneck {
    unavailable,
    balanced,
    gpu,
    cpu,
    ram_pressure,
    vram_streaming,
    frame_cap,
};
enum class Confidence { unavailable, low, medium, high };

struct PerformanceEvidence {
    bool fresh{false};
    std::optional<double> fps;
    std::optional<double> p95_frame_time_ms;
    std::optional<double> cpu_percent;
    std::optional<double> system_cpu_percent;
    std::optional<double> critical_core_percent;
    std::optional<double> effective_core_usage;
    std::optional<double> dominant_thread_share_percent;
    std::optional<std::uint32_t> active_cpu_threads;
    std::optional<std::uint32_t> affinity_logical_processors;
    std::optional<std::uint32_t> affinity_physical_cores;
    std::optional<std::uint32_t> system_logical_processors;
    // KF2-attributed GPU engine occupancy. gpu_percent remains the whole
    // physical adapter load used to detect contention from any process.
    std::optional<double> process_gpu_percent;
    std::optional<double> gpu_percent;
    std::optional<std::uint64_t> dedicated_vram_bytes;
    std::optional<std::uint64_t> dedicated_vram_budget_bytes;
    std::optional<std::uint64_t> adapter_vram_used_bytes;
    std::optional<std::uint64_t> adapter_vram_budget_bytes;
    std::optional<std::uint64_t> system_ram_used_bytes;
    std::optional<std::uint64_t> system_ram_budget_bytes;
    std::optional<std::uint64_t> system_commit_used_bytes;
    std::optional<std::uint64_t> system_commit_budget_bytes;
    std::optional<std::uint64_t> process_private_bytes;
};

struct OptimizerInput {
    int target_fps{60};
    QualityPolicy quality{QualityPolicy::exact};
    Profile profile{Profile::balanced};
    bool profile_preview_requested{false};
    PerformanceEvidence evidence;
};

struct OptimizerDecision {
    Bottleneck bottleneck{Bottleneck::unavailable};
    Confidence confidence{Confidence::unavailable};
    std::wstring reason;
    std::vector<config::RequestedChange> changes;
};

struct StartupMemoryProfile {
    int texture_pool_size_mb{160};
    int memory_margin_mb{20};
    int streaming_hysteresis_limit{20};
};

[[nodiscard]] std::optional<StartupMemoryProfile>
recommended_startup_memory_profile(
    std::uint64_t dedicated_vram_bytes) noexcept;

[[nodiscard]] OptimizerDecision evaluate(const OptimizerInput& input);

}  // namespace kf2::optimizer
