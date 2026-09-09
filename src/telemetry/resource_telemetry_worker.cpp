#include "kf2/telemetry/resource_telemetry_worker.hpp"

#include <Windows.h>

#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

#include "kf2/game/game_log_locator.hpp"
#include "kf2/game/game_log_session.hpp"

namespace kf2::telemetry {
namespace {

bool same_binding(const ResourceTelemetryBinding& left,
                  const ResourceTelemetryBinding& right) noexcept {
    return left.identity.pid == right.identity.pid &&
        left.identity.process_start_id == right.identity.process_start_id &&
        left.adapter_luid == right.adapter_luid &&
        left.adapter_name == right.adapter_name &&
        left.adapter_vendor_id == right.adapter_vendor_id &&
        left.game_log_directory == right.game_log_directory;
}

bool same_session(const ResourceTelemetryBinding& left,
                  const ResourceTelemetryBinding& right) noexcept {
    return left.identity.pid == right.identity.pid &&
        left.identity.process_start_id == right.identity.process_start_id &&
        left.game_log_directory == right.game_log_directory;
}

class NativeGameLogSampler final {
public:
    explicit NativeGameLogSampler(ResourceTelemetryBinding binding)
        : binding_{std::move(binding)} {}

    std::optional<GameLogChunk> sample() {
        if (binding_.game_log_directory.empty()) return std::nullopt;
        bool reset_parser = false;
        if (!bound_ || path_.empty()) {
            const auto selected = game::find_active_game_log(
                binding_.game_log_directory,
                binding_.identity.process_start_id);
            if (!selected.has_value() || !selected.value()) return std::nullopt;
            path_ = selected.value()->path;
        }

        HANDLE file = CreateFileW(path_.c_str(),
            FILE_READ_ATTRIBUTES | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            reset_binding();
            return std::nullopt;
        }
        BY_HANDLE_FILE_INFORMATION information{};
        const bool inspected = GetFileInformationByHandle(file, &information) &&
            (information.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
        CloseHandle(file);
        if (!inspected) {
            reset_binding();
            return std::nullopt;
        }
        const auto size =
            (static_cast<std::uintmax_t>(information.nFileSizeHigh) << 32U) |
            information.nFileSizeLow;
        const auto last_write =
            (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime)
             << 32U) |
            information.ftLastWriteTime.dwLowDateTime;
        const auto creation =
            (static_cast<std::uint64_t>(information.ftCreationTime.dwHighDateTime)
             << 32U) |
            information.ftCreationTime.dwLowDateTime;
        if (!game::game_log_belongs_to_process(
                last_write, binding_.identity.process_start_id)) {
            return std::nullopt;
        }
        const auto file_index =
            (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
            information.nFileIndexLow;
        if (bound_ &&
            (volume_serial_ != information.dwVolumeSerialNumber ||
             file_index_ != file_index)) {
            reset_binding();
            return std::nullopt;
        }
        if (!bound_) {
            bound_ = true;
            volume_serial_ = information.dwVolumeSerialNumber;
            file_index_ = file_index;
            offset_ = 0;
            reset_parser = true;
        }
        if (size < offset_) {
            offset_ = 0;
            reset_parser = true;
        }

        GameLogChunk chunk;
        chunk.identity = binding_.identity;
        chunk.reset_parser = reset_parser;
        chunk.creation_filetime = creation;
        if (size == offset_) {
            return reset_parser ? std::optional{std::move(chunk)}
                                : std::nullopt;
        }
        std::ifstream input(path_, std::ios::binary);
        if (!input) return std::nullopt;
        input.seekg(static_cast<std::streamoff>(offset_));
        constexpr std::uintmax_t kMaximumLogChunkBytes = 32 * 1024;
        const auto requested = static_cast<std::size_t>(
            (std::min)(size - offset_, kMaximumLogChunkBytes));
        chunk.bytes.assign(requested, '\0');
        input.read(chunk.bytes.data(), static_cast<std::streamsize>(requested));
        const auto received = static_cast<std::size_t>(input.gcount());
        if (received == 0) return reset_parser
            ? std::optional{std::move(chunk)} : std::nullopt;
        chunk.bytes.resize(received);
        offset_ += received;
        return chunk;
    }

private:
    void reset_binding() {
        path_.clear();
        offset_ = 0;
        volume_serial_ = 0;
        file_index_ = 0;
        bound_ = false;
    }

    ResourceTelemetryBinding binding_;
    std::filesystem::path path_;
    std::uintmax_t offset_{0};
    std::uint32_t volume_serial_{0};
    std::uint64_t file_index_{0};
    bool bound_{false};
};

class NativeResourceSamplers final {
public:
    explicit NativeResourceSamplers(const ResourceTelemetryBinding& binding)
        : binding_{binding}, process_{game::GameProcessIdentity{
              binding.identity.pid, binding.identity.process_start_id, {}}} {
        bind_gpu(binding);
    }

