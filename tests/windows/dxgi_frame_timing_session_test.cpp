#include <Windows.h>
#include <evntrace.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>

#include "kf2/platform/windows/dxgi_frame_timing_session.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    using kf2::platform::windows::DxgiFrameTimingSession;
    CHECK(kf2::platform::windows::DxgiFrameTimingSession::
        test_event_callback_exception_boundary());
    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 120};
    CHECK(source.start().has_value());

    const auto session_name = std::wstring{L"KF2OptimizerNext-DXGI-"} +
                              std::to_wstring(GetCurrentProcessId());
    for (const unsigned int worker : {1U, 2U}) {
        DxgiFrameTimingSession::test_fail_worker_creation(worker);
        bool threw = false;
        try {
            const auto failed = DxgiFrameTimingSession::start(identity, source);
            CHECK(!failed.has_value());
            CHECK(failed.error().code == kf2::ErrorCode::platform_failure);
            CHECK(failed.error().native_code == ERROR_NOT_ENOUGH_MEMORY);
        } catch (const std::system_error&) {
            threw = true;
        }
        // Query the actual ETW session, not a cleanup counter or a mock.
        struct QueryProperties {
            EVENT_TRACE_PROPERTIES properties{};
            wchar_t name[128]{};
        } query;
        query.properties.Wnode.BufferSize = sizeof(query);
        query.properties.LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        const ULONG queried = ControlTraceW(0, session_name.c_str(),
            &query.properties, EVENT_TRACE_CONTROL_QUERY);
        if (queried == ERROR_SUCCESS) {
            // Keep the regression's unfixed baseline from leaking a session.
            static_cast<void>(ControlTraceW(0, session_name.c_str(),
                &query.properties, EVENT_TRACE_CONTROL_STOP));
        }
        CHECK(!threw);
        CHECK(queried == ERROR_WMI_INSTANCE_NOT_FOUND);
        auto retry = DxgiFrameTimingSession::start(identity, source);
        CHECK(retry.has_value());
        CHECK(retry.value()->stop().has_value());
        CHECK(retry.value()->stop().has_value());
    }
    auto session =
        kf2::platform::windows::DxgiFrameTimingSession::start(identity, source);
    if (!session.has_value()) {
        std::wcerr << L"DXGI frame timing unavailable: " << session.error().message
                   << L" (" << session.error().native_code << L")\n";
        return EXIT_FAILURE;
    }
    Sleep(20);
    CHECK(session.value()->stop().has_value());
    CHECK(session.value()->stop().has_value());
    CHECK(source.stop().has_value());
    return EXIT_SUCCESS;
}
