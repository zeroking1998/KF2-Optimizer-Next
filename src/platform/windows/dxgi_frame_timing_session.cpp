#include "kf2/platform/windows/dxgi_frame_timing_session.hpp"

#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
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
constexpr wchar_t kSessionPrefix[] = L"KF2OptimizerNext-DXGI-";

struct PresentStartPayload {
    std::uint64_t swap_chain;
    std::uint32_t sync_interval;
    std::uint32_t flags;
};
static_assert(sizeof(PresentStartPayload) == 16);

std::uint64_t qpc_to_ns(std::uint64_t ticks, std::uint64_t frequency) {
    return static_cast<std::uint64_t>(static_cast<long double>(ticks) *
        1'000'000'000.0L / static_cast<long double>(frequency));
}

bool process_is_alive(DWORD pid) {
    if (pid == 0 || pid == GetCurrentProcessId()) return true;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

void stop_stale_sessions() {
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
    if (QueryAllTracesW(sessions.data(), kMaximumSessions, &count) != ERROR_SUCCESS)
        return;
    const std::wstring_view prefix{kSessionPrefix};
    for (ULONG index = 0; index < count; ++index) {
        auto* properties = sessions[index];
        const auto* name = reinterpret_cast<const wchar_t*>(
            reinterpret_cast<const std::byte*>(properties) +
            properties->LoggerNameOffset);
        const std::wstring_view session_name{name};
        if (!session_name.starts_with(prefix)) continue;
        const auto pid_text = session_name.substr(prefix.size());
        wchar_t* end = nullptr;
        const unsigned long pid = std::wcstoul(std::wstring{pid_text}.c_str(),
                                               &end, 10);
        if (pid == 0 || process_is_alive(static_cast<DWORD>(pid))) continue;
        EVENT_TRACE_PROPERTIES stop_properties{};
        stop_properties.Wnode.BufferSize = sizeof(stop_properties);
        static_cast<void>(ControlTraceW(
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
    std::uint64_t qpc_frequency{};
    std::unordered_map<ULONG, PendingPresent> pending_by_thread;

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

    static void WINAPI event_callback(EVENT_RECORD* record) {
        auto* self = static_cast<Impl*>(record->UserContext);
        if (self) self->on_event(*record);
    }

    void on_event(const EVENT_RECORD& record) {
        const auto& header = record.EventHeader;
        if (header.ProcessId != identity.pid ||
            !IsEqualGUID(header.ProviderId, kDxgiProvider)) {
            return;
        }
        if (header.EventDescriptor.Id == kPresentStartEvent) {
            if (record.UserDataLength < sizeof(PresentStartPayload)) return;
            PresentStartPayload payload{};
            std::memcpy(&payload, record.UserData, sizeof(payload));
            if ((payload.flags & kPresentTest) != 0 || payload.swap_chain == 0)
                return;
            pending_by_thread[header.ThreadId] = {
                static_cast<std::uint64_t>(header.TimeStamp.QuadPart),
                payload.swap_chain};
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
        std::int32_t result{};
        std::memcpy(&result, record.UserData, sizeof(result));
        if (FAILED(static_cast<HRESULT>(result)) || present.timestamp_qpc == 0)
            return;

        const auto total_loss = observed_events_lost.load(
            std::memory_order_acquire);
        const auto new_loss = total_loss >= reported_events_lost
            ? total_loss - reported_events_lost : total_loss;
        reported_events_lost = total_loss;
        static_cast<void>(sink->ingest(
            {identity, qpc_to_ns(present.timestamp_qpc, qpc_frequency),
             1, true, new_loss, present.swap_chain}));
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

        ULONG status = StartTraceW(&session_handle, name.c_str(),
                                   session_properties);
        if (status != ERROR_SUCCESS) return status;

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

DxgiFrameTimingSession::DxgiFrameTimingSession(
    std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}

DxgiFrameTimingSession::~DxgiFrameTimingSession() {
    static_cast<void>(stop());
}

Result<std::unique_ptr<DxgiFrameTimingSession>>
DxgiFrameTimingSession::start(telemetry::SampleIdentity identity,
                              telemetry::PresentSource& sink) {
    stop_stale_sessions();
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
                 std::to_wstring(GetCurrentProcessId());
    impl->qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    const ULONG status = impl->open();
    if (status != ERROR_SUCCESS) {
        impl->stop_session();
        impl->close_trace();
        return Result<std::unique_ptr<DxgiFrameTimingSession>>::failure(
            {ErrorCode::platform_failure,
             L"Native DXGI frame timing session cannot start", status});
    }
    impl->running.store(true, std::memory_order_release);
    impl->trace_worker = std::thread{
        [pointer = impl.get()] { pointer->process_trace(); }};
    impl->flush_worker = std::thread{
        [pointer = impl.get()] { pointer->flush_trace(); }};
    return Result<std::unique_ptr<DxgiFrameTimingSession>>::success(
        std::unique_ptr<DxgiFrameTimingSession>{
            new DxgiFrameTimingSession{std::move(impl)}});
}

Result<bool> DxgiFrameTimingSession::stop() {
    if (!implementation_ ||
        !implementation_->running.exchange(false, std::memory_order_acq_rel)) {
        return Result<bool>::success(true);
    }
    implementation_->stop_session();
    if (implementation_->flush_worker.joinable())
        implementation_->flush_worker.join();
    if (implementation_->trace_worker.joinable())
        implementation_->trace_worker.join();
    implementation_->close_trace();
    return Result<bool>::success(true);
}

}  // namespace kf2::platform::windows
