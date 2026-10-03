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

#include "kf2/game/game_log_session.hpp"
#include "kf2/game/video_settings.hpp"
#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::telemetry {

struct GameLogBoundaryEvents final {
    std::optional<game::GameMenuGraphicsReadback> graphics_readback;
    std::optional<std::wstring> map_prewarm_selection;
    bool load_map_started{false};
    bool new_settings_restart_requested{false};
    bool startup_ready{false};
    bool verified_engine_exit{false};
};

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
    std::shared_ptr<const game::GameProcessHandle> native_process;
};

struct GameLogChunk final {
    SampleIdentity identity;
    bool reset_parser{false};
    bool observations_expired{false};
    std::uint64_t creation_filetime{0};
    std::string bytes;
    GameLogBoundaryEvents boundaries;
    game::GameLogSessionSnapshot parsed_session;
    game::GameLogParserStats parser_stats;
};

struct ResourceSampleRequest final {
    ResourceTelemetryBinding binding;
    ResourceSampleGroup group{ResourceSampleGroup::process_and_memory};
    std::uint64_t sampled_at_ns{0};
};

// Published only when a provider is constructed or retried. Sharing this
// immutable report avoids copying error strings on every resource sample.
struct GpuProviderStatus final {
    std::uint64_t pdh_attempts{0};
    std::optional<Error> pdh_error;
    std::uint64_t nvidia_attempts{0};
    std::optional<Error> nvidia_error;
};

struct ResourceSampleBatch final {
    ResourceSampleGroup group{ResourceSampleGroup::process_and_memory};
    std::optional<ProcessMetrics> process;
    std::optional<SystemMemoryMetrics> system_memory;
    std::optional<GpuMetrics> gpu;
    std::optional<double> driver_gpu_percent;
    std::optional<NvidiaGpuSource> nvidia_source;
    std::optional<GpuAdapter> detected_process_adapter;
    std::shared_ptr<const GpuProviderStatus> gpu_provider_status;
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
    std::shared_ptr<const GpuProviderStatus> gpu_provider_status;
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

#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
namespace detail {
using GameLogReadHook = void (*)(const std::filesystem::path&);
using ResourceRequestHook = void (*)(ResourceTelemetryWorker&);
void set_resource_request_hook_for_testing(ResourceRequestHook hook) noexcept;
[[nodiscard]] std::uint64_t resource_requests_for_testing() noexcept;
void set_game_log_read_hook_for_testing(GameLogReadHook hook) noexcept;
void fail_next_resource_telemetry_publication() noexcept;
}
#endif

}  // namespace kf2::telemetry
