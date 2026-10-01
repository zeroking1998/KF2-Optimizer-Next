#pragma once

#include <memory>

#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
#include <Windows.h>
#include <evntrace.h>
#endif

#include "kf2/core/result.hpp"
#include "kf2/telemetry/present_source.hpp"

namespace kf2::platform::windows {

// Process-bound DirectX 11 application-present timing. The session consumes
// only the DXGI Present Start/Stop ETW events; it never injects into KF2.
class DxgiFrameTimingSession final {
public:
    DxgiFrameTimingSession(const DxgiFrameTimingSession&) = delete;
    DxgiFrameTimingSession& operator=(const DxgiFrameTimingSession&) = delete;
    ~DxgiFrameTimingSession();

    [[nodiscard]] static Result<std::unique_ptr<DxgiFrameTimingSession>> start(
        telemetry::SampleIdentity identity, telemetry::PresentSource& sink);
    [[nodiscard]] Result<bool> stop();

#ifdef KF2_DXGI_FRAME_TIMING_SESSION_TESTING
    static void test_cleanup_stale_sessions(
        decltype(&QueryAllTracesW) query_traces,
        decltype(&ControlTraceW) control_trace);
    [[nodiscard]] static bool test_event_callback_exception_boundary() noexcept;
    static void test_fail_worker_creation(unsigned int ordinal) noexcept;
    [[nodiscard]] static std::unique_ptr<DxgiFrameTimingSession> test_parser(
        telemetry::SampleIdentity identity, telemetry::PresentSource& sink,
        std::uint64_t qpc_frequency);
    void test_present_event(bool start, std::uint32_t thread,
                            std::uint64_t timestamp_qpc,
                            std::uint64_t swap_chain = 1,
                            std::uint32_t flags = 0,
                            std::int32_t result = 0);
    void test_events_lost(std::uint32_t count) noexcept;
    [[nodiscard]] std::size_t test_pending_count() const noexcept;
#endif

private:
    struct Impl;
    explicit DxgiFrameTimingSession(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::platform::windows
