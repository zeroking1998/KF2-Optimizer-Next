#include "kf2/telemetry/resource_telemetry_worker.hpp"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "kf2/game/game_log_locator.hpp"
#include "kf2/game/game_log_session.hpp"
#include "kf2/game/startup_prewarmer.hpp"

namespace kf2::telemetry {
namespace {

#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
std::atomic_bool fail_next_telemetry_publication{false};
kf2::telemetry::detail::GameLogReadHook game_log_read_hook{nullptr};
std::atomic_uint64_t resource_requests{0};
std::atomic_uint64_t game_log_handle_opens{0};
std::atomic_uint64_t game_log_handle_closes{0};
kf2::telemetry::detail::ResourceRequestHook resource_request_hook{nullptr};
std::mutex game_log_trace_mutex;
detail::GameLogTestTrace game_log_test_trace;

struct TraceGameLogRead final {
    detail::GameLogReadTrace value;
    ~TraceGameLogRead() {
        std::scoped_lock lock{game_log_trace_mutex};
        game_log_test_trace.read = value;
    }
};

void record_game_log_publication(const detail::GameLogTestTrace& trace,
                                std::string_view stage) {
    std::scoped_lock lock{game_log_trace_mutex};
    if (stage == "sampling") game_log_test_trace.read = {};
    game_log_test_trace.publication_stage = stage;
    game_log_test_trace.request_generation = trace.request_generation;
    game_log_test_trace.current_generation = trace.current_generation;
    game_log_test_trace.chunk_queued = trace.chunk_queued;
    game_log_test_trace.sample_exception = trace.sample_exception;
}
#endif

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

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    ~UniqueHandle() { reset(); }

    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            ++game_log_handle_closes;
#endif
        }
        handle_ = handle;
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        if (handle_ != INVALID_HANDLE_VALUE) ++game_log_handle_opens;
#endif
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class GameLogBoundaryExtractor final {
public:
    GameLogBoundaryEvents feed(std::string_view bytes,
                               bool verified_log_identity) {
        GameLogBoundaryEvents events;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto newline = bytes.find('\n', offset);
            const auto end = newline == std::string_view::npos
                ? bytes.size() : newline;
            const auto segment = bytes.substr(offset, end - offset);
            if (dropping_oversized_line_) {
                if (newline == std::string_view::npos) break;
                dropping_oversized_line_ = false;
                offset = newline + 1;
                continue;
            }
            if (segment.size() > kMaximumBoundaryLineBytes - pending_.size()) {
                pending_.clear();
                if (newline == std::string_view::npos) {
                    dropping_oversized_line_ = true;
                    break;
                }
            } else {
                pending_.append(segment);
                if (newline == std::string_view::npos) break;
                if (!pending_.empty() && pending_.back() == '\r') {
                    pending_.pop_back();
                }
                consume_line(pending_, verified_log_identity, events);
                pending_.clear();
            }
            offset = newline + 1;
        }
        return events;
    }

    void reset() noexcept {
        pending_.clear();
        dropping_oversized_line_ = false;
    }

private:
    static constexpr std::size_t kMaximumBoundaryLineBytes = 4096;

    static void consume_line(std::string_view line,
                             bool verified_log_identity,
                             GameLogBoundaryEvents& events) {
        if (const auto readback =
                game::parse_game_menu_graphics_readback(line)) {
            events.graphics_readback = *readback;
        }
        if (const auto selected_map =
                game::map_prewarm_request_from_log_line(line)) {
            events.map_prewarm_selection = *selected_map;
        }
        events.load_map_started = events.load_map_started ||
            line.find("Log: LoadMap: ") != std::string_view::npos;
        events.new_settings_restart_requested =
            events.new_settings_restart_requested ||
            game::game_log_requests_settings_restart(line);
        events.startup_ready = events.startup_ready ||
            line.find("WidgetInitialized - WidgetName:  StartMenu") !=
                std::string_view::npos;
        events.verified_engine_exit = events.verified_engine_exit ||
            (verified_log_identity && game::game_log_reports_engine_exit(line));
    }

    std::string pending_;
    bool dropping_oversized_line_{false};
};

class NativeGameLogSampler final {
public:
    explicit NativeGameLogSampler(ResourceTelemetryBinding binding)
        : binding_{std::move(binding)} {}

