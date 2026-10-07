#include "kf2/platform/windows/dxgi_frame_timing_session.hpp"
#include "kf2/platform/windows/thread_cpu_time.hpp"

#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kf2::platform::windows {
namespace {

constexpr GUID kDxgiProvider{
    0xca11c036, 0x0102, 0x4a2d,
    {0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xd3, 0xc9}};
constexpr USHORT kPresentStartEvent = 178;
constexpr USHORT kPresentStopEvent = 179;
// Microsoft-Windows-DXGI manifest task IDXGISwapChain_Present. These logging
// events bracket the application call and expose its swap-chain and HRESULT.
constexpr std::uint32_t kPresentTest = 0x1;
constexpr std::size_t kMaximumPendingPresents = 256;
constexpr std::uint64_t kPendingLifetimeSeconds = 30;
constexpr wchar_t kSessionPrefix[] = L"KF2OptimizerNext-DXGI-";
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
std::atomic_bool fail_next_event_callback{false};
std::atomic_uint failed_worker_ordinal{0};
std::atomic<DxgiFrameTimingSession::TestStartOperation> test_start_operation{};

void test_worker_creation(unsigned int ordinal) {
    unsigned int expected = ordinal;
    if (failed_worker_ordinal.compare_exchange_strong(expected, 0)) {
        throw std::system_error{
            ERROR_NOT_ENOUGH_MEMORY, std::system_category()};
    }
}
#endif

struct PresentStartPayload {
    std::uint64_t swap_chain;
    std::uint32_t sync_interval;
    std::uint32_t flags;
};
static_assert(sizeof(PresentStartPayload) == 16);

std::uint64_t qpc_to_ns(std::uint64_t ticks, std::uint64_t frequency,
                        std::uint64_t nanoseconds_per_tick) {
    std::uint64_t high{};
    const auto low = _umul128(ticks,
        nanoseconds_per_tick ? nanoseconds_per_tick : 1'000'000'000ULL, &high);
    // Integral Windows clock ratios need only a multiply. Reject timestamps
    // outside the nanosecond range rather than overflowing or rounding them.
    if (nanoseconds_per_tick) return high == 0 ? low : 0;
    if (frequency == 0 || high >= frequency) return 0;
    std::uint64_t remainder{};
    return _udiv128(high, low, frequency, &remainder);
}

bool process_is_alive(DWORD pid, std::uint64_t creation_time = 0) {
    if (pid == 0 || (creation_time == 0 && pid == GetCurrentProcessId())) return true;
    const auto access = SYNCHRONIZE |
        (creation_time != 0 ? PROCESS_QUERY_LIMITED_INFORMATION : 0);
    const HANDLE process = OpenProcess(access, FALSE, pid);
    // Unknown ownership is not proof that another instance's session is stale.
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    const auto waited = WaitForSingleObject(process, 0);
    bool alive = waited != WAIT_OBJECT_0;
    if (waited == WAIT_TIMEOUT && creation_time != 0) {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
            const auto actual_creation =
                (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
                creation.dwLowDateTime;
            alive = actual_creation == creation_time;
        }
    }
    CloseHandle(process);
    return alive;
}

void stop_stale_sessions(
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
    decltype(&QueryAllTracesW) query_traces = &QueryAllTracesW,
    decltype(&ControlTraceW) control_trace = &ControlTraceW
#endif
) {
#ifndef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
    constexpr auto query_traces = &QueryAllTracesW;
    constexpr auto control_trace = &ControlTraceW;
#endif
    constexpr ULONG kMaximumSessions = 64;
    std::vector<std::vector<std::byte>> storage(
        kMaximumSessions,
        std::vector<std::byte>(sizeof(EVENT_TRACE_PROPERTIES) +
                               2 * MAX_PATH * sizeof(wchar_t)));
    std::vector<EVENT_TRACE_PROPERTIES*> sessions;
    sessions.reserve(kMaximumSessions);
    for (auto& bytes : storage) {
        auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data());
        properties->Wnode.BufferSize = static_cast<ULONG>(bytes.size());
        properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        properties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) +
                                        MAX_PATH * sizeof(wchar_t);
        sessions.push_back(properties);
    }
    ULONG count = kMaximumSessions;
    const auto status = query_traces(sessions.data(), kMaximumSessions, &count);
    // ERROR_MORE_DATA still fills the supplied entries. Keep the existing
    // startup-only budget, but never use the total session count as its bound.
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
        return;
    count = std::min(count, kMaximumSessions);
    const std::wstring_view prefix{kSessionPrefix};
    for (ULONG index = 0; index < count; ++index) {
        auto* properties = sessions[index];
        const auto bytes = std::min(static_cast<std::size_t>(properties->Wnode.BufferSize),
                                    storage[index].size());
        const auto offset = properties->LoggerNameOffset;
        if (offset < sizeof(EVENT_TRACE_PROPERTIES) || offset >= bytes ||
            offset % alignof(wchar_t) != 0) continue;
        const auto* name = reinterpret_cast<const wchar_t*>(
            reinterpret_cast<const std::byte*>(properties) +
            offset);
        const auto characters = (bytes - offset) / sizeof(wchar_t);
        const auto* end = std::find(name, name + characters, L'\0');
        if (end == name + characters) continue;
        const std::wstring_view session_name{name, static_cast<std::size_t>(end - name)};
        if (!session_name.starts_with(prefix)) continue;
        const auto owner_text = session_name.substr(prefix.size());
        const auto separator = owner_text.find(L'-');
        const auto pid_text = owner_text.substr(0, separator);
        if (pid_text.empty() || !std::all_of(pid_text.begin(), pid_text.end(),
            [](wchar_t digit) { return digit >= L'0' && digit <= L'9'; })) continue;
        errno = 0;
        const unsigned long pid = std::wcstoul(std::wstring{pid_text}.c_str(),
                                               nullptr, 10);
        if (errno == ERANGE || pid == 0) continue;
        std::uint64_t creation_time{};
        if (separator != std::wstring_view::npos) {
            const auto time_text = owner_text.substr(separator + 1);
            if (time_text.empty() || !std::all_of(time_text.begin(), time_text.end(),
                [](wchar_t digit) { return digit >= L'0' && digit <= L'9'; })) continue;
            errno = 0;
            creation_time = std::wcstoull(std::wstring{time_text}.c_str(), nullptr, 10);
            if (errno == ERANGE || creation_time == 0) continue;
        }
        if (process_is_alive(static_cast<DWORD>(pid), creation_time)) continue;
        EVENT_TRACE_PROPERTIES stop_properties{};
        stop_properties.Wnode.BufferSize = sizeof(stop_properties);
        static_cast<void>(control_trace(
            0, std::wstring{session_name}.c_str(), &stop_properties,
            EVENT_TRACE_CONTROL_STOP));
    }
}

}  // namespace

