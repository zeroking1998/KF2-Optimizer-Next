#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry {

enum class ResourceSampleGroup {
    process_and_memory,
    gpu,
};

struct ResourceTelemetryBinding final {
    SampleIdentity identity;
    std::optional<std::uint64_t> adapter_luid;
    std::wstring adapter_name;
    std::uint32_t adapter_vendor_id{0};
    std::filesystem::path game_log_directory;
};

struct GameLogChunk final {
    SampleIdentity identity;
    bool reset_parser{false};
    std::uint64_t creation_filetime{0};
    std::string bytes;
};

struct ResourceSampleRequest final {
    ResourceTelemetryBinding binding;
    ResourceSampleGroup group{ResourceSampleGroup::process_and_memory};
    std::uint64_t sampled_at_ns{0};
};

struct ResourceSampleBatch final {
    ResourceSampleGroup group{ResourceSampleGroup::process_and_memory};
    std::optional<ProcessMetrics> process;
    std::optional<SystemMemoryMetrics> system_memory;
    std::optional<GpuMetrics> gpu;
    std::optional<double> driver_gpu_percent;
    std::optional<NvidiaGpuSource> nvidia_source;
    std::optional<GpuAdapter> detected_process_adapter;
};

struct ResourceTelemetrySnapshot final {
    std::uint64_t generation{0};
    std::uint64_t publication_sequence{0};
    SampleIdentity identity;
    std::optional<std::uint64_t> adapter_luid;
    std::uint64_t process_sampled_at_ns{0};
    std::uint64_t gpu_sampled_at_ns{0};
    std::optional<ProcessMetrics> process;
    std::optional<SystemMemoryMetrics> system_memory;
    std::optional<GpuMetrics> gpu;
    std::optional<double> driver_gpu_percent;
    std::optional<NvidiaGpuSource> nvidia_source;
    std::optional<GpuAdapter> detected_process_adapter;
};

using ResourceSampleFunction = std::function<ResourceSampleBatch(
    const ResourceSampleRequest&, std::stop_token)>;

// Owns the only desktop resource-sampling thread. UI callers only submit a
// timestamp and consume immutable snapshots; a process or adapter rebind
// invalidates every in-flight result from the previous generation.
class ResourceTelemetryWorker final {
public:
    ResourceTelemetryWorker();
    explicit ResourceTelemetryWorker(ResourceSampleFunction sample);
    ~ResourceTelemetryWorker();

    ResourceTelemetryWorker(const ResourceTelemetryWorker&) = delete;
    ResourceTelemetryWorker& operator=(const ResourceTelemetryWorker&) = delete;
    ResourceTelemetryWorker(ResourceTelemetryWorker&&) = delete;
    ResourceTelemetryWorker& operator=(ResourceTelemetryWorker&&) = delete;

    [[nodiscard]] std::uint64_t bind(ResourceTelemetryBinding binding);
    [[nodiscard]] std::uint64_t invalidate_samples();
    void clear();
    void request(std::uint64_t sampled_at_ns);
    [[nodiscard]] std::shared_ptr<const ResourceTelemetrySnapshot> latest()
        const;
    [[nodiscard]] std::vector<GameLogChunk> take_game_log_chunks(
        SampleIdentity identity);
    [[nodiscard]] bool wait_until_idle(std::chrono::milliseconds timeout);
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::telemetry
