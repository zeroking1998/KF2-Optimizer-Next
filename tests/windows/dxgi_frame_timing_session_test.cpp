#include <Windows.h>
#include <evntrace.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "kf2/platform/windows/dxgi_frame_timing_session.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
using kf2::platform::windows::DxgiFrameTimingSession;
using namespace kf2::telemetry;
constexpr SampleIdentity kIdentity{123, 1};
constexpr std::uint64_t kFrequency = 1'000;

int test_capacity() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    for (std::uint32_t thread = 1; thread <= 2'000; ++thread) {
        parser->test_present_event(true, thread, 1'000 + thread);
        CHECK(parser->test_pending_count() <= 256);
    }
    CHECK(parser->test_pending_count() == 256);
    // A retired start must not pair with a much later reused thread ID.
    parser->test_present_event(false, 1, 3'001);
    CHECK(parser->test_pending_count() == 256);
    CHECK(!source.drain(3'001'000'000ULL, 1'000'000'000ULL).fps);
    // At capacity, overwrite an existing thread without discarding another.
    parser->test_present_event(true, 2'000, 3'010, 7);
    CHECK(parser->test_pending_count() == 256);
    parser->test_present_event(false, 2'000, 3'011);
    CHECK(parser->test_pending_count() == 255);
    parser->test_present_event(true, 2'000, 3'026, 7);
    parser->test_present_event(false, 2'000, 3'027);
    const auto metrics = source.drain(3'027'000'000ULL, 1'000'000'000ULL);
    CHECK(metrics.fps && *metrics.fps == 62.5);
    CHECK(metrics.loss_count == 1'744);
    // Admission evicts only the oldest pending start, preserving fresh pairs.
    parser->test_present_event(false, 1'746, 3'028);
    CHECK(parser->test_pending_count() == 254);
    return EXIT_SUCCESS;
}

int test_expiry() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_present_event(true, 1, 1'000);
    parser->test_present_event(true, 2, 30'999, 7);
    CHECK(parser->test_pending_count() == 2);
    // Expiry applies even between the one-second cleanup passes.
    parser->test_present_event(false, 1, 31'000);
    CHECK(parser->test_pending_count() == 1);
    CHECK(source.measure_window(1, 31'000'000'000ULL).count == 0);
    CHECK(!source.drain(31'000'000'000ULL, 1'000'000'000ULL).fps);
    parser->test_present_event(false, 2, 31'001);
    parser->test_present_event(true, 2, 31'015, 7);
    parser->test_present_event(false, 2, 31'016);
    const auto metrics = source.drain(31'016'000'000ULL, 1'000'000'000ULL);
    CHECK(metrics.fps && *metrics.fps == 62.5);
    CHECK(metrics.loss_count == 1);
    // A new start drives periodic cleanup, including orphans on other threads.
    CHECK(source.start().has_value());
    parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_present_event(true, 1, 1'000);
    parser->test_present_event(true, 2, 1'001);
    parser->test_present_event(true, 3, 31'001);
    CHECK(parser->test_pending_count() == 1);
    parser->test_present_event(false, 1, 31'002);
    CHECK(parser->test_pending_count() == 1);
    parser->test_present_event(false, 3, 31'003);
    CHECK(parser->test_pending_count() == 0);
    // Invalid clock values cannot age valid entries or produce fake frames.
    parser->test_present_event(true, 4, 32'000);
    parser->test_present_event(true, 5, 0);
    parser->test_present_event(true, 6, UINT64_MAX);
    CHECK(parser->test_pending_count() == 1);
    parser->test_present_event(false, 4, 31'999);
    CHECK(parser->test_pending_count() == 0);
    return EXIT_SUCCESS;
}

int test_event_loss() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_present_event(true, 1, 1'000);
    parser->test_present_event(true, 2, 1'001);
    parser->test_events_lost(3);
    parser->test_present_event(false, 1, 1'002);
    CHECK(parser->test_pending_count() == 0);
    parser->test_present_event(false, 2, 1'003);
    CHECK(!source.drain(1'003'000'000ULL, 1'000'000'000ULL).fps);
    // The loss delta survives discarded/failed pairs until a fresh completion.
    parser->test_present_event(true, 1, 1'010);
    parser->test_present_event(false, 1, 1'011, 1, 0, E_FAIL);
    parser->test_present_event(true, 1, 1'020, 7);
    parser->test_present_event(false, 1, 1'021);
    parser->test_present_event(true, 1, 1'036, 7);
    parser->test_present_event(false, 1, 1'037);
    const auto metrics = source.drain(1'037'000'000ULL, 1'000'000'000ULL);
    CHECK(metrics.fps && *metrics.fps == 62.5);
    CHECK(metrics.loss_count == 3);
    CHECK(metrics.quality == SampleQuality::degraded);
    // Invalidation on a new Start also removes other threads' old starts.
    parser->test_present_event(true, 2, 1'040);
    parser->test_events_lost(4);
    parser->test_present_event(true, 3, 1'041);
    CHECK(parser->test_pending_count() == 1);
    parser->test_present_event(false, 2, 1'042);
    CHECK(parser->test_pending_count() == 1);
    parser->test_present_event(false, 3, 1'043);
    CHECK(source.drain(1'043'000'000ULL, 1'000'000'000ULL).loss_count == 4);
    parser->test_present_event(true, 4, 1'050, 1, 1);
    parser->test_present_event(true, 5, 1'051, 0);
    CHECK(parser->test_pending_count() == 0);
    return EXIT_SUCCESS;
}

int test_active_pairing() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    // Two threads may complete in a different order. Keep their swap chains
    // and start timestamps separate; a duplicate Stop must add no sample.
    for (std::uint64_t frame = 0; frame < 90; ++frame) {
        const auto timestamp = 1'000 + frame * 16;
        parser->test_present_event(true, 1, timestamp, 7);
        parser->test_present_event(true, 2, timestamp + 1, 8);
        parser->test_present_event(false, 2, timestamp + 2);
        parser->test_present_event(false, 1, timestamp + 3);
        parser->test_present_event(false, 1, timestamp + 4);
        CHECK(parser->test_pending_count() == 0);
    }
    const auto metrics = source.drain(2'425'000'000ULL, 1'000'000'000ULL);
    CHECK(metrics.fps && *metrics.fps == 62.5);
    CHECK(metrics.loss_count == 0);
    CHECK(metrics.quality == SampleQuality::good);
    // A delayed but still valid call remains paired before the 30-second TTL.
    CHECK(source.start().has_value());
    parser->test_present_event(true, 1, 3'000);
    parser->test_present_event(false, 1, 32'999);
    CHECK(source.measure_window(1, 33'000'000'000ULL).count == 1);
    CHECK(parser->test_pending_count() == 0);
    return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        const std::string_view scenario{argv[1]};
        if (scenario == "--pending-capacity") return test_capacity();
        if (scenario == "--pending-expiry") return test_expiry();
        if (scenario == "--pending-loss") return test_event_loss();
        return EXIT_FAILURE;
    }
    CHECK(test_capacity() == EXIT_SUCCESS);
    CHECK(test_expiry() == EXIT_SUCCESS);
    CHECK(test_event_loss() == EXIT_SUCCESS);
    CHECK(test_active_pairing() == EXIT_SUCCESS);
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