    void bind_gpu(const ResourceTelemetryBinding& binding) {
        binding_ = binding;
        gpu_.reset();
        nvidia_.reset();
        nvidia_source_.reset();
        if (binding.adapter_luid) {
            auto gpu = PdhGpuSampler::create(
                binding.identity.pid, *binding.adapter_luid);
            if (gpu.has_value()) gpu_.emplace(std::move(gpu.value()));
        }
        if (binding.adapter_vendor_id == 0x10DE &&
            !binding.adapter_name.empty()) {
            auto driver = NvidiaGpuSampler::create(binding.adapter_name);
            if (driver.has_value()) {
                nvidia_source_ = driver.value().source();
                nvidia_.emplace(std::move(driver.value()));
            }
        }
    }

    ResourceSampleBatch sample(const ResourceSampleRequest& request) {
        ResourceSampleBatch batch;
        batch.group = request.group;
        if (request.group == ResourceSampleGroup::process_and_memory) {
            auto process = process_.sample();
            if (process.has_value()) batch.process = std::move(process.value());
            auto memory = query_system_memory_metrics();
            if (memory.has_value()) batch.system_memory = memory.value();
            return batch;
        }

        batch.nvidia_source = nvidia_source_;
        if (nvidia_) {
            auto driver = nvidia_->sample();
            if (driver.has_value()) batch.driver_gpu_percent = driver.value();
        }
        if (gpu_) {
            auto gpu = gpu_->sample();
            if (gpu.has_value()) {
                batch.gpu = std::move(gpu.value());
                if (batch.gpu->process_adapter_luid &&
                    (!binding_.adapter_luid ||
                     *batch.gpu->process_adapter_luid !=
                         *binding_.adapter_luid)) {
                    const auto adapters = enumerate_gpu_adapters();
                    if (adapters.has_value()) {
                        batch.detected_process_adapter =
                            find_hardware_gpu_adapter_by_luid(
                                adapters.value(),
                                *batch.gpu->process_adapter_luid);
                    }
                }
            }
        }
        return batch;
    }

private:
    ResourceTelemetryBinding binding_;
    ProcessMetricSampler process_;
    std::optional<PdhGpuSampler> gpu_;
    std::optional<NvidiaGpuSampler> nvidia_;
    std::optional<NvidiaGpuSource> nvidia_source_;
};

}  // namespace

class ResourceTelemetryWorker::Impl final {
public:
    explicit Impl(ResourceSampleFunction sample)
        : custom_sample_{std::move(sample)},
          thread_{[this](std::stop_token stop) { run(stop); }} {}

    ~Impl() { stop(); }

    std::uint64_t bind(ResourceTelemetryBinding binding) {
        std::scoped_lock lock{mutex_};
        if (binding_ && same_binding(*binding_, binding)) return generation_;
        const bool preserve_log = binding_ && same_session(*binding_, binding);
        ++generation_;
        binding_ = std::move(binding);
        pending_ = false;
        next_group_ = ResourceSampleGroup::process_and_memory;
        current_ = {};
        published_.reset();
        if (!preserve_log) log_chunks_.clear();
        condition_.notify_all();
        return generation_;
    }

    std::uint64_t invalidate_samples() {
        std::scoped_lock lock{mutex_};
        if (!binding_ || stopped_) return 0;
        ++generation_;
        pending_ = false;
        next_group_ = ResourceSampleGroup::process_and_memory;
        current_ = {};
        published_.reset();
        condition_.notify_all();
        return generation_;
    }