    std::optional<GameLogChunk> sample(std::uint64_t now_ns) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        TraceGameLogRead trace;
        trace.value.process_start_filetime = binding_.identity.process_start_id;
        trace.value.stage = "discovery";
#endif
        if (binding_.game_log_directory.empty()) return std::nullopt;
        bool reset_parser = false;
        if (file_.get() == INVALID_HANDLE_VALUE) {
            const auto selected = game::find_active_game_log(
                binding_.game_log_directory,
                binding_.identity.process_start_id);
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            if (!selected.has_value()) {
                trace.value.last_windows_error = selected.error().native_code;
            }
#endif
            if (!selected.has_value() || !selected.value()) return std::nullopt;
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            trace.value.stage = "open";
            trace.value.last_write_filetime = selected.value()->last_write_filetime;
#endif
            path_ = selected.value()->path;
            file_.reset(CreateFileW(path_.c_str(),
                FILE_READ_ATTRIBUTES | GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr));
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            if (file_.get() == INVALID_HANDLE_VALUE) {
                trace.value.last_windows_error = GetLastError();
            }
#endif
            if (file_.get() == INVALID_HANDLE_VALUE) return reject_binding();
            // Allocate once per binding, with room for the volume-relative
            // name. Never allocate or reopen an unchanged file on idle polls.
            name_buffer_.resize(sizeof(FILE_NAME_INFO) +
                (path_.native().size() + MAX_PATH) * sizeof(wchar_t));
        }

        BY_HANDLE_FILE_INFORMATION information{};
        FILE_STANDARD_INFO standard{};
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "inspection";
#endif
        const bool inspected =
            GetFileInformationByHandle(file_.get(), &information) &&
            (information.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
            information.nNumberOfLinks == 1 &&
            GetFileInformationByHandleEx(file_.get(), FileStandardInfo,
                &standard, sizeof(standard)) && !standard.DeletePending &&
            !standard.Directory && standard.NumberOfLinks == 1 &&
            GetFileInformationByHandleEx(file_.get(), FileNameInfo,
                name_buffer_.data(), static_cast<DWORD>(name_buffer_.size()));
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.attributes = information.dwFileAttributes;
        trace.value.links = information.nNumberOfLinks;
        trace.value.last_write_filetime =
            (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32) |
            information.ftLastWriteTime.dwLowDateTime;
        trace.value.file_size =
            (static_cast<std::uintmax_t>(information.nFileSizeHigh) << 32) |
            information.nFileSizeLow;
        // Raw last-error is meaningful only for a failed Windows API, not a
        // rejected metadata predicate. Keep the inspected fields alongside it.
        if (!inspected) trace.value.last_windows_error = GetLastError();
        if (inspected) trace.value.stage = "file_name";
#endif
        if (!inspected) return reject_binding();
        const auto* name = reinterpret_cast<const FILE_NAME_INFO*>(
            name_buffer_.data());
        if (name->FileNameLength == 0 ||
            name->FileNameLength % sizeof(wchar_t) != 0 ||
            name->FileNameLength >
                name_buffer_.size() - offsetof(FILE_NAME_INFO, FileName)) {
            return reject_binding();
        }
        const std::wstring_view current_name{
            name->FileName, name->FileNameLength / sizeof(wchar_t)};
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "retained_name";
#endif
        // File IDs are immutable on a retained handle. Detect rename/delete
        // through that same handle rather than continuing to tail an old file.
        if (bound_ && current_name != bound_name_) return reject_binding();
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
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "freshness";
#endif
        if (!game::game_log_belongs_to_process(
                last_write, binding_.identity.process_start_id)) {
            return reject_binding();
        }
        const auto file_index =
            (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
            information.nFileIndexLow;
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "file_identity";
#endif
        if (bound_ &&
            (volume_serial_ != information.dwVolumeSerialNumber ||
             file_index_ != file_index)) {
            return reject_binding();
        }
        if (!bound_) {
            bound_name_ = current_name;
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
        if (reset_parser) {
            catching_up_ = false;
            catch_up_started_ns_ = 0;
            caught_up_at_ns_ = 0;
        }

        GameLogChunk chunk;
        chunk.identity = binding_.identity;
        chunk.reset_parser = reset_parser;
        chunk.creation_filetime = creation;
        constexpr std::uintmax_t kNormalLogChunkBytes = 32 * 1024;
        constexpr std::uintmax_t kCatchUpLogChunkBytes = 512 * 1024;
        chunk.historical = reset_parser || catching_up_ ||
            now_ns < caught_up_at_ns_ ||
            now_ns - caught_up_at_ns_ > game::kGameLogObservationFreshnessNs;
        if (size == offset_) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            trace.value.stage = "end_of_file";
#endif
            caught_up_at_ns_ = now_ns;
            return reset_parser ? std::optional{std::move(chunk)}
                                : std::nullopt;
        }
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "seek";
        if (game_log_read_hook != nullptr) game_log_read_hook(path_);
#endif
        if (offset_ > static_cast<std::uintmax_t>(
                          (std::numeric_limits<LONGLONG>::max)())) {
            return reject_binding();
        }
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset_);
        if (!SetFilePointerEx(file_.get(), position, nullptr, FILE_BEGIN)) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            trace.value.last_windows_error = GetLastError();
#endif
            return reject_binding();
        }
        const auto requested = static_cast<std::size_t>(
            (std::min)(size - offset_, chunk.historical ||
                size - offset_ > kNormalLogChunkBytes
                ? kCatchUpLogChunkBytes : kNormalLogChunkBytes));
        chunk.bytes.assign(requested, '\0');
        DWORD received = 0;
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "read";
#endif
        if (!ReadFile(file_.get(), chunk.bytes.data(),
                      static_cast<DWORD>(requested), &received, nullptr)) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            trace.value.last_windows_error = GetLastError();
#endif
            return reject_binding();
        }
        if (received == 0) return reject_binding();
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.bytes_read = received;
        trace.value.stage = "post_read_size";