struct DxgiFrameTimingSession::Impl {
    struct PendingPresent {
        std::uint64_t timestamp_qpc{};
        std::uint64_t swap_chain{};
    };

    telemetry::SampleIdentity identity;
    telemetry::PresentSource* sink{};
    std::wstring name;
    std::vector<std::byte> properties_storage;
    TRACEHANDLE session_handle{};
    TRACEHANDLE trace_handle{INVALID_PROCESSTRACE_HANDLE};
    EVENT_TRACE_LOGFILEW trace_log{};
    std::thread trace_worker;
    std::thread flush_worker;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> observed_events_lost{0};
    std::uint64_t reported_events_lost{};
    std::uint64_t invalidated_events_lost{};
    std::uint64_t unreported_pending_loss{};
    std::uint64_t last_pending_cleanup_qpc{};
    std::uint64_t qpc_frequency{};
    std::uint64_t nanoseconds_per_tick{};
    std::unordered_map<ULONG, PendingPresent> pending_by_thread;

    ~Impl() { shutdown(); }

    void shutdown() noexcept {
        running.store(false, std::memory_order_release);
        stop_session();
        if (flush_worker.joinable()) flush_worker.join();
        if (trace_worker.joinable()) trace_worker.join();
        close_trace();
    }

    EVENT_TRACE_PROPERTIES* properties() {
        return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(
            properties_storage.data());
    }

    static ULONG WINAPI buffer_callback(EVENT_TRACE_LOGFILEW* log) {
        auto* self = static_cast<Impl*>(log->Context);
        self->observed_events_lost.store(log->EventsLost,
                                         std::memory_order_release);
        return self->running.load(std::memory_order_acquire) ? TRUE : FALSE;
    }

