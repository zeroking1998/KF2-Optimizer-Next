#include <Windows.h>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vector>
#include "kf2/game/game_session.hpp"
#include "kf2/telemetry/system_metrics.hpp"
#include "../support/process_inspection_denial.hpp"

namespace {
thread_local bool fail_next_cache_allocation = false;
bool cache_fault_stage_seen = false;
kf2::telemetry::detail::ThreadCacheAllocationStage selected_fault_stage{};

void arm_cache_allocation_failure(
    kf2::telemetry::detail::ThreadCacheAllocationStage stage) {
    if (stage != selected_fault_stage) return;
    kf2::telemetry::detail::set_thread_cache_allocation_hook_for_testing(nullptr);
    cache_fault_stage_seen = true;
    fail_next_cache_allocation = true;
}
}

void* operator new(std::size_t size) {
    if (fail_next_cache_allocation && size >= 32) {
        fail_next_cache_allocation = false;
        throw std::bad_alloc{};
    }
    for (;;) {
        if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc{};
        handler();
    }
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    using detail::CpuCapacityObservation;
    using detail::CpuSetQueryState;
    using detail::ProcessorGroupMask;

    const auto logical_capacity = [](const auto& masks) {
        std::uint32_t total = 0;
        for (const auto& mask : masks) {
            total += static_cast<std::uint32_t>(std::popcount(mask.mask));
        }
        return total;
    };
    const std::vector<ProcessorGroupMask> two_group_system{
        {0, 0xff}, {1, 0x3f}};

    CpuCapacityObservation single_group{
        .system_group_masks = {{0, 0xff}},
        .process_groups = std::vector<std::uint16_t>{0},
        .primary_group_affinity = ProcessorGroupMask{0, 0x0f}};
    const auto single_group_capacity =
        detail::resolve_process_capacity_masks(single_group);
    CHECK(single_group_capacity.has_value());
    const std::vector<ProcessorGroupMask> single_group_expected{{0, 0x0f}};
    CHECK(*single_group_capacity == single_group_expected);
    CHECK(logical_capacity(*single_group_capacity) == 4);

    CpuCapacityObservation windows_11_default{
        .system_group_masks = two_group_system,
        .process_groups = std::vector<std::uint16_t>{0, 1},
        .primary_group_affinity = ProcessorGroupMask{0, 0xff},
        .primary_group_affinity_is_full = true,
        .default_affinity_spans_groups = true,
        .cpu_set_query = CpuSetQueryState::succeeded};
    const auto windows_11_capacity =
        detail::resolve_process_capacity_masks(windows_11_default);
    CHECK(windows_11_capacity.has_value());
    CHECK(*windows_11_capacity == two_group_system);
    CHECK(logical_capacity(*windows_11_capacity) == 14);

    auto windows_11_group_subset = windows_11_default;
    windows_11_group_subset.process_groups =
        std::vector<std::uint16_t>{1};
    windows_11_group_subset.primary_group_affinity =
        ProcessorGroupMask{1, 0x3f};
    const auto group_subset_capacity =
        detail::resolve_process_capacity_masks(windows_11_group_subset);
    const std::vector<ProcessorGroupMask> group_subset_expected{{1, 0x3f}};
    CHECK(group_subset_capacity.has_value());
    CHECK(*group_subset_capacity == group_subset_expected);

    CpuCapacityObservation explicit_cross_group{
        .system_group_masks = two_group_system,
        .process_groups = std::vector<std::uint16_t>{0, 1},
        .cpu_set_query = CpuSetQueryState::succeeded};
    const auto explicit_capacity =
        detail::resolve_process_capacity_masks(explicit_cross_group);
    CHECK(!explicit_capacity.has_value());

    auto cpu_set_limited = windows_11_default;
    cpu_set_limited.default_cpu_set_masks = {{0, 0x03}, {1, 0x0c}};
    const auto cpu_set_capacity =
        detail::resolve_process_capacity_masks(cpu_set_limited);
    CHECK(cpu_set_capacity.has_value());
    CHECK(*cpu_set_capacity == cpu_set_limited.default_cpu_set_masks);
    CHECK(logical_capacity(*cpu_set_capacity) == 4);

    auto failed_group_query = single_group;
    failed_group_query.process_groups.reset();
    CHECK(!detail::resolve_process_capacity_masks(failed_group_query));
    auto failed_cpu_set_query = windows_11_default;
    failed_cpu_set_query.cpu_set_query = CpuSetQueryState::failed;
    CHECK(!detail::resolve_process_capacity_masks(failed_cpu_set_query));

    const auto inventory = query_hardware_inventory();
    CHECK(inventory.has_value());
    CHECK(inventory.value().physical_cores >= 1);
    CHECK(inventory.value().logical_processors >= 1);
    CHECK(inventory.value().physical_cores <=
          inventory.value().logical_processors);
    CHECK(inventory.value().processor_groups >= 1);
    CHECK(inventory.value().installed_memory_bytes > 0);
    const auto memory = query_system_memory_metrics();
    CHECK(memory.has_value());
    CHECK(memory.value().total_physical_bytes > 0);
    CHECK(memory.value().available_physical_bytes <=
          memory.value().total_physical_bytes);
    CHECK(memory.value().commit_limit_bytes > 0);
    CHECK(memory.value().available_commit_bytes <=
          memory.value().commit_limit_bytes);
    CHECK(memory.value().used_percent >= 0.0 &&
          memory.value().used_percent <= 100.0);

    CHECK(!calculate_cpu_percent({100, 50}, {100, 60}).has_value());
    const auto cpu = calculate_cpu_percent({100, 50}, {200, 75});
    CHECK(cpu.has_value());
    CHECK(*cpu == 25.0);
    CHECK(calculate_cpu_percent({100, 50}, {200, 500}).value() == 100.0);
    CHECK(!calculate_system_cpu_percent({100, 50, 40}, {100, 60, 40})
               .has_value());
    CHECK(calculate_system_cpu_percent({100, 50, 40}, {200, 75, 90})
              .value() == 50.0);
    CHECK(!calculate_thread_cpu_percent(100, 110, 0).has_value());
    CHECK(!calculate_thread_cpu_percent(110, 100, 10).has_value());
    CHECK(calculate_thread_cpu_percent(100, 50'100, 100).value() == 5.0);
    CHECK(calculate_thread_cpu_percent(100, 2'000'100, 100).value() == 100.0);

    // Numeric thread IDs are not instance identities. Never subtract CPU
    // counters from different creation times, even when the new counter grew.
    using detail::ThreadCpuTimes;
    const ThreadCpuTimes old_thread{1'000, 100};
    for (const auto reused_ticks : {50ULL, 100ULL, 50'100ULL}) {
        CHECK(!detail::calculate_thread_cpu_percent(
            old_thread, ThreadCpuTimes{2'000, reused_ticks}, 100));
    }
    CHECK(!detail::calculate_thread_cpu_percent(
        ThreadCpuTimes{}, ThreadCpuTimes{0, 50'000}, 100));
    CHECK(!detail::calculate_thread_cpu_percent(
        old_thread, ThreadCpuTimes{1'000, 50'100}, 0));
    CHECK(!detail::calculate_thread_cpu_percent(
        old_thread, ThreadCpuTimes{1'000, 50}, 100));
    CHECK(detail::calculate_thread_cpu_percent(
        old_thread, ThreadCpuTimes{1'000, 50'100}, 100).value() == 5.0);
    CHECK(detail::calculate_thread_cpu_percent(
        old_thread, old_thread, 100).value() == 0.0);

    const detail::ThreadPressureMetrics pressure{
        98.0, 1.25, 78.4, 3};
    detail::ThreadPressureCache terminated_threads;
    terminated_threads.observe(pressure, 1'000);
    terminated_threads.miss(1'500);
    CHECK(terminated_threads.current().has_value());
    terminated_threads.miss(2'000);
    CHECK(terminated_threads.current().has_value());
    terminated_threads.miss(2'500);
    CHECK(!terminated_threads.current().has_value());

    // One empty GetThreadTimes sample is a transient gap, not zero pressure.
    detail::ThreadPressureCache empty_thread_times;
    empty_thread_times.observe(pressure, 3'000);
    empty_thread_times.miss(3'500);
    CHECK(empty_thread_times.current().has_value());
    CHECK(empty_thread_times.current()->critical_core_percent == 98.0);

    // Repeated thread-enumeration failures age out the previous workload.
    detail::ThreadPressureCache failed_enumeration;
    failed_enumeration.observe(pressure, 4'000);
    failed_enumeration.miss(4'500);
    failed_enumeration.miss(5'000);
    failed_enumeration.miss(5'500);
    CHECK(!failed_enumeration.current().has_value());

    wchar_t path[MAX_PATH]{};
    CHECK(GetModuleFileNameW(nullptr, path, MAX_PATH) > 0);
    const auto identity = kf2::game::bind_game_process(GetCurrentProcessId(), path);
    CHECK(identity.has_value());
    const auto own_threads = detail::query_process_thread_ids(identity.value());
    CHECK(own_threads.has_value());
    CHECK(std::find(own_threads.value().begin(), own_threads.value().end(),
                   GetCurrentThreadId()) != own_threads.value().end());
    auto snapshot_stale = identity.value();
    ++snapshot_stale.process_start_id;
    const auto stale_threads = detail::query_process_thread_ids(snapshot_stale);
    CHECK(!stale_threads.has_value());
    CHECK(stale_threads.error().code == kf2::ErrorCode::stale_data);
    detail::fail_next_process_thread_snapshot_walk_for_testing();
    const auto partial_threads = detail::query_process_thread_ids(identity.value());
    CHECK(!partial_threads.has_value());
    CHECK(partial_threads.error().code == kf2::ErrorCode::platform_failure);
    {
        detail::fail_next_process_thread_snapshot_walk_for_testing();
        ProcessMetricSampler partial_fallback{identity.value()};
        CHECK(partial_fallback.sample().has_value());
        Sleep(520);
        const auto sampled = partial_fallback.sample();
        CHECK(sampled.has_value());
        CHECK(sampled.value().critical_core_percent.has_value());
        CHECK(sampled.value().effective_core_usage.has_value());
    }
    bool cache_handles_leaked = false;
    for (const auto stage : {detail::ThreadCacheAllocationStage::snapshot_ids,
                            detail::ThreadCacheAllocationStage::cached_thread}) {
        DWORD before = 0;
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
        // Repeat beyond ambient handle noise; retries must not accumulate leaks.
        for (int attempt = 0; attempt < 8; ++attempt) {
            ProcessMetricSampler faulted{identity.value()};
            selected_fault_stage = stage;
            cache_fault_stage_seen = false;
            detail::fail_next_process_thread_snapshot_walk_for_testing();
            detail::set_thread_cache_allocation_hook_for_testing(
                &arm_cache_allocation_failure);
            bool threw = false;
            try {
                static_cast<void>(faulted.sample());
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            detail::set_thread_cache_allocation_hook_for_testing(nullptr);
            const bool injected = cache_fault_stage_seen &&
                !fail_next_cache_allocation;
            fail_next_cache_allocation = false;
            CHECK(injected && threw);
            CHECK(faulted.sample().has_value());
        }
        DWORD after = 0;
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
        if (after > before + 2) {
            std::cerr << "Cache allocation stage " << static_cast<int>(stage)
                      << " leaked " << after - before << " handles\n";
            cache_handles_leaked = true;
        }
    }
    CHECK(!cache_handles_leaked);

    ProcessMetricSampler sampler{identity.value()};
    const auto opens_before = detail::process_metric_opens_for_testing();
    const auto first = sampler.sample();
    CHECK(first.has_value());
    CHECK(first.value().working_set_bytes > 0);
    CHECK(!first.value().cpu_percent.has_value());
    Sleep(20);
    const auto second = sampler.sample();
    CHECK(second.has_value());
    CHECK(second.value().cpu_percent.has_value());
    CHECK(second.value().system_cpu_percent.has_value());
    Sleep(520);
    const auto third = sampler.sample();
    CHECK(third.has_value());
    CHECK(detail::process_metric_opens_for_testing() == opens_before);
    CHECK(third.value().critical_core_percent.has_value());
    CHECK(*third.value().critical_core_percent >= 0.0);
    CHECK(*third.value().critical_core_percent <= 100.0);
    CHECK(third.value().effective_core_usage.has_value());
    CHECK(*third.value().effective_core_usage >= 0.0);
    CHECK(third.value().dominant_thread_share_percent.has_value());
    CHECK(*third.value().dominant_thread_share_percent >= 0.0);
    CHECK(*third.value().dominant_thread_share_percent <= 100.0);
    CHECK(third.value().active_cpu_threads.has_value());
    CHECK(third.value().affinity_logical_processors.has_value());
    CHECK(*third.value().affinity_logical_processors >= 1);
    CHECK(third.value().system_logical_processors.has_value());
    CHECK(*third.value().system_logical_processors >=
          *third.value().affinity_logical_processors);
    if (third.value().affinity_physical_cores) {
        CHECK(*third.value().affinity_physical_cores >= 1);
        CHECK(*third.value().affinity_physical_cores <=
              *third.value().affinity_logical_processors);
    }
    CHECK(second.value().affinity_logical_processors ==
          third.value().affinity_logical_processors);
    CHECK(second.value().affinity_physical_cores ==
          third.value().affinity_physical_cores);
    CHECK(second.value().system_logical_processors ==
          third.value().system_logical_processors);
    auto stale = identity.value(); ++stale.process_start_id;
    CHECK(!ProcessMetricSampler{stale}.sample().has_value());
    stale = identity.value(); ++stale.pid;
    CHECK(!ProcessMetricSampler{stale}.sample().has_value());
    {
        auto detached = identity.value();
        detached.native_process.reset();
        const auto before = detail::process_metric_opens_for_testing();
        ProcessMetricSampler standalone{detached};
        CHECK(standalone.sample().has_value());
        CHECK(standalone.sample().has_value());
        auto moved = std::move(standalone);
        CHECK(moved.sample().has_value());
        CHECK(detail::process_metric_opens_for_testing() == before + 1);
    }
    {
        // Broader query rights may be denied even while the original metric
        // handle remains readable. Thread pressure must keep working then.
        std::unique_ptr<void, decltype(&CloseHandle)> owned{
            OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId()), &CloseHandle};
        CHECK(owned);
        kf2::test::ProcessInspectionDenial denial;
        CHECK(denial.deny(owned.get(), PROCESS_QUERY_LIMITED_INFORMATION |
            PROCESS_VM_READ | SYNCHRONIZE));
        const auto unavailable = detail::query_process_thread_ids(identity.value());
        CHECK(!unavailable.has_value());
        CHECK(unavailable.error().code == kf2::ErrorCode::access_denied);
        ProcessMetricSampler fallback{identity.value()};
        CHECK(fallback.sample().has_value());
        Sleep(520);
        const auto sampled = fallback.sample();
        CHECK(sampled.has_value());
        CHECK(sampled.value().critical_core_percent.has_value());
        CHECK(sampled.value().effective_core_usage.has_value());
        CHECK(denial.restore());
    }
    {
        // Denying VM_READ must not break read-only session observation.
        std::unique_ptr<void, decltype(&CloseHandle)> owned{
            OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId()), &CloseHandle};
        CHECK(owned);
        kf2::test::ProcessInspectionDenial denial;
        CHECK(denial.deny(owned.get(), PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE));
        const auto limited = kf2::game::bind_game_process(GetCurrentProcessId(), path);
        CHECK(limited.has_value());
        CHECK(limited.value().native_process);
        CHECK(!limited.value().native_process->metrics_readable());
        CHECK(kf2::game::is_game_process_current(limited.value()));
        ProcessMetricSampler restricted{limited.value()};
        CHECK(!restricted.sample().has_value());
        CHECK(denial.restore());
        const auto before = detail::process_metric_opens_for_testing();
        CHECK(restricted.sample().has_value());
        CHECK(restricted.sample().has_value());
        CHECK(detail::process_metric_opens_for_testing() == before + 1);
    }

    std::vector<std::jthread> workers;
    for (int index = 0; index < 8; ++index) {
        workers.emplace_back([](std::stop_token stop) {
            while (!stop.stop_requested()) Sleep(5);
        });
    }
    const auto worker_ids = detail::query_process_thread_ids(identity.value());
    CHECK(worker_ids.has_value());
    for (auto& worker : workers) {
        CHECK(std::find(worker_ids.value().begin(), worker_ids.value().end(),
            GetThreadId(worker.native_handle())) != worker_ids.value().end());
    }
    DWORD handles_before = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    constexpr DWORD kAmbientHandleAllowance = 2;
    {
        ProcessMetricSampler tracked{identity.value()};
        CHECK(tracked.sample().has_value());
        DWORD handles_while_tracked = 0;
        CHECK(GetProcessHandleCount(GetCurrentProcess(),
                                    &handles_while_tracked));
        CHECK(handles_while_tracked >=
              handles_before + static_cast<DWORD>(workers.size()));

        // A failed first entry or partial fallback must preserve all cached
        // workers and leave membership refresh due for the next sample.
        Sleep(5'050);
        for (const bool after_matching_entry : {true, false}) {
            detail::fail_next_process_thread_snapshot_walk_for_testing();
            detail::fail_next_toolhelp_thread_walk_for_testing(after_matching_entry);
            CHECK(tracked.sample().has_value());
            DWORD handles_after_failure = 0;
            CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after_failure));
            CHECK(handles_after_failure + kAmbientHandleAllowance >=
                  handles_while_tracked);
            Sleep(520);
        }

        for (auto& worker : workers) worker.request_stop();
        for (auto& worker : workers) worker.join();
        DWORD handles_after_exit = 0;
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after_exit));
        // Failed refreshes must not delay retry for another five seconds.
        // A complete refresh still releases exited workers' cached handles.
        CHECK(tracked.sample().has_value());
        DWORD handles_after_refresh = 0;
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after_refresh));
        CHECK(handles_after_refresh + static_cast<DWORD>(workers.size()) <=
              handles_after_exit + kAmbientHandleAllowance);
    }
    DWORD handles_after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    // The process-wide count can move by a handle or two when Windows or the
    // test runtime performs unrelated asynchronous work. Joined workers no
    // longer own the eight native handles counted before sampling started.
    CHECK(handles_after + static_cast<DWORD>(workers.size()) <=
          handles_before + kAmbientHandleAllowance);
    return EXIT_SUCCESS;
}
