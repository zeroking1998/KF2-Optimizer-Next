#pragma once
#include <Windows.h>
#include <pdh.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "kf2/core/result.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry {
struct GpuInstanceIdentity {
    std::uint32_t pid{0};
    std::uint64_t adapter_luid{0};
    std::wstring engine;
    std::uint32_t physical_index{0xFFFFFFFFU};
    std::uint32_t engine_index{0xFFFFFFFFU};
};
struct GpuCounterValue {
    GpuInstanceIdentity identity;
    double utilization_percent{0};
    std::uint64_t dedicated_bytes{0};
    std::uint64_t shared_bytes{0};
};
struct GpuMetrics {
    // Process-bound load retained for attribution and diagnostics.
    std::optional<double> gpu_percent;
    // Whole physical adapter load, comparable to Task Manager-style totals.
    std::optional<double> adapter_gpu_percent;
    std::uint64_t dedicated_bytes{0};
    std::uint64_t shared_bytes{0};
    std::optional<std::uint64_t> adapter_local_usage_bytes;
    std::optional<std::uint64_t> adapter_local_budget_bytes;
    // Adapter that currently carries this process's busiest GPU engine.
    std::optional<std::uint64_t> process_adapter_luid;
    SampleQuality quality{SampleQuality::unavailable};
    UnavailableReason reason{UnavailableReason::no_samples};
};
struct GpuMemoryBudget final {
    std::uint64_t current_usage_bytes{0};
    std::uint64_t budget_bytes{0};
    std::uint64_t available_for_reservation_bytes{0};
};
struct GpuAdapter {
    std::uint64_t luid{0};
    std::wstring name;
    std::uint64_t dedicated_memory_bytes{0};
    std::uint32_t vendor_id{0};
    std::uint32_t device_id{0};
    std::optional<std::uint64_t> umd_driver_version;
    bool software{false};
    // Several DXGI LUIDs can represent display paths backed by the same
    // physical device. This PnP key stays equal for those projections while
    // remaining different for two genuinely separate, identical GPUs.
    std::wstring physical_device_key;
};
enum class ProcessGpuPreference {
    unspecified,
    minimum_power,
    high_performance,
};
struct ConfiguredGpuAdapter {
    ProcessGpuPreference preference{ProcessGpuPreference::unspecified};
    std::optional<std::uint64_t> adapter_luid;
};
// Missing GPU selection in a valid Windows option list is unspecified;
// malformed or duplicate GPU selections are rejected.
[[nodiscard]] std::optional<ProcessGpuPreference>
parse_windows_gpu_preference(std::wstring_view value) noexcept;
[[nodiscard]] std::string_view process_gpu_preference_token(
    ProcessGpuPreference preference) noexcept;
[[nodiscard]] Result<ConfiguredGpuAdapter> configured_gpu_adapter_for_process(
    const std::filesystem::path& executable);
[[nodiscard]] Result<std::vector<GpuAdapter>> enumerate_gpu_adapters();
[[nodiscard]] Result<GpuMemoryBudget> query_gpu_memory_budget(
    std::uint64_t adapter_luid);
[[nodiscard]] std::vector<GpuAdapter> unique_physical_gpu_adapters(
    const std::vector<GpuAdapter>& adapters);
[[nodiscard]] std::optional<GpuAdapter> find_hardware_gpu_adapter_by_luid(
    const std::vector<GpuAdapter>& adapters, std::uint64_t adapter_luid);
[[nodiscard]] std::optional<GpuAdapter> find_unique_hardware_gpu_adapter_by_name(
    const std::vector<GpuAdapter>& adapters, std::wstring_view adapter_name);
[[nodiscard]] std::optional<std::uint64_t> active_process_gpu_adapter_luid(
    const std::vector<GpuCounterValue>& values, std::uint32_t pid,
    std::uint64_t preferred_adapter_luid = 0);
