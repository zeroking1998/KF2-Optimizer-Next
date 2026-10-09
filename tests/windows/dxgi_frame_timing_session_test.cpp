#include <Windows.h>
#include <evntrace.h>

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "kf2/platform/windows/dxgi_frame_timing_session.hpp"
#include "../support/process_inspection_denial.hpp"

namespace {
thread_local bool fail_next_allocation{};
thread_local unsigned int allocation_failures{};
}

void* operator new(std::size_t size) {
    if (std::exchange(fail_next_allocation, false)) {
        ++allocation_failures;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
using kf2::platform::windows::DxgiFrameTimingSession;
using namespace kf2::telemetry;
constexpr SampleIdentity kIdentity{123, 1};
constexpr std::uint64_t kFrequency = 1'000;

int test_exact_clock_conversion() {
    struct Case { std::uint64_t frequency, ticks, expected_ns; };
    for (const auto [frequency, ticks, expected_ns] : {
            Case{1'000'000'000ULL, 12'345'678'901'245ULL, 12'345'678'901'245ULL},
            Case{10'000'000ULL, 12'345'678'901'245ULL, 1'234'567'890'124'500ULL},
            Case{3, 7, 2'333'333'333ULL},
            Case{3, 30'000'000'001ULL, 10'000'000'000'333'333'333ULL},
            Case{2'000'000'000ULL, 12'345'678'901'245ULL, 6'172'839'450'622ULL},
            Case{10'000'000ULL, 184'467'440'737'095'516ULL,
                 18'446'744'073'709'551'600ULL},
            Case{1, 9'223'372'036'854'775'806ULL, 0},
            Case{0, 1, 0}}) {
        PresentSource source{kIdentity, 8};
        CHECK(source.start().has_value());
        auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, frequency);
        parser->test_present_event(true, 1, ticks, 7);
        parser->test_present_event(false, 1, ticks + 1);
        if (expected_ns == 0) {
            CHECK(source.measure_window(0, UINT64_MAX).count == 0);
        } else {
            CHECK(source.measure_window(expected_ns, expected_ns + 1).count == 1);
        }
    }
    return EXIT_SUCCESS;
}

ULONG cleanup_query_status{ERROR_SUCCESS};
ULONG cleanup_reported_count{64};
ULONG cleanup_control_status{ERROR_SUCCESS};
int cleanup_malformed{};
bool cleanup_arguments_valid{};
std::vector<std::wstring> cleanup_stops;
std::vector<std::wstring> cleanup_names;
constexpr wchar_t kDeadSession[] = L"KF2OptimizerNext-DXGI-4294967295";
constexpr wchar_t kLastSession[] = L"KF2OptimizerNext-DXGI-4294967291";

std::uint64_t current_creation_time() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
        creation.dwLowDateTime;
}

std::wstring current_session_name(std::uint64_t creation) {
    return L"KF2OptimizerNext-DXGI-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(creation);
}

ULONG WINAPI query_cleanup_sessions(
    PEVENT_TRACE_PROPERTIES* sessions, ULONG capacity, PULONG count) {
    cleanup_arguments_valid = sessions && count && capacity == 64;
    if (!cleanup_arguments_valid) return ERROR_INVALID_PARAMETER;
    for (ULONG index = 0; index < capacity; ++index) {
        auto* properties = sessions[index];
        const auto name = index < cleanup_names.size() ? cleanup_names[index]
            : index == 0 ? std::wstring{kDeadSession}
            : index == capacity - 1 ? std::wstring{kLastSession}
            : index == 1 ? L"KF2OptimizerNext-DXGI-" + std::to_wstring(GetCurrentProcessId())
                         : std::wstring{L"Unrelated-ETW-session"};
        auto* destination = reinterpret_cast<wchar_t*>(
            reinterpret_cast<std::byte*>(properties) + properties->LoggerNameOffset);
        std::copy(name.begin(), name.end(), destination);
        destination[name.size()] = L'\0';
        if (index != 0) continue;
        switch (cleanup_malformed) {
        case 1: properties->LoggerNameOffset = 0; break;
        case 2: properties->LoggerNameOffset = properties->Wnode.BufferSize; break;
        case 3: ++properties->LoggerNameOffset; break;
        case 4: {
            const auto characters = (properties->Wnode.BufferSize -
                properties->LoggerNameOffset) / sizeof(wchar_t);
            std::fill(destination + name.size(), destination + characters, L'X');
            break;
        }
        case 5: destination[name.size()] = L'x'; destination[name.size() + 1] = L'\0'; break;
        case 6: destination[22] = L'-'; break;
        case 7: properties->Wnode.BufferSize = 0; break;
        case 8: std::fill(destination + 22, destination + 43, L'9'); destination[43] = L'\0'; break;
        }
    }
    *count = cleanup_reported_count;
    return cleanup_query_status;
}

ULONG WINAPI control_cleanup_session(
    TRACEHANDLE session, LPCWSTR name, PEVENT_TRACE_PROPERTIES properties,
    ULONG operation) {
    cleanup_arguments_valid = cleanup_arguments_valid && session == 0 && name &&
        properties && properties->Wnode.BufferSize == sizeof(EVENT_TRACE_PROPERTIES) &&
        operation == EVENT_TRACE_CONTROL_STOP;
    if (name) cleanup_stops.emplace_back(name);
    return cleanup_control_status;
}

int test_stale_cleanup_overflow() {
    // No real QueryAllTraces/ControlTrace calls: even stale/foreign session
    // fixtures cannot stop another application's actual tracing session.
    for (const auto status : {ERROR_MORE_DATA, ERROR_SUCCESS,
                             ERROR_ACCESS_DENIED, ERROR_INVALID_PARAMETER}) {
        for (const ULONG count : {0UL, 1UL, 64UL, 96UL, std::numeric_limits<ULONG>::max()}) {
            cleanup_query_status = status;
            cleanup_reported_count = count;
            cleanup_control_status = ERROR_SUCCESS;
            cleanup_malformed = 0;
            cleanup_stops.clear();
            DxgiFrameTimingSession::test_cleanup_stale_sessions(
                query_cleanup_sessions, control_cleanup_session);
            CHECK(cleanup_arguments_valid);
            const bool usable = status == ERROR_SUCCESS || status == ERROR_MORE_DATA;
            const std::size_t expected = !usable || count == 0 ? 0 : count < 64 ? 1 : 2;
            CHECK(cleanup_stops.size() == expected);
            if (expected != 0) CHECK(cleanup_stops.front() == kDeadSession);
            if (expected == 2) CHECK(cleanup_stops.back() == kLastSession);
        }
    }
    for (int malformed = 1; malformed <= 8; ++malformed) {
        for (const ULONG count : {1UL, 64UL}) {
            cleanup_query_status = ERROR_SUCCESS;
            cleanup_reported_count = count;
            cleanup_malformed = malformed;
            cleanup_stops.clear();
            DxgiFrameTimingSession::test_cleanup_stale_sessions(
                query_cleanup_sessions, control_cleanup_session);
            CHECK(cleanup_arguments_valid);
            CHECK(cleanup_stops.size() == (count == 1 ? 0U : 1U));
            if (!cleanup_stops.empty()) CHECK(cleanup_stops.front() == kLastSession);
        }
    }
    cleanup_query_status = ERROR_MORE_DATA;
    cleanup_reported_count = 96;
    cleanup_malformed = 0;
    cleanup_control_status = ERROR_ACCESS_DENIED;
    cleanup_stops.clear();
    DxgiFrameTimingSession::test_cleanup_stale_sessions(
        query_cleanup_sessions, control_cleanup_session);
    CHECK(cleanup_arguments_valid && cleanup_stops.size() == 2);
    return EXIT_SUCCESS;
}

int test_stale_cleanup_pid_reuse() {
    const auto creation = current_creation_time();
    CHECK(creation > 1);
    const auto legacy = L"KF2OptimizerNext-DXGI-" +
        std::to_wstring(GetCurrentProcessId());
    const auto live = current_session_name(creation);
    const auto orphan = current_session_name(creation - 1);
    cleanup_names = {orphan, live, legacy,
        std::wstring{kDeadSession} + L"-123",
        std::wstring{kLastSession} + L"-123",
        legacy + L"-", legacy + L"-0", legacy + L"--1",
        legacy + L"-123x", legacy + L"-18446744073709551616",
        live + L"-1", L"KF2OptimizerNext-DXGI-0-123",
        L"Other-owner-123"};
    cleanup_query_status = ERROR_SUCCESS;
    cleanup_reported_count = static_cast<ULONG>(cleanup_names.size());
    cleanup_control_status = ERROR_SUCCESS;
    cleanup_malformed = 0;
    cleanup_stops.clear();
    DxgiFrameTimingSession::test_cleanup_stale_sessions(
        query_cleanup_sessions, control_cleanup_session);
    CHECK(cleanup_arguments_valid);
    CHECK(cleanup_stops == (std::vector<std::wstring>{orphan,
        std::wstring{kDeadSession} + L"-123",
        std::wstring{kLastSession} + L"-123"}));
    {
        kf2::test::ProcessInspectionDenial denied;
        CHECK(denied.deny(GetCurrentProcess()));
        const auto process = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, GetCurrentProcessId());
        const auto error = GetLastError();
        if (process) CloseHandle(process);
        CHECK(!process && error == ERROR_ACCESS_DENIED);
        cleanup_stops.clear();
        DxgiFrameTimingSession::test_cleanup_stale_sessions(
            query_cleanup_sessions, control_cleanup_session);
        // Without verified ownership, preserve even a same-PID candidate.
        CHECK(cleanup_stops == (std::vector<std::wstring>{
            std::wstring{kDeadSession} + L"-123",
            std::wstring{kLastSession} + L"-123"}));
        CHECK(denied.restore());
    }
    cleanup_names.clear();
    return EXIT_SUCCESS;
}

struct OwnedTrace {
    std::wstring name;
    TRACEHANDLE handle{};
    struct Properties {
        EVENT_TRACE_PROPERTIES header{};
        wchar_t name[128]{};
    } properties;

    explicit OwnedTrace(std::wstring session_name) : name{std::move(session_name)} {
        properties.header.Wnode.BufferSize = sizeof(properties);
        properties.header.Wnode.ClientContext = 1;
        properties.header.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties.header.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        properties.header.LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    }
    OwnedTrace(const OwnedTrace&) = delete;
    OwnedTrace& operator=(const OwnedTrace&) = delete;
    ULONG start() {
        TRACEHANDLE owned{};
        const auto status = StartTraceW(&owned, name.c_str(), &properties.header);
        if (status == ERROR_SUCCESS) handle = owned;
        return status;
    }
    ULONG query() {
        return ControlTraceW(0, name.c_str(), &properties.header,
                             EVENT_TRACE_CONTROL_QUERY);
    }
    ~OwnedTrace() {
        if (handle != 0) {
            // Stop only the handle this fixture successfully created.
            static_cast<void>(ControlTraceW(handle, nullptr, &properties.header,
                EVENT_TRACE_CONTROL_STOP));
        }
    }
};

int test_real_orphan_startup() {
    const auto creation = current_creation_time();
    CHECK(creation > 1);
    OwnedTrace orphan{current_session_name(creation - 1)};
    OwnedTrace legacy{L"KF2OptimizerNext-DXGI-" +
        std::to_wstring(GetCurrentProcessId())};
    CHECK(orphan.start() == ERROR_SUCCESS);
    CHECK(legacy.start() == ERROR_SUCCESS);
    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 120};
    CHECK(source.start().has_value());
    auto session = DxgiFrameTimingSession::start(identity, source);
    CHECK(session.has_value());
    CHECK(orphan.query() == ERROR_WMI_INSTANCE_NOT_FOUND);
    CHECK(legacy.query() == ERROR_SUCCESS);
    const auto duplicate = DxgiFrameTimingSession::start(identity, source);
    CHECK(!duplicate.has_value());
    CHECK(duplicate.error().native_code == ERROR_ALREADY_EXISTS);
    OwnedTrace live{current_session_name(creation)};
    CHECK(live.query() == ERROR_SUCCESS);
    CHECK(session.value()->stop().has_value());
    CHECK(live.query() == ERROR_WMI_INSTANCE_NOT_FOUND);
    auto retry = DxgiFrameTimingSession::start(identity, source);
    CHECK(retry.has_value());
    CHECK(retry.value()->is_running());
    CHECK(live.query() == ERROR_SUCCESS);
    CHECK(ControlTraceW(0, live.name.c_str(), &live.properties.header,
        EVENT_TRACE_CONTROL_STOP) == ERROR_SUCCESS);
    const auto stopped_at = GetTickCount64();
    while (retry.value()->is_running() && GetTickCount64() - stopped_at < 2'000)
        Sleep(1);
    CHECK(!retry.value()->is_running());
    CHECK(retry.value()->stop().has_value());
    CHECK(!retry.value()->is_running());
    CHECK(legacy.query() == ERROR_SUCCESS);
    return EXIT_SUCCESS;
}

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
    CHECK(metrics.loss_count == 1'745);
    // Admission evicts only the oldest pending start, preserving fresh pairs.
    parser->test_present_event(false, 1'746, 3'028);
    CHECK(parser->test_pending_count() == 254);
    return EXIT_SUCCESS;
}

int test_filtered_starts() {
    for (const auto [swap_chain, flags] : {
            std::pair{7ULL, 1U}, std::pair{0ULL, 0U}}) {
        for (const bool outstanding : {false, true}) {
            PresentSource source{kIdentity, 120};
            CHECK(source.start().has_value());
            auto parser = DxgiFrameTimingSession::test_parser(
                kIdentity, source, kFrequency);
            if (outstanding) parser->test_present_event(true, 1, 1'000, 9);
            parser->test_present_event(true, 1, 1'001, swap_chain, flags);
            const auto pending = parser->test_pending_count();
            parser->test_present_event(false, 1, 1'002);
            parser->test_present_event(false, 1, 1'003);
            // A filtered call's Stop cannot complete a previous real Start.
            CHECK(source.measure_window(1, 1'003'000'000ULL).count == 0);
            CHECK(pending == 0);
            for (const auto at : {1'010ULL, 1'026ULL}) {
                parser->test_present_event(true, 1, at, 7);
                parser->test_present_event(false, 1, at + 1);
            }
            const auto metrics = source.drain(1'027'000'000ULL, 1'000'000'000ULL);
            CHECK(metrics.fps && *metrics.fps == 62.5);
            CHECK(metrics.loss_count == (outstanding ? 1U : 0U));
            CHECK(metrics.quality == (outstanding
                ? SampleQuality::degraded : SampleQuality::good));
            parser->test_present_event(true, 1, 1'042, 7);
            parser->test_present_event(false, 1, 1'043);
            CHECK(source.drain(1'043'000'000ULL, 1'000'000'000ULL).loss_count ==
                  (outstanding ? 1U : 0U));
            // A fresh post-loss window recovers without another reset.
            for (const auto at : {12'010ULL, 12'026ULL}) {
                parser->test_present_event(true, 1, at, 7);
                parser->test_present_event(false, 1, at + 1);
            }
            const auto recovered = source.drain(
                12'027'000'000ULL, 1'000'000'000ULL);
            CHECK(recovered.loss_count == 0);
            CHECK(recovered.quality == SampleQuality::good);
        }
    }
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

int test_callback_loss() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_present_event(true, 1, 1'000, 7);
    parser->test_present_event(true, 2, 1'000, 8);
    DxgiFrameTimingSession::test_fail_next_event_callback();
    parser->test_present_event(false, 1, 1'001);
    CHECK(parser->test_pending_count() == 0);
    parser->test_present_event(false, 2, 1'002);
    CHECK(source.measure_window(0, 1'002'000'000ULL).count == 0);
    // ETW invokes the buffer callback after its event callbacks. Its unused
    // EventsLost field must not erase a locally observed callback failure.
    parser->test_unused_buffer_loss(0);
    parser->test_present_event(true, 1, 1'020, 7);
    parser->test_present_event(false, 1, 1'021);
    parser->test_present_event(true, 1, 1'036, 7);
    parser->test_present_event(false, 1, 1'037);
    const auto metrics = source.drain(1'037'000'000ULL, 1'000'000'000ULL);
    CHECK(metrics.fps && *metrics.fps == 62.5);
    // One observed callback failure, not an estimate of lost Present events.
    CHECK(metrics.loss_count == 1);
    CHECK(metrics.quality == SampleQuality::degraded);
    parser->test_unused_buffer_loss(0);
    parser->test_present_event(true, 1, 1'052, 7);
    parser->test_present_event(false, 1, 1'053);
    CHECK(source.drain(1'053'000'000ULL, 1'000'000'000ULL).loss_count == 1);
    parser->test_present_event(true, 1, 12'000, 7);
    parser->test_present_event(false, 1, 12'001);
    parser->test_present_event(true, 1, 12'016, 7);
    parser->test_present_event(false, 1, 12'017);
    const auto recovered = source.drain(12'017'000'000ULL, 1'000'000'000ULL);
    CHECK(recovered.fps && *recovered.fps == 62.5);
    CHECK(recovered.loss_count == 0);
    CHECK(recovered.quality == SampleQuality::good);
    return EXIT_SUCCESS;
}

int test_partial_loss_commit() {
    PresentSource source{kIdentity, 120};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_events_lost(3);
    parser->test_present_event(true, 1, 1'000, 7);
    parser->test_present_event(true, 2, 31'001, 7);
    CHECK(parser->test_pending_count() == 1);
    allocation_failures = 0;
    fail_next_allocation = true;
    parser->test_present_event(false, 2, 31'002);
    CHECK(allocation_failures == 1 && !fail_next_allocation);
    CHECK(parser->test_pending_count() == 0);
    parser->test_present_event(true, 3, 31'020, 7);
    parser->test_present_event(false, 3, 31'021);
    parser->test_present_event(true, 3, 31'036, 7);
    parser->test_present_event(false, 3, 31'037);
    const auto measured = source.drain(31'037'000'000ULL, 1'000'000'000ULL);
    CHECK(measured.fps && *measured.fps == 62.5);
    // Three observed OS losses, one expired pair, one callback failure;
    // this is not an estimate of the number of missing Presents.
    CHECK(measured.loss_count == 5);
    CHECK(measured.quality == SampleQuality::degraded);
    return EXIT_SUCCESS;
}

int test_loss_acknowledgment() {
    PresentSource source{kIdentity, 120};
    CHECK(!source.record_loss(kIdentity, 1'000'000'000ULL, 3));
    CHECK(source.start().has_value());
    CHECK(!source.record_loss({999, 2}, 1'000'000'000ULL, 3));
    CHECK(!source.record_loss(kIdentity, 1'000'000'000ULL, 0));
    CHECK(source.ingest({kIdentity, 1'000'000'000ULL, 1, true, 0, 7}));
    CHECK(source.ingest({kIdentity, 1'016'000'000ULL, 1, true, 0, 7}));
    CHECK(source.drain(1'017'000'000ULL, 1'000'000'000ULL).loss_count == 0);
    allocation_failures = 0;
    fail_next_allocation = true;
    const auto committed = source.record_loss(kIdentity, 1'017'000'000ULL, 4);
    const auto no_allocation = fail_next_allocation;
    fail_next_allocation = false;
    CHECK(committed && no_allocation && allocation_failures == 0);
    // Loss acknowledgment is independent of duplicate sample rejection.
    CHECK(!source.ingest({kIdentity, 1'016'000'000ULL, 1, true, 0, 7}));
    const auto marked = source.drain(1'017'000'000ULL, 1'000'000'000ULL);
    CHECK(marked.loss_count == 4 && marked.quality == SampleQuality::degraded);
    CHECK(!source.ingest({kIdentity, 1'032'000'000ULL, 0, true, 0, 7}));
    CHECK(!source.record_loss(kIdentity, 1'032'000'000ULL, 3));

    // A rejected loss transfer retains parser debt until the source is ready.
    CHECK(source.start().has_value());
    CHECK(source.stop().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    parser->test_events_lost(3);
    parser->test_present_event(true, 1, 1'000, 7);
    parser->test_present_event(false, 1, 1'001);
    CHECK(source.start().has_value());
    parser->test_present_event(true, 1, 1'020, 7);
    parser->test_present_event(false, 1, 1'021);
    parser->test_present_event(true, 1, 1'036, 7);
    parser->test_present_event(false, 1, 1'037);
    const auto ready = source.drain(1'037'000'000ULL, 1'000'000'000ULL);
    CHECK(ready.fps && *ready.fps == 62.5);
    CHECK(ready.loss_count == 3 && ready.quality == SampleQuality::degraded);
    CHECK(source.stop().has_value());
    parser->test_session_statistics(ERROR_SUCCESS, 3, 1);
    parser->test_present_event(true, 1, 1'050, 7);
    parser->test_present_event(false, 1, 1'051);
    CHECK(source.start().has_value());
    for (const auto at : {1'064ULL, 1'080ULL}) {
        parser->test_present_event(true, 1, at, 7);
        parser->test_present_event(false, 1, at + 1);
    }
    const auto buffer_debt = source.drain(1'081'000'000ULL, 1'000'000'000ULL);
    CHECK(buffer_debt.fps && *buffer_debt.fps == 62.5);
    CHECK(buffer_debt.loss_count == 0 && buffer_debt.quality == SampleQuality::degraded);
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

int test_session_loss_statistics() {
    for (const bool buffer_only : {false, true}) {
        PresentSource source{kIdentity, 128};
        CHECK(source.start().has_value());
        auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
        parser->test_present_event(true, 1, 1'000, 7);
        parser->test_present_event(false, 1, 1'001);
        parser->test_present_event(true, 1, 1'016, 7);
        parser->test_present_event(false, 1, 1'017);
        const auto before = source.measure_window(1'000'000'000ULL, 1'016'000'000ULL);
        CHECK(before.complete);
        parser->test_present_event(true, 2, 1'018, 7);
        parser->test_session_statistics(ERROR_SUCCESS, buffer_only ? 0 : 3,
                                         buffer_only ? 1 : 0);
        // Failed output and the callback's unused field cannot erase statistics.
        parser->test_session_statistics(ERROR_MORE_DATA, 0, 0);
        parser->test_session_statistics(ERROR_ACCESS_DENIED, 99, 99);
        parser->test_unused_buffer_loss(0);
        parser->test_unused_buffer_loss(99);
        parser->test_present_event(false, 2, 1'019);
        CHECK(parser->test_pending_count() == 0);
        CHECK(source.measure_window(1'018'000'000ULL, 1'019'000'000ULL).count == 0);
        for (const auto at : {1'032ULL, 1'048ULL}) {
            parser->test_present_event(true, 1, at, 7);
            parser->test_present_event(false, 1, at + 1);
        }
        const auto marked = source.drain(1'049'000'000ULL, 1'000'000'000ULL);
        CHECK(marked.fps && *marked.fps == 62.5);
        CHECK(marked.quality == SampleQuality::degraded);
        CHECK(marked.loss_count == (buffer_only ? 0 : 3));
        const auto crossed = source.measure_window(1'000'000'000ULL, 1'048'000'000ULL);
        CHECK(crossed.count == 0 && !crossed.complete);
        CHECK(crossed.generation != before.generation);
        parser->test_present_event(true, 1, 1'064, 7);
        parser->test_present_event(false, 1, 1'065);
        const auto unchanged = source.drain(1'065'000'000ULL, 1'000'000'000ULL);
        CHECK(unchanged.loss_count == marked.loss_count);
        CHECK(unchanged.source_generation == marked.source_generation);
        for (const auto at : {12'000ULL, 12'016ULL}) {
            parser->test_present_event(true, 1, at, 7);
            parser->test_present_event(false, 1, at + 1);
        }
        const auto recovered = source.drain(12'017'000'000ULL, 1'000'000'000ULL);
        CHECK(recovered.fps && *recovered.fps == 62.5);
        CHECK(recovered.loss_count == 0 && recovered.quality == SampleQuality::good);
        CHECK(source.measure_window(12'000'000'000ULL, 12'016'000'000ULL).complete);
    }
    PresentSource source{kIdentity, 128};
    CHECK(source.start().has_value());
    auto parser = DxgiFrameTimingSession::test_parser(kIdentity, source, kFrequency);
    struct Counters { std::uint32_t events, buffers; std::uint64_t expected; };
    std::uint64_t at = 1'000;
    for (const auto [events, buffers, expected] : {
            Counters{UINT32_MAX, 0, UINT32_MAX},
            Counters{0, 0, 1ULL + UINT32_MAX},
            Counters{0, UINT32_MAX, 1ULL + UINT32_MAX},
            Counters{0, 0, 1ULL + UINT32_MAX},
            Counters{1, 1, 2ULL + UINT32_MAX}}) {
        parser->test_session_statistics(ERROR_SUCCESS, events, buffers);
        for (unsigned int frame = 0; frame < 2; ++frame, at += 16) {
            parser->test_present_event(true, 1, at, 7);
            parser->test_present_event(false, 1, at + 1);
        }
        const auto marked = source.drain((at - 15) * 1'000'000ULL, 1'000'000'000ULL);
        CHECK(marked.fps && *marked.fps == 62.5);
        CHECK(marked.loss_count == expected);
        CHECK(marked.quality == SampleQuality::degraded);
    }
    return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        const std::string_view scenario{argv[1]};
        if (scenario == "--stale-session-overflow") return test_stale_cleanup_overflow();
        if (scenario == "--pid-reuse") return test_stale_cleanup_pid_reuse();
        if (scenario == "--orphan-startup") return test_real_orphan_startup();
        if (scenario == "--pending-capacity") return test_capacity();
        if (scenario == "--pending-expiry") return test_expiry();
        if (scenario == "--pending-loss") return test_event_loss();
        if (scenario == "--filtered-starts") return test_filtered_starts();
        if (scenario == "--callback-loss") return test_callback_loss();
        if (scenario == "--partial-loss-commit") return test_partial_loss_commit();
        if (scenario == "--loss-acknowledgment") return test_loss_acknowledgment();
        if (scenario == "--session-loss-statistics") return test_session_loss_statistics();
        return EXIT_FAILURE;
    }
    CHECK(test_exact_clock_conversion() == EXIT_SUCCESS);
    CHECK(test_stale_cleanup_overflow() == EXIT_SUCCESS);
    CHECK(test_stale_cleanup_pid_reuse() == EXIT_SUCCESS);
    CHECK(test_real_orphan_startup() == EXIT_SUCCESS);
    CHECK(test_capacity() == EXIT_SUCCESS);
    CHECK(test_filtered_starts() == EXIT_SUCCESS);
    CHECK(test_expiry() == EXIT_SUCCESS);
    CHECK(test_event_loss() == EXIT_SUCCESS);
    CHECK(test_callback_loss() == EXIT_SUCCESS);
    CHECK(test_partial_loss_commit() == EXIT_SUCCESS);
    CHECK(test_loss_acknowledgment() == EXIT_SUCCESS);
    CHECK(test_session_loss_statistics() == EXIT_SUCCESS);
    CHECK(test_active_pairing() == EXIT_SUCCESS);
    using namespace kf2::telemetry;
    using kf2::platform::windows::DxgiFrameTimingSession;
    CHECK(kf2::platform::windows::DxgiFrameTimingSession::
        test_event_callback_exception_boundary());
    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 120};
    CHECK(source.start().has_value());

    const auto session_name = current_session_name(current_creation_time());
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
    CHECK(session.value()->test_flush_statistics() == ERROR_SUCCESS);
    CHECK(session.value()->stop().has_value());
    CHECK(session.value()->stop().has_value());
    CHECK(source.stop().has_value());
    return EXIT_SUCCESS;
}