    void clear() {
        std::scoped_lock lock{mutex_};
        ++generation_;
        binding_.reset();
        pending_ = false;
        next_group_ = ResourceSampleGroup::process_and_memory;
        current_ = {};
        published_.reset();
        log_chunks_.clear();
        condition_.notify_all();
    }

    void request(std::uint64_t sampled_at_ns) {
        std::scoped_lock lock{mutex_};
        if (!binding_ || stopped_) return;
        pending_at_ns_ = sampled_at_ns;
        pending_ = true;
        condition_.notify_one();
    }

    std::shared_ptr<const ResourceTelemetrySnapshot> latest() const {
        std::scoped_lock lock{mutex_};
        return published_;
    }

    std::vector<GameLogChunk> take_game_log_chunks(SampleIdentity identity) {
        std::scoped_lock lock{mutex_};
        std::vector<GameLogChunk> result;
        while (!log_chunks_.empty()) {
            auto chunk = std::move(log_chunks_.front());
            log_chunks_.pop_front();
            if (chunk.identity.pid == identity.pid &&
                chunk.identity.process_start_id == identity.process_start_id) {
                result.push_back(std::move(chunk));
            }
        }
        return result;
    }

    bool wait_until_idle(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return condition_.wait_for(lock, timeout, [this] {
            return !pending_ && !active_;
        });
    }