[[nodiscard]] std::wstring format_gpu_driver_version(std::uint64_t version);
[[nodiscard]] Result<std::uint64_t> adapter_luid_for_window(HWND window);
[[nodiscard]] std::optional<GpuInstanceIdentity> parse_gpu_instance(
    std::wstring_view instance);
[[nodiscard]] GpuMetrics aggregate_gpu_counters(
    const std::vector<GpuCounterValue>& values, std::uint32_t pid,
    std::uint64_t adapter_luid);
[[nodiscard]] std::optional<double> aggregate_adapter_gpu_percent(
    const std::vector<GpuCounterValue>& values, std::uint64_t adapter_luid);
[[nodiscard]] std::optional<double> choose_total_gpu_percent(
    std::optional<double> driver_gpu_percent,
    std::optional<double> adapter_gpu_percent);

struct GpuUtilizationObservation final {
    std::uint64_t timestamp_ns{0};
    std::uint64_t adapter_luid{0};
    std::optional<double> process_percent;
    std::optional<double> adapter_percent;
};

struct GpuUtilizationEstimate final {
    std::uint64_t adapter_luid{0};
    std::optional<double> process_percent;
    std::optional<double> adapter_percent;
    std::uint64_t sample_age_ns{0};
    std::uint32_t continuity_samples{0};
    double confidence{0.0};
    bool decision_ready{false};
};

// Keeps Adaptive independent from vendor-specific counter cadence. Abrupt edge
// readings require a second agreeing observation, while a confirmed value may
// bridge one short dropout. A physical-adapter change always clears history.
class GpuUtilizationFilter final {
public:
    [[nodiscard]] GpuUtilizationEstimate update(
        const GpuUtilizationObservation& observation) noexcept;
    void reset() noexcept;

private:
    struct ChannelState final {
        std::optional<double> confirmed;
        std::optional<double> pending;
        std::uint64_t confirmed_at_ns{0};
        std::uint32_t pending_samples{0};
        std::uint32_t continuity_samples{0};
    };

    std::uint64_t adapter_luid_{0};
    std::uint64_t last_observation_ns_{0};
    ChannelState process_;
    ChannelState adapter_;
};

enum class NvidiaGpuSource {
    nvapi_dynamic_pstates,
    nvml_utilization,
};

class NvidiaGpuSampler final {
public:
    NvidiaGpuSampler(const NvidiaGpuSampler&) = delete;
    NvidiaGpuSampler& operator=(const NvidiaGpuSampler&) = delete;
    NvidiaGpuSampler(NvidiaGpuSampler&&) noexcept;
    NvidiaGpuSampler& operator=(NvidiaGpuSampler&&) noexcept;
    ~NvidiaGpuSampler();

    [[nodiscard]] static Result<NvidiaGpuSampler> create(
        std::wstring_view adapter_name);
    [[nodiscard]] Result<double> sample() const;
    [[nodiscard]] NvidiaGpuSource source() const noexcept;

private:
    struct Impl;
    explicit NvidiaGpuSampler(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

class PdhGpuSampler final {
public:
    PdhGpuSampler(const PdhGpuSampler&) = delete;
    PdhGpuSampler& operator=(const PdhGpuSampler&) = delete;
    PdhGpuSampler(PdhGpuSampler&& other) noexcept;
    PdhGpuSampler& operator=(PdhGpuSampler&& other) noexcept;
    ~PdhGpuSampler();
    [[nodiscard]] static Result<PdhGpuSampler> create(std::uint32_t pid,
                                                       std::uint64_t adapter_luid);
    [[nodiscard]] Result<GpuMetrics> sample();
private:
    PdhGpuSampler() = default;
    PDH_HQUERY query_{};
    PDH_HCOUNTER utilization_{};
    PDH_HCOUNTER dedicated_{};
    PDH_HCOUNTER shared_{};
    std::uint32_t pid_{0};
    std::uint64_t adapter_luid_{0};
    bool warmed_{false};
};
}  // namespace kf2::telemetry