#endif
        chunk.bytes.resize(received);
        offset_ += received;
        // Inspect the same verified handle again: writers may have appended
        // during this bounded read. Never mistake the earlier EOF for live.
        LARGE_INTEGER latest_size{};
        if (!GetFileSizeEx(file_.get(), &latest_size) ||
            latest_size.QuadPart < 0 ||
            static_cast<std::uintmax_t>(latest_size.QuadPart) < offset_) {
            reset_binding();
            chunk.reset_parser = true;
            chunk.catching_up = true;
            chunk.historical = true;
            chunk.bytes.clear();
            return chunk;
        }
        chunk.parser_stats.backlog_bytes =
            static_cast<std::uintmax_t>(latest_size.QuadPart) - offset_;
        chunk.catching_up = chunk.parser_stats.backlog_bytes != 0;
        chunk.historical = chunk.historical || chunk.catching_up;
        if (chunk.catching_up) {
            if (!catching_up_ || now_ns < catch_up_started_ns_) {
                catch_up_started_ns_ = now_ns;
            }
            chunk.parser_stats.catch_up_age_ns = now_ns - catch_up_started_ns_;
        }
        catching_up_ = chunk.catching_up;
        if (!catching_up_) caught_up_at_ns_ = now_ns;
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
        trace.value.stage = "sampled";
#endif
        return chunk;
    }

private:
    std::optional<GameLogChunk> reject_binding() {
        const bool was_bound = bound_;
        reset_binding();
        if (!was_bound) return std::nullopt;
        GameLogChunk reset;
        reset.identity = binding_.identity;
        reset.reset_parser = true;
        reset.catching_up = true;
        return reset;
    }

    void reset_binding() {
        file_.reset();
        bound_name_.clear();
        path_.clear();
        offset_ = 0;
        volume_serial_ = 0;
        file_index_ = 0;
        bound_ = false;
        catching_up_ = false;
        catch_up_started_ns_ = 0;
        caught_up_at_ns_ = 0;
    }

    ResourceTelemetryBinding binding_;
    UniqueHandle file_;
    std::vector<std::byte> name_buffer_;
    std::wstring bound_name_;
    std::filesystem::path path_;
    std::uintmax_t offset_{0};
    std::uint32_t volume_serial_{0};
    std::uint64_t file_index_{0};
    bool bound_{false};
    bool catching_up_{false};
    std::uint64_t catch_up_started_ns_{0};
    std::uint64_t caught_up_at_ns_{0};
};

struct GpuProviderRetry final {
    std::uint64_t last_attempt_ns{0};
    std::uint64_t delay_ns{0};