    void stop() {
        {
            std::scoped_lock lock{mutex_};
            if (stopped_) return;
            stopped_ = true;
            pending_ = false;
            thread_.request_stop();
            condition_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    void run(std::stop_token stop) {
        static_cast<void>(SetThreadPriority(
            GetCurrentThread(), THREAD_PRIORITY_NORMAL));
        std::optional<ResourceTelemetryBinding> sampler_binding;
        std::unique_ptr<NativeResourceSamplers> native;
        std::optional<ResourceTelemetryBinding> log_binding;
        std::unique_ptr<NativeGameLogSampler> log_sampler;
        game::GameLogSessionParser log_parser;
        while (!stop.stop_requested()) {
            ResourceSampleRequest request;
            std::uint64_t request_generation = 0;
            {
                std::unique_lock lock{mutex_};
                if (!condition_.wait(lock, stop, [this] {
                        return pending_;
                    })) {
                    break;
                }
                if (!binding_) continue;
                request.binding = *binding_;
                request.group = next_group_;
                request.sampled_at_ns = pending_at_ns_;
                request_generation = generation_;
                pending_ = false;
                active_ = true;
                next_group_ = next_group_ ==
                        ResourceSampleGroup::process_and_memory
                    ? ResourceSampleGroup::gpu
                    : ResourceSampleGroup::process_and_memory;
            }

            ResourceSampleBatch batch;
            batch.group = request.group;
            std::optional<GameLogChunk> log_chunk;
            try {
                if (custom_sample_) {
                    batch = custom_sample_(request, stop);
                } else {
                    if (!log_sampler || !log_binding ||
                        !same_session(*log_binding, request.binding)) {
                        log_binding = request.binding;
                        log_sampler = std::make_unique<NativeGameLogSampler>(
                            request.binding);
                        log_parser.reset();
                    }
                    bool log_queue_has_room = false;
                    {
                        std::scoped_lock lock{mutex_};
                        log_queue_has_room = log_chunks_.size() < 8;
                    }
                    if (log_queue_has_room) {
                        log_chunk = log_sampler->sample();
                        if (log_chunk) {
                            if (log_chunk->reset_parser) log_parser.reset();
                            if (!log_chunk->bytes.empty()) {
                                log_chunk->parsed_session = log_parser.feed(
                                    log_chunk->bytes, request.sampled_at_ns);
                            }
                            log_chunk->parser_stats = log_parser.stats();
                        } else if (const auto expired =
                                       log_parser.expire_observations(
                                           request.sampled_at_ns)) {
                            GameLogChunk expiration;
                            expiration.identity = request.binding.identity;
                            expiration.observations_expired = true;
                            expiration.parsed_session = std::move(expired);
                            expiration.parser_stats = log_parser.stats();
                            log_chunk = std::move(expiration);
                        }
                    }
                    if (!native || !sampler_binding ||
                        !same_session(*sampler_binding, request.binding)) {
                        native = std::make_unique<NativeResourceSamplers>(
                            request.binding);
                    } else if (!same_binding(
                                   *sampler_binding, request.binding)) {
                        native->bind_gpu(request.binding);
                    }
                    sampler_binding = request.binding;
                    batch = native->sample(request);
                }
            } catch (const std::exception&) {
                // A failed desktop provider invalidates only this group. The
                // next request retries on the same single worker.
                batch = {};
            } catch (...) {
                batch = {};
            }
            batch.group = request.group;

            std::scoped_lock lock{mutex_};
            active_ = false;
            if (log_chunk && binding_ &&
                same_session(*binding_, request.binding)) {
                log_chunks_.push_back(std::move(*log_chunk));
            }
            if (stop.stop_requested() || !binding_ ||
                generation_ != request_generation ||
                !same_binding(*binding_, request.binding)) {
                condition_.notify_all();
                continue;
            }
            if (current_.generation != request_generation) {
                current_ = {};
                current_.generation = request_generation;
                current_.identity = request.binding.identity;
                current_.adapter_luid = request.binding.adapter_luid;
            }
            if (batch.group == ResourceSampleGroup::process_and_memory) {
                current_.process_sampled_at_ns = request.sampled_at_ns;
                current_.process = std::move(batch.process);
                current_.system_memory = std::move(batch.system_memory);
            } else {
                current_.gpu_sampled_at_ns = request.sampled_at_ns;
                current_.gpu = std::move(batch.gpu);
                current_.driver_gpu_percent = batch.driver_gpu_percent;
                current_.nvidia_source = batch.nvidia_source;
                current_.detected_process_adapter =
                    std::move(batch.detected_process_adapter);
            }
            current_.publication_sequence = ++publication_sequence_;
            published_ = std::make_shared<const ResourceTelemetrySnapshot>(
                current_);
            condition_.notify_all();
        }
    }

    ResourceSampleFunction custom_sample_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::jthread thread_;
    bool stopped_{false};
    bool pending_{false};
    bool active_{false};
    std::uint64_t pending_at_ns_{0};
    std::uint64_t generation_{0};
    std::uint64_t publication_sequence_{0};
    ResourceSampleGroup next_group_{ResourceSampleGroup::process_and_memory};
    std::optional<ResourceTelemetryBinding> binding_;
    ResourceTelemetrySnapshot current_;
    std::shared_ptr<const ResourceTelemetrySnapshot> published_;
    std::deque<GameLogChunk> log_chunks_;
};

ResourceTelemetryWorker::ResourceTelemetryWorker()
    : implementation_{std::make_unique<Impl>(ResourceSampleFunction{})} {}

ResourceTelemetryWorker::ResourceTelemetryWorker(ResourceSampleFunction sample)
    : implementation_{std::make_unique<Impl>(std::move(sample))} {}

ResourceTelemetryWorker::~ResourceTelemetryWorker() = default;

std::uint64_t ResourceTelemetryWorker::bind(
    ResourceTelemetryBinding binding) {
    return implementation_->bind(std::move(binding));
}

std::uint64_t ResourceTelemetryWorker::invalidate_samples() {
    return implementation_->invalidate_samples();
}

void ResourceTelemetryWorker::clear() { implementation_->clear(); }

void ResourceTelemetryWorker::request(std::uint64_t sampled_at_ns) {
    implementation_->request(sampled_at_ns);
}

std::shared_ptr<const ResourceTelemetrySnapshot>
ResourceTelemetryWorker::latest() const {
    return implementation_->latest();
}

std::vector<GameLogChunk> ResourceTelemetryWorker::take_game_log_chunks(
    SampleIdentity identity) {
    return implementation_->take_game_log_chunks(identity);
}

bool ResourceTelemetryWorker::wait_until_idle(
    std::chrono::milliseconds timeout) {
    return implementation_->wait_until_idle(timeout);
}

void ResourceTelemetryWorker::stop() { implementation_->stop(); }

}  // namespace kf2::telemetry
