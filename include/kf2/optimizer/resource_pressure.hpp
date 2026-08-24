#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace kf2::optimizer {

enum class ResourceKind : std::uint8_t { cpu, gpu, vram, ram, unknown };
enum class PressureTrend : std::uint8_t { unknown, falling, stable, rising };

struct ResourcePressureSignal final {
    double raw{0.0};
    double smoothed{0.0};
    double confidence{0.0};
    PressureTrend trend{PressureTrend::unknown};
    std::optional<double> reserve_bytes;
};

struct ResourcePressureInput final {
    std::uint64_t timestamp_ns{0};
    int target_fps{60};
    std::optional<double> frame_time_ms;
    std::optional<double> p95_frame_time_ms;
    std::optional<double> process_cpu_percent;
    std::optional<double> system_cpu_percent;
    std::optional<double> critical_thread_percent;
    std::optional<double> effective_core_usage;
    std::optional<std::uint32_t> affinity_logical_processors;
    std::optional<double> process_gpu_percent;
    std::optional<double> adapter_gpu_percent;
    std::optional<double> vram_used_bytes;
    std::optional<double> vram_budget_bytes;
    std::optional<double> ram_used_bytes;
    std::optional<double> ram_budget_bytes;
    std::optional<double> commit_used_bytes;
    std::optional<double> commit_budget_bytes;
    std::optional<double> process_private_bytes;
    std::optional<double> paging_pressure;
    bool discontinuity{false};
};

struct ResourcePressureSnapshot final {
    ResourcePressureSignal cpu;
    ResourcePressureSignal gpu;
    ResourcePressureSignal vram;
    ResourcePressureSignal ram;
    ResourceKind primary{ResourceKind::unknown};
    double primary_confidence{0.0};
    double total{0.0};
    double headroom{0.0};
    double frame_budget_deficit_ms{0.0};
    double predicted_deficit_ms{0.0};
    bool recovery_safe{false};
    bool shared_cpu_pressure{false};
    bool shared_gpu_pressure{false};
};

class ResourcePressureEstimator final {
public:
    [[nodiscard]] ResourcePressureSnapshot evaluate(
        const ResourcePressureInput& input) noexcept;
    void reset() noexcept;

private:
    struct State final {
        bool initialized{false};
        double smoothed{0.0};
        double previous_raw{0.0};
    };

    std::array<State, 4> states_{};
    std::optional<double> previous_p95_ms_;
    std::uint64_t previous_timestamp_ns_{0};
};

[[nodiscard]] const char* resource_kind_name(ResourceKind kind) noexcept;
[[nodiscard]] const char* pressure_trend_name(PressureTrend trend) noexcept;

}  // namespace kf2::optimizer
