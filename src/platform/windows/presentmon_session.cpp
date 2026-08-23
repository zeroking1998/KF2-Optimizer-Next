#include "kf2/platform/windows/presentmon_session.hpp"

#include <Windows.h>
#include <evntrace.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PresentMonTraceConsumer.hpp"
#include "PresentMonTraceSession.hpp"

namespace kf2::platform::windows {
namespace {
std::uint64_t qpc_to_ns(std::uint64_t ticks, std::uint64_t frequency) {
    return static_cast<std::uint64_t>(static_cast<long double>(ticks) *
        1'000'000'000.0L / static_cast<long double>(frequency));
}

bool process_is_alive(DWORD pid) {
    if (pid == 0 || pid == GetCurrentProcessId()) return true;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    // Access denied is not proof that a process is dead; preserve its session.
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

void stop_stale_presentmon_sessions() {
    constexpr ULONG kMaximumSessions = 64;
    constexpr wchar_t kPrefix[] = L"KF2OptimizerNext-PresentMon-";
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
    const std::wstring_view prefix{kPrefix};
    for (ULONG index = 0; index < count; ++index) {
        auto* properties = sessions[index];
        const auto* name = reinterpret_cast<const wchar_t*>(
            reinterpret_cast<const std::byte*>(properties) +
            properties->LoggerNameOffset);
        const std::wstring_view session_name{name};
        if (!session_name.starts_with(prefix)) continue;
        const std::wstring_view pid_text = session_name.substr(prefix.size());
        wchar_t* end = nullptr;
        const unsigned long pid = std::wcstoul(std::wstring{pid_text}.c_str(), &end, 10);
        if (pid == 0 || process_is_alive(static_cast<DWORD>(pid))) continue;
        EVENT_TRACE_PROPERTIES stop_properties{};
        stop_properties.Wnode.BufferSize = sizeof(stop_properties);
        static_cast<void>(ControlTraceW(0, std::wstring{session_name}.c_str(),
                                        &stop_properties, EVENT_TRACE_CONTROL_STOP));
    }
}
}  // namespace

struct PresentMonSession::Impl {
    telemetry::SampleIdentity identity;
    telemetry::PresentSource* sink{};
    std::wstring name;
    PMTraceConsumer consumer;
    PMTraceSession session;
    std::thread trace_worker;
    std::thread flush_worker;
    std::atomic<bool> running{false};
    std::uint64_t qpc_frequency{};

    void process_trace() {
        TRACEHANDLE handle = session.mTraceHandle;
        static_cast<void>(ProcessTrace(&handle, 1, nullptr, nullptr));
    }
    void ingest_completed_presents() {
        std::vector<std::shared_ptr<PresentEvent>> presents;
        consumer.DequeuePresentEvents(presents);
        for (const auto& present : presents) {
            const bool completed_application_present = present &&
                (present->FinalState == PresentResult::Presented ||
                 present->FinalState == PresentResult::Discarded);
            if (!present || present->ProcessId != identity.pid ||
                present->PresentStartTime == 0 || present->PresentFailed ||
                present->IsLost || !completed_application_present) {
                continue;
            }
            // MSI Afterburner reports KF2's application-present cadence. A
            // successful Present remains an application frame when the display
            // pipeline later discards it; excluding those frames under-reports
            // the same running game by roughly the discard rate.
            static_cast<void>(sink->ingest(
                {identity,
                 qpc_to_ns(present->PresentStartTime, qpc_frequency),
                 1, true, 0, present->SwapChainAddress}));
        }
    }
    void flush_trace() {
        while (running.load(std::memory_order_acquire)) {
            EVENT_TRACE_PROPERTIES properties{};
            properties.Wnode.BufferSize = sizeof(properties);
            static_cast<void>(FlushTraceW(session.mSessionHandle, nullptr,
                                          &properties));
            ingest_completed_presents();
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }
};

PresentMonSession::PresentMonSession(std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}
PresentMonSession::~PresentMonSession() { static_cast<void>(stop()); }

Result<std::unique_ptr<PresentMonSession>> PresentMonSession::start(
    telemetry::SampleIdentity identity, telemetry::PresentSource& sink) {
    stop_stale_presentmon_sessions();
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        return Result<std::unique_ptr<PresentMonSession>>::failure(
            {ErrorCode::platform_failure, L"High-resolution clock is unavailable",
             GetLastError()});
    }
    auto impl = std::make_unique<Impl>();
    impl->identity = identity;
    impl->sink = &sink;
    impl->name = L"KF2OptimizerNext-PresentMon-" +
                 std::to_wstring(GetCurrentProcessId());
    impl->qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    impl->consumer.mFilteredProcessIds = true;
    // Display tracking supplies the final state needed to reject discarded
    // runtime presents instead of treating every API call as a visible frame.
    impl->consumer.mTrackDisplay = true;
    impl->consumer.mTrackGPU = false;
    impl->consumer.mTrackGPUVideo = false;
    impl->consumer.mTrackInput = false;
    impl->consumer.mRuntimePresentStartOnly = false;
    impl->consumer.AddTrackedProcessForFiltering(identity.pid);
    impl->session.mPMConsumer = &impl->consumer;
    impl->session.mTimestampType = PMTraceSession::TIMESTAMP_TYPE_QPC;
    const ULONG status = impl->session.Start(nullptr, impl->name.c_str());
    if (status != ERROR_SUCCESS) {
        return Result<std::unique_ptr<PresentMonSession>>::failure(
            {ErrorCode::platform_failure,
             L"Embedded PresentMon session cannot start", status});
    }
    impl->running.store(true, std::memory_order_release);
    impl->trace_worker = std::thread{[pointer = impl.get()] { pointer->process_trace(); }};
    impl->flush_worker = std::thread{[pointer = impl.get()] { pointer->flush_trace(); }};
    return Result<std::unique_ptr<PresentMonSession>>::success(
        std::unique_ptr<PresentMonSession>{new PresentMonSession{std::move(impl)}});
}

Result<bool> PresentMonSession::stop() {
    if (!implementation_ ||
        !implementation_->running.exchange(false, std::memory_order_acq_rel)) {
        return Result<bool>::success(true);
    }
    implementation_->session.Stop();
    if (implementation_->trace_worker.joinable()) implementation_->trace_worker.join();
    if (implementation_->flush_worker.joinable()) implementation_->flush_worker.join();
    // Stopping the real-time trace can complete presents whose display state
    // was still pending during the last periodic flush. Preserve those final
    // samples instead of silently dropping them.
    implementation_->ingest_completed_presents();
    return Result<bool>::success(true);
}
}  // namespace kf2::platform::windows