    static void WINAPI event_callback(EVENT_RECORD* record) noexcept {
        if (!record) return;
        auto* self = static_cast<Impl*>(record->UserContext);
        if (!self) return;
        try {
            self->on_event(*record);
        } catch (...) {
            self->pending_by_thread.clear();
            self->observed_events_lost.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void on_event(const EVENT_RECORD& record) {
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
        if (fail_next_event_callback.exchange(false,
                                               std::memory_order_acq_rel)) {
            throw std::bad_alloc{};
        }
#endif
        const auto& header = record.EventHeader;
        if (header.ProcessId != identity.pid ||
            !IsEqualGUID(header.ProviderId, kDxgiProvider)) {
            return;
        }
        if ((header.EventDescriptor.Id != kPresentStartEvent &&
             header.EventDescriptor.Id != kPresentStopEvent) ||
            !record.UserData || header.TimeStamp.QuadPart <= 0 ||
            qpc_frequency == 0) {
            return;
        }
        const auto now_qpc = static_cast<std::uint64_t>(header.TimeStamp.QuadPart);
        const auto total_loss = observed_events_lost.load(
            std::memory_order_acquire);
        if (total_loss != invalidated_events_lost) {
            // A lost Stop must never match a reused thread's later Stop.
            pending_by_thread.clear();
            invalidated_events_lost = total_loss;
        }
        const auto expired = [&](const PendingPresent& present) {
            return now_qpc >= present.timestamp_qpc &&
                (now_qpc - present.timestamp_qpc) / qpc_frequency >=
                    kPendingLifetimeSeconds;
        };
        // Event-driven maintenance: at most one bounded pass per QPC second,
        // with no timer, additional worker or thread-liveness queries.
        if (now_qpc >= last_pending_cleanup_qpc &&
            now_qpc - last_pending_cleanup_qpc >= qpc_frequency) {
            last_pending_cleanup_qpc = now_qpc;
            for (auto pending = pending_by_thread.begin();
                 pending != pending_by_thread.end();) {
                if (expired(pending->second)) {
                    pending = pending_by_thread.erase(pending);
                    ++unreported_pending_loss;
                } else {
                    ++pending;
                }
            }
        }
        if (header.EventDescriptor.Id == kPresentStartEvent) {
            if (record.UserDataLength < sizeof(PresentStartPayload)) return;
            PresentStartPayload payload{};
            std::memcpy(&payload, record.UserData, sizeof(payload));
            if ((payload.flags & kPresentTest) != 0 || payload.swap_chain == 0)
                return;
            const auto existing = pending_by_thread.find(header.ThreadId);
            if (existing != pending_by_thread.end()) {
                existing->second = {now_qpc, payload.swap_chain};
                return;
            }
            if (pending_by_thread.size() >= kMaximumPendingPresents) {
                const auto oldest = std::min_element(
                    pending_by_thread.begin(), pending_by_thread.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.timestamp_qpc < right.second.timestamp_qpc;
                    });
                pending_by_thread.erase(oldest);
                ++unreported_pending_loss;
            }
            pending_by_thread.emplace(header.ThreadId,
                PendingPresent{now_qpc, payload.swap_chain});
            return;
        }
        if (header.EventDescriptor.Id != kPresentStopEvent ||
            record.UserDataLength < sizeof(std::uint32_t)) {
            return;
        }
        const auto pending = pending_by_thread.find(header.ThreadId);
        if (pending == pending_by_thread.end()) return;
        const PendingPresent present = pending->second;
        pending_by_thread.erase(pending);
        if (now_qpc < present.timestamp_qpc || expired(present)) {
            ++unreported_pending_loss;
            return;
        }
        std::int32_t result{};
        std::memcpy(&result, record.UserData, sizeof(result));
        if (FAILED(static_cast<HRESULT>(result)) || present.timestamp_qpc == 0)
            return;
        const auto timestamp_ns = qpc_to_ns(present.timestamp_qpc, qpc_frequency,
                                            nanoseconds_per_tick);
        if (timestamp_ns == 0) return;

        const auto new_loss = total_loss >= reported_events_lost
            ? total_loss - reported_events_lost : total_loss;
        reported_events_lost = total_loss;
        static_cast<void>(sink->ingest(
            {identity, timestamp_ns,
             1, true, new_loss + unreported_pending_loss, present.swap_chain}));
        unreported_pending_loss = 0;
    }

    void process_trace() {
        TRACEHANDLE handle = trace_handle;
        static_cast<void>(ProcessTrace(&handle, 1, nullptr, nullptr));
    }

    void flush_trace() {
        while (running.load(std::memory_order_acquire)) {
            EVENT_TRACE_PROPERTIES flush_properties{};
            flush_properties.Wnode.BufferSize = sizeof(flush_properties);
            static_cast<void>(FlushTraceW(
                session_handle, nullptr, &flush_properties));
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }

    ULONG open() {
        constexpr std::size_t kNameBytes = 128 * sizeof(wchar_t);
        properties_storage.assign(sizeof(EVENT_TRACE_PROPERTIES) + kNameBytes,
                                  std::byte{});
        auto* session_properties = properties();
        session_properties->Wnode.BufferSize =
            static_cast<ULONG>(properties_storage.size());
        session_properties->Wnode.ClientContext = 1;
        session_properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        session_properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE |
            EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
        session_properties->BufferSize = 16;
        session_properties->MinimumBuffers = 4;
        session_properties->MaximumBuffers = 16;
        session_properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        TRACEHANDLE started_session{};
        ULONG status = StartTraceW(&started_session, name.c_str(),
                                   session_properties);
        if (status != ERROR_SUCCESS) return status;
        session_handle = started_session;

        alignas(EVENT_FILTER_EVENT_ID) std::array<
            std::byte, offsetof(EVENT_FILTER_EVENT_ID, Events) +
                           2 * sizeof(USHORT)> event_filter_storage{};
        auto* event_filter = reinterpret_cast<EVENT_FILTER_EVENT_ID*>(
            event_filter_storage.data());
        event_filter->FilterIn = TRUE;
        event_filter->Count = 2;
        event_filter->Events[0] = kPresentStartEvent;
        event_filter->Events[1] = kPresentStopEvent;
        EVENT_FILTER_DESCRIPTOR filter_descriptor{};
        filter_descriptor.Ptr = reinterpret_cast<ULONGLONG>(event_filter);
        filter_descriptor.Size =
            static_cast<ULONG>(event_filter_storage.size());
        filter_descriptor.Type = EVENT_FILTER_TYPE_EVENT_ID;
        ENABLE_TRACE_PARAMETERS enable_parameters{};
        enable_parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
        enable_parameters.EnableFilterDesc = &filter_descriptor;
        enable_parameters.FilterDescCount = 1;
        status = EnableTraceEx2(
            session_handle, &kDxgiProvider,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
            0, 0, 0, &enable_parameters);
        if (status != ERROR_SUCCESS) return status;

        trace_log = {};
        trace_log.LoggerName = name.data();
        trace_log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
            PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        trace_log.Context = this;
        trace_log.BufferCallback = buffer_callback;
        trace_log.EventRecordCallback = event_callback;
        trace_handle = OpenTraceW(&trace_log);
        if (trace_handle == INVALID_PROCESSTRACE_HANDLE) return GetLastError();
        return ERROR_SUCCESS;
    }

    void stop_session() {
        if (session_handle != 0) {
            EVENT_TRACE_PROPERTIES stop_properties{};
            stop_properties.Wnode.BufferSize = sizeof(stop_properties);
            static_cast<void>(ControlTraceW(
                session_handle, nullptr, &stop_properties,
                EVENT_TRACE_CONTROL_STOP));
        }
    }

    void close_trace() {
        if (trace_handle != INVALID_PROCESSTRACE_HANDLE) {
            static_cast<void>(CloseTrace(trace_handle));
            trace_handle = INVALID_PROCESSTRACE_HANDLE;
        }
        session_handle = 0;
    }
};

#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
void DxgiFrameTimingSession::test_set_start_operation(
    TestStartOperation operation) noexcept {
    test_start_operation.store(operation, std::memory_order_release);
}

void DxgiFrameTimingSession::test_cleanup_stale_sessions(
    decltype(&QueryAllTracesW) query_traces,
    decltype(&ControlTraceW) control_trace) {
    stop_stale_sessions(query_traces, control_trace);
}

void DxgiFrameTimingSession::test_fail_worker_creation(
    unsigned int ordinal) noexcept {
    failed_worker_ordinal.store(ordinal, std::memory_order_release);
}

bool DxgiFrameTimingSession::test_event_callback_exception_boundary() noexcept {
    Impl implementation;
    implementation.pending_by_thread.emplace(1, Impl::PendingPresent{1, 1});
    EVENT_RECORD event{};
    event.UserContext = &implementation;
    fail_next_event_callback.store(true, std::memory_order_release);
    Impl::event_callback(&event);
    return implementation.observed_events_lost.load(
               std::memory_order_acquire) == 1 &&
           implementation.pending_by_thread.empty();
}

std::unique_ptr<DxgiFrameTimingSession> DxgiFrameTimingSession::test_parser(
    telemetry::SampleIdentity identity, telemetry::PresentSource& sink,
    std::uint64_t qpc_frequency) {
    auto impl = std::make_unique<Impl>();
    impl->identity = identity;
    impl->sink = &sink;
    impl->qpc_frequency = qpc_frequency;
    impl->nanoseconds_per_tick = qpc_frequency != 0 &&
        1'000'000'000ULL % qpc_frequency == 0
        ? 1'000'000'000ULL / qpc_frequency : 0;
    return std::unique_ptr<DxgiFrameTimingSession>{
        new DxgiFrameTimingSession{std::move(impl)}};
}

void DxgiFrameTimingSession::test_present_event(
    bool start, std::uint32_t thread, std::uint64_t timestamp_qpc,
    std::uint64_t swap_chain, std::uint32_t flags, std::int32_t result) {
    PresentStartPayload payload{swap_chain, 0, flags};
    EVENT_RECORD event{};
    event.UserContext = implementation_.get();
    event.EventHeader.ProviderId = kDxgiProvider;
    event.EventHeader.ProcessId = implementation_->identity.pid;
    event.EventHeader.ThreadId = thread;
    event.EventHeader.TimeStamp.QuadPart = static_cast<LONGLONG>(timestamp_qpc);
    event.EventHeader.EventDescriptor.Id = start
        ? kPresentStartEvent : kPresentStopEvent;
    event.UserData = start ? static_cast<void*>(&payload)
                          : static_cast<void*>(&result);
    event.UserDataLength = static_cast<USHORT>(start
        ? sizeof(payload) : sizeof(result));
    Impl::event_callback(&event);
}

void DxgiFrameTimingSession::test_events_lost(std::uint32_t count) noexcept {
    EVENT_TRACE_LOGFILEW log{};
    log.Context = implementation_.get();
    log.EventsLost = count;
    static_cast<void>(Impl::buffer_callback(&log));
}

std::size_t DxgiFrameTimingSession::test_pending_count() const noexcept {
    return implementation_->pending_by_thread.size();
}
#endif

DxgiFrameTimingSession::DxgiFrameTimingSession(
    std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}

DxgiFrameTimingSession::~DxgiFrameTimingSession() {
    static_cast<void>(stop());
}

Result<std::unique_ptr<DxgiFrameTimingSession>>
DxgiFrameTimingSession::start(telemetry::SampleIdentity identity,
                              telemetry::PresentSource& sink) {
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
    if (const auto operation = test_start_operation.load(
            std::memory_order_acquire)) {
        return operation(identity, sink);
    }
#endif
    stop_stale_sessions();
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure, L"DXGI trace owner identity is unavailable",
             GetLastError()});
    }
    const auto creation_time =
        (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
        creation.dwLowDateTime;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure, L"High-resolution clock is unavailable",
             GetLastError()});
    }
    auto impl = std::make_unique<Impl>();
    impl->identity = identity;
    impl->sink = &sink;
    impl->name = std::wstring{kSessionPrefix} +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(creation_time);
    impl->qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    impl->nanoseconds_per_tick = 1'000'000'000ULL % impl->qpc_frequency == 0
        ? 1'000'000'000ULL / impl->qpc_frequency : 0;
    const ULONG status = impl->open();
    if (status != ERROR_SUCCESS) {
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure,
             L"Native DXGI frame timing session cannot start", status});
    }
    impl->running.store(true, std::memory_order_release);
    try {
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
        test_worker_creation(1);
#endif
        impl->trace_worker = std::thread{
            [pointer = impl.get()] { pointer->process_trace(); }};
#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
        test_worker_creation(2);
#endif
        impl->flush_worker = std::thread{
            [pointer = impl.get()] { pointer->flush_trace(); }};
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::success(
            std::unique_ptr<DxgiFrameTimingSession>{
                new DxgiFrameTimingSession{std::move(impl)}});
    } catch (const std::system_error& error) {
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure,
             L"Native DXGI frame timing workers cannot start",
             static_cast<std::uint32_t>(error.code().value())});
    } catch (const std::bad_alloc&) {
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure,
             L"Native DXGI frame timing workers cannot start",
             ERROR_NOT_ENOUGH_MEMORY});
    }
}

Result<bool> DxgiFrameTimingSession::stop() {
    if (implementation_) implementation_->shutdown();
    return Result<bool>::success(true);
}

std::optional<std::uint64_t> DxgiFrameTimingSession::cpu_work_ns() const noexcept {
    if (!implementation_ || !implementation_->trace_worker.joinable() ||
        !implementation_->flush_worker.joinable()) return std::nullopt;
    const auto trace = thread_cpu_ns(implementation_->trace_worker.native_handle());
    const auto flush = thread_cpu_ns(implementation_->flush_worker.native_handle());
    return trace && flush ? std::optional{*trace + *flush} : std::nullopt;
}

}  // namespace kf2::platform::windows