    bool begin_attempt(std::uint64_t now_ns) noexcept {
        if (delay_ns != 0) {
            // Rebase a rolled-back clock without bypassing the backoff. Use
            // elapsed time rather than a deadline that could overflow.
            if (now_ns < last_attempt_ns) last_attempt_ns = now_ns;
            if (now_ns - last_attempt_ns < delay_ns) return false;
        }
        last_attempt_ns = now_ns;
        delay_ns = delay_ns == 0 ? 1'000'000'000ULL
            : (std::min)(delay_ns * 2, 30'000'000'000ULL);
        return true;
    }
};

class NativeResourceSamplers final {
public:
    explicit NativeResourceSamplers(const ResourceTelemetryBinding& binding)
        : binding_{binding}, process_{game::GameProcessIdentity{
              binding.identity.pid, binding.identity.process_start_id, {},
              binding.native_process}} {
        bind_gpu(binding);
    }

    void bind_gpu(const ResourceTelemetryBinding& binding) {
        binding_ = binding;
        gpu_.reset();
        nvidia_.reset();
        nvidia_source_.reset();
        pdh_retry_ = {};
        nvidia_retry_ = {};
        provider_status_.reset();
    }

    void retry_missing_gpu_providers(std::uint64_t now_ns) {
        const bool retry_pdh = !gpu_ && binding_.adapter_luid &&
            pdh_retry_.begin_attempt(now_ns);
        const bool retry_nvidia = !nvidia_ &&
            binding_.adapter_vendor_id == 0x10DE &&
            !binding_.adapter_name.empty() &&
            nvidia_retry_.begin_attempt(now_ns);
        if (!retry_pdh && !retry_nvidia) return;
        auto status = provider_status_ ? *provider_status_ : GpuProviderStatus{};
        if (retry_pdh) {
            ++status.pdh_attempts;
            auto gpu = PdhGpuSampler::create(
                binding_.identity.pid, *binding_.adapter_luid);
            if (gpu.has_value()) {
                gpu_.emplace(std::move(gpu.value()));
                status.pdh_error.reset();
            } else {
                status.pdh_error = std::move(gpu.error());
            }
        }
        if (retry_nvidia) {
            ++status.nvidia_attempts;
            auto driver = NvidiaGpuSampler::create(binding_.adapter_name);
            if (driver.has_value()) {
                nvidia_source_ = driver.value().source();
                nvidia_.emplace(std::move(driver.value()));
                status.nvidia_error.reset();
            } else {
                status.nvidia_error = std::move(driver.error());
            }
        }
        provider_status_ = std::make_shared<const GpuProviderStatus>(
            std::move(status));
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

        retry_missing_gpu_providers(request.sampled_at_ns);
        batch.gpu_provider_status = provider_status_;
        batch.nvidia_source = nvidia_source_;
        if (nvidia_) {
            auto driver = nvidia_->sample();
            if (driver.has_value()) batch.driver_gpu_percent = driver.value();
        }
        if (gpu_) {
            auto gpu = gpu_->sample();
            if (gpu.has_value()) {
                if (request.collect_own_gpu) {
                    batch.own_gpu_percent =
                        gpu_->latest_process_gpu_percent(GetCurrentProcessId());
                }
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
    GpuProviderRetry pdh_retry_;
    GpuProviderRetry nvidia_retry_;
    std::shared_ptr<const GpuProviderStatus> provider_status_;
};

}  // namespace

#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
detail::GameLogTestTrace detail::game_log_trace_for_testing() {
    std::scoped_lock lock{game_log_trace_mutex};
    return game_log_test_trace;
}

std::uint64_t detail::game_log_handle_opens_for_testing() noexcept {
    return game_log_handle_opens.load();
}

std::uint64_t detail::game_log_handle_closes_for_testing() noexcept {
    return game_log_handle_closes.load();
}

void detail::set_resource_request_hook_for_testing(
    ResourceRequestHook hook) noexcept {
    resource_request_hook = hook;
}

std::uint64_t detail::resource_requests_for_testing() noexcept {
    return resource_requests.load();
}

void detail::set_game_log_read_hook_for_testing(
    GameLogReadHook hook) noexcept {
    game_log_read_hook = hook;
}

void detail::fail_next_resource_telemetry_publication() noexcept {
    fail_next_telemetry_publication.store(true, std::memory_order_release);
}
#endif

class ResourceTelemetryWorker::Impl final {
public:
    explicit Impl(ResourceSampleFunction sample)
        : custom_sample_{std::move(sample)},
          thread_{[this](std::stop_token stop) { run(stop); }} {}

    ~Impl() { stop(); }
    std::optional<std::uint64_t> cpu_work_ns() noexcept {
        return thread_.joinable()
            ? query_thread_cpu_ns(thread_.native_handle()) : std::nullopt;
    }

    std::uint64_t bind(ResourceTelemetryBinding binding) {
        std::scoped_lock lock{mutex_};
        if (binding_ && same_binding(*binding_, binding)) return generation_;
        const bool preserve_log = binding_ && same_session(*binding_, binding);
        ++generation_;
        binding_ = std::move(binding);
        pending_ = false;
        next_group_ = ResourceSampleGroup::process_and_memory;
        published_.reset();
        if (!preserve_log) {
            log_chunks_.clear();
            reset_log_ = !stopped_ && !worker_failed_;
        }
        condition_.notify_all();
        return generation_;
    }

    std::uint64_t invalidate_samples() {
        std::scoped_lock lock{mutex_};
        if (!binding_ || stopped_) return 0;
        ++generation_;
        pending_ = false;
        next_group_ = ResourceSampleGroup::process_and_memory;
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
        published_.reset();
        log_chunks_.clear();
        reset_log_ = !stopped_ && !worker_failed_;
        condition_.notify_all();
    }

    void request(std::uint64_t sampled_at_ns) {
        std::scoped_lock lock{mutex_};
        if (!binding_ || stopped_ || worker_failed_) return;
        pending_at_ns_ = sampled_at_ns;
        pending_ = true;
        condition_.notify_one();
    }

    void set_own_gpu_enabled(bool enabled) {
        std::scoped_lock lock{mutex_};
        collect_own_gpu_ = enabled;
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
            return !pending_ && !active_ && !reset_log_;
        });
    }

    void stop() {
        {
            std::scoped_lock lock{mutex_};
            if (stopped_) return;
            stopped_ = true;
            pending_ = false;
            reset_log_ = false;
            thread_.request_stop();
            condition_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    void finish_failed_sample() noexcept {
        try {
            std::scoped_lock lock{mutex_};
            active_ = false;
            published_.reset();
        } catch (...) {
        }
        condition_.notify_all();
    }

    void run(std::stop_token stop) noexcept {
        try {
            run_loop(stop);
        } catch (...) {
            try {
                std::scoped_lock lock{mutex_};
                worker_failed_ = true;
                pending_ = false;
                reset_log_ = false;
                active_ = false;
                published_.reset();
            } catch (...) {
            }
            condition_.notify_all();
        }
    }

    void run_loop(std::stop_token stop) {
        static_cast<void>(SetThreadPriority(
            GetCurrentThread(), THREAD_PRIORITY_NORMAL));
        std::optional<ResourceTelemetryBinding> sampler_binding;
        std::unique_ptr<NativeResourceSamplers> native;
        std::optional<ResourceTelemetryBinding> log_binding;
        std::unique_ptr<NativeGameLogSampler> log_sampler;
        game::GameLogSessionParser log_parser;
        GameLogBoundaryExtractor log_boundaries;
        GameLogBoundaryEvents catch_up_boundaries;
        while (!stop.stop_requested()) {
            ResourceSampleRequest request;
            std::uint64_t request_generation = 0;
            {
                std::unique_lock lock{mutex_};
                if (!condition_.wait(lock, stop, [this] {
                        return pending_ || reset_log_;
                    })) {
                    break;
                }
                if (reset_log_) {
                    reset_log_ = false;
                    active_ = true;
                    lock.unlock();
                    // Detach/rebind closes the retained file on its owning
                    // worker even when no further sample is requested.
                    log_sampler.reset();
                    log_binding.reset();
                    log_parser.reset();
                    log_boundaries.reset();
                    catch_up_boundaries = {};
                    lock.lock();
                    active_ = false;
                    condition_.notify_all();
                    continue;
                }
                if (!binding_) continue;
                request.binding = *binding_;
                request.group = next_group_;
                request.sampled_at_ns = pending_at_ns_;
                request.collect_own_gpu = collect_own_gpu_;
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
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
            detail::GameLogTestTrace test_trace;
            test_trace.request_generation = request_generation;
            record_game_log_publication(test_trace, "sampling");
#endif
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
                        log_boundaries.reset();
                        catch_up_boundaries = {};
                    }
                    bool log_queue_has_room = false;
                    {
                        std::scoped_lock lock{mutex_};
                        log_queue_has_room = log_chunks_.size() < 8;
                    }
                    if (log_queue_has_room) {
                        log_chunk = log_sampler->sample(request.sampled_at_ns);
                        if (log_chunk) {
                            if (log_chunk->reset_parser) {
                                log_parser.reset();
                                log_boundaries.reset();
                                catch_up_boundaries = {};
                            }
                            if (!log_chunk->bytes.empty()) {
                                const bool verified_log_identity =
                                    game::game_log_belongs_to_process(
                                        log_chunk->creation_filetime,
                                        request.binding.identity.process_start_id);
                                auto boundaries = log_boundaries.feed(
                                    log_chunk->bytes, verified_log_identity);
                                if (log_chunk->historical) {
                                    // Preserve bounded lifecycle/readback state,
                                    // not historical speculative prewarm work.
                                    if (boundaries.graphics_readback) {
                                        catch_up_boundaries.graphics_readback =
                                            std::move(boundaries.graphics_readback);
                                    }
                                    catch_up_boundaries.load_map_started |=
                                        boundaries.load_map_started;
                                    catch_up_boundaries.startup_ready |=
                                        boundaries.startup_ready;
                                    catch_up_boundaries.verified_engine_exit |=
                                        boundaries.verified_engine_exit;
                                    catch_up_boundaries.new_settings_restart_requested |=
                                        boundaries.new_settings_restart_requested;
                                } else {
                                    log_chunk->boundaries = std::move(boundaries);
                                }
                                // Keep the parser's existing bounded input size.
                                // Only the final current snapshot crosses to UI.
                                std::optional<game::GameLogSession> parsed_session;
                                constexpr std::size_t kParserChunkBytes = 32 * 1024;
                                const std::string_view bytes{log_chunk->bytes};
                                for (std::size_t offset = 0; offset < bytes.size();
                                     offset += kParserChunkBytes) {
                                    if (auto parsed = log_parser.feed(
                                            bytes.substr(offset, kParserChunkBytes),
                                            request.sampled_at_ns,
                                            !log_chunk->historical)) {
                                        parsed_session = std::move(parsed);
                                    }
                                }
                                if (!log_chunk->catching_up &&
                                    log_parser.current() &&
                                    (parsed_session || log_chunk->historical)) {
                                    log_chunk->parsed_session =
                                        game::make_game_log_session_snapshot(
                                            parsed_session
                                                ? std::move(*parsed_session)
                                                : *log_parser.current());
                                }
                                if (log_chunk->historical &&
                                    !log_chunk->catching_up) {
                                    log_chunk->boundaries =
                                        std::move(catch_up_boundaries);
                                    catch_up_boundaries = {};
                                }
                                // Online corpse capability/action receipts are
                                // intentionally emitted only once per World.
                                // Always hand the parser's current snapshot to
                                // the UI boundary for a chunk containing one,
                                // even when the receipt only refreshed an
                                // already-known value and feed() therefore had
                                // no value-change snapshot to publish.
                                if (log_chunk->bytes.find(
                                        "KF2OPT_ONLINE_CORPSE") !=
                                        std::string::npos &&
                                    log_parser.current() &&
                                    !log_chunk->catching_up &&
                                    !log_chunk->parsed_session) {
                                    log_chunk->parsed_session =
                                        game::make_game_log_session_snapshot(
                                            *log_parser.current());
                                }
                                // Raw Launch.log bytes are worker-private. The
                                // UI consumes only bounded boundary events and
                                // immutable structured session snapshots.
                                std::string{}.swap(log_chunk->bytes);
                            }
                            const auto backlog = log_chunk->parser_stats;
                            log_chunk->parser_stats = log_parser.stats();
                            log_chunk->parser_stats.backlog_bytes = backlog.backlog_bytes;
                            log_chunk->parser_stats.catch_up_age_ns = backlog.catch_up_age_ns;
                        } else if (const auto expired =
                                       log_parser.expire_observations(
                                           request.sampled_at_ns)) {
                            GameLogChunk expiration;
                            expiration.identity = request.binding.identity;
                            expiration.observations_expired = true;
                            expiration.parsed_session =
                                game::make_game_log_session_snapshot(
                                    std::move(*expired));
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
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                test_trace.sample_exception = true;
#endif
                // A failed desktop provider invalidates only this group. The
                // next request retries on the same single worker.
                batch = {};
            } catch (...) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                test_trace.sample_exception = true;
#endif
                batch = {};
            }
            batch.group = request.group;

            try {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                if (fail_next_telemetry_publication.exchange(
                        false, std::memory_order_acq_rel)) {
                    throw std::bad_alloc{};
                }
#endif
                std::scoped_lock lock{mutex_};
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                test_trace.current_generation = generation_;
#endif
                if (log_chunk && binding_ && !reset_log_ &&
                    same_session(*binding_, request.binding)) {
                    log_chunks_.push_back(std::move(*log_chunk));
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                    test_trace.chunk_queued = true;
#endif
                }
                if (stop.stop_requested() || !binding_ ||
                    generation_ != request_generation ||
                    !same_binding(*binding_, request.binding)) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                    record_game_log_publication(test_trace, "generation_rejected");
#endif
                    active_ = false;
                    condition_.notify_all();
                    continue;
                }

                auto next = published_
                    ? std::make_shared<ResourceTelemetrySnapshot>(*published_)
                    : std::make_shared<ResourceTelemetrySnapshot>();
                if (next->generation != request_generation) {
                    *next = {};
                    next->generation = request_generation;
                    next->identity = request.binding.identity;
                    next->adapter_luid = request.binding.adapter_luid;
                }
                if (batch.group == ResourceSampleGroup::process_and_memory) {
                    next->process_sampled_at_ns = request.sampled_at_ns;
                    next->process = std::move(batch.process);
                    next->system_memory = std::move(batch.system_memory);
                } else {
                    next->gpu_sampled_at_ns = request.sampled_at_ns;
                    next->gpu = std::move(batch.gpu);
                    next->own_gpu_percent = collect_own_gpu_ && request.collect_own_gpu
                        ? batch.own_gpu_percent : std::nullopt;
                    next->driver_gpu_percent = batch.driver_gpu_percent;
                    next->nvidia_source = batch.nvidia_source;
                    next->gpu_provider_status =
                        std::move(batch.gpu_provider_status);
                    next->detected_process_adapter =
                        std::move(batch.detected_process_adapter);
                }
                next->publication_sequence = publication_sequence_ + 1;
                publication_sequence_ = next->publication_sequence;
                published_ = std::move(next);
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                record_game_log_publication(test_trace, "published");
#endif
                active_ = false;
                condition_.notify_all();
            } catch (...) {
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
                record_game_log_publication(test_trace, "publication_failed");
#endif
                finish_failed_sample();
            }
        }
    }

    ResourceSampleFunction custom_sample_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    bool stopped_{false};
    bool worker_failed_{false};
    bool pending_{false};
    bool reset_log_{false};
    bool active_{false};
    bool collect_own_gpu_{false};
    std::uint64_t pending_at_ns_{0};
    std::uint64_t generation_{0};
    std::uint64_t publication_sequence_{0};
    ResourceSampleGroup next_group_{ResourceSampleGroup::process_and_memory};
    std::optional<ResourceTelemetryBinding> binding_;
    std::shared_ptr<const ResourceTelemetrySnapshot> published_;
    std::deque<GameLogChunk> log_chunks_;
    // Construct last and destroy first. The worker may access every state
    // member above as soon as its thread begins running.
    std::jthread thread_;
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
#ifdef KF2_RESOURCE_TELEMETRY_WORKER_TESTING
    ++resource_requests;
    if (resource_request_hook) resource_request_hook(*this);
#endif
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

std::optional<std::uint64_t> ResourceTelemetryWorker::cpu_work_ns() const noexcept {
    return implementation_->cpu_work_ns();
}

void ResourceTelemetryWorker::set_own_gpu_enabled(bool enabled) {
    implementation_->set_own_gpu_enabled(enabled);
}

}  // namespace kf2::telemetry
