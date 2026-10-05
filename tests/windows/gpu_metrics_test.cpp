#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include "kf2/telemetry/gpu_metrics.hpp"
#include <pdhmsg.h>

namespace {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t raw_allocations = 0;
thread_local std::array<std::size_t, 3> raw_sizes{};
}

void* operator new(std::size_t size) {
    if (count_allocations) {
        ++allocations;
        if (std::find(raw_sizes.begin(), raw_sizes.end(), size) != raw_sizes.end())
            ++raw_allocations;
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
using namespace kf2::telemetry;
struct CounterEntry {
    std::wstring name;
    double utilization{0};
    LONGLONG memory{0};
    DWORD status{PDH_CSTATUS_VALID_DATA};
};
std::array<std::vector<CounterEntry>, 3> counter_arrays;
std::size_t parse_calls = 0;
std::size_t closed_queries = 0;
std::size_t next_counter = 0;
int failed_array = -1;
bool collection_failed = false;
bool fail_data_read = false;

PDH_STATUS WINAPI open_query(LPCWSTR, DWORD_PTR, PDH_HQUERY* query) {
    *query = reinterpret_cast<PDH_HQUERY>(100);
    next_counter = 0;
    return ERROR_SUCCESS;
}
PDH_STATUS WINAPI add_counter(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER* counter) {
    *counter = reinterpret_cast<PDH_HCOUNTER>(++next_counter);
    return ERROR_SUCCESS;
}
PDH_STATUS WINAPI collect_query(PDH_HQUERY query) {
    return !query || collection_failed ? PDH_INVALID_HANDLE : ERROR_SUCCESS;
}
PDH_STATUS WINAPI close_query(PDH_HQUERY) {
    ++closed_queries;
    return ERROR_SUCCESS;
}
PDH_STATUS WINAPI read_counter(PDH_HCOUNTER counter, DWORD format,
    LPDWORD bytes, LPDWORD count, PPDH_FMT_COUNTERVALUE_ITEM_W items) {
    const auto index = reinterpret_cast<std::uintptr_t>(counter) - 1;
    if (index >= counter_arrays.size()) return PDH_INVALID_HANDLE;
    if (failed_array == static_cast<int>(index)) return PDH_INVALID_HANDLE;
    const auto& entries = counter_arrays[index];
    if (entries.empty()) return PDH_NO_DATA;
    std::size_t required = entries.size() * sizeof(*items);
    for (const auto& entry : entries) required += (entry.name.size() + 1) * sizeof(wchar_t);
    raw_sizes[index] = required;
    *count = static_cast<DWORD>(entries.size());
    if (!items || *bytes < required) {
        *bytes = static_cast<DWORD>(required);
        return PDH_MORE_DATA;
    }
    if (fail_data_read) return PDH_MORE_DATA;
    auto* name = reinterpret_cast<wchar_t*>(items + entries.size());
    for (std::size_t at = 0; at < entries.size(); ++at) {
        const auto& entry = entries[at];
        std::memcpy(name, entry.name.c_str(), (entry.name.size() + 1) * sizeof(wchar_t));
        items[at].szName = name;
        items[at].FmtValue.CStatus = entry.status;
        if (format == PDH_FMT_DOUBLE) items[at].FmtValue.doubleValue = entry.utilization;
        else items[at].FmtValue.largeValue = entry.memory;
        name += entry.name.size() + 1;
    }
    *bytes = static_cast<DWORD>(required);
    return ERROR_SUCCESS;
}

std::vector<GpuCounterValue> reference_counters() {
    std::vector<GpuCounterValue> values;
    for (std::size_t kind = 0; kind < counter_arrays.size(); ++kind) {
        for (const auto& entry : counter_arrays[kind]) {
            if (entry.status != PDH_CSTATUS_VALID_DATA && entry.status != PDH_CSTATUS_NEW_DATA)
                continue;
            const auto identity = parse_gpu_instance(entry.name);
            if (!identity) continue;
            const auto memory = static_cast<std::uint64_t>(std::max<LONGLONG>(0, entry.memory));
            values.push_back({*identity, kind == 0 ? entry.utilization : 0,
                kind == 1 ? memory : 0, kind == 2 ? memory : 0});
        }
    }
    return values;
}

bool equivalent_metrics(const GpuMetrics& actual, std::uint32_t pid, std::uint64_t luid) {
    const auto values = reference_counters();
    const auto expected = aggregate_gpu_counters(values, pid, luid);
    const auto adapter = aggregate_adapter_gpu_percent(values, luid);
    const bool valid_adapter = adapter && expected.reason != UnavailableReason::source_failure;
    return actual.gpu_percent == expected.gpu_percent && actual.dedicated_bytes == expected.dedicated_bytes &&
        actual.shared_bytes == expected.shared_bytes && actual.adapter_gpu_percent == adapter &&
        actual.process_adapter_luid == active_process_gpu_adapter_luid(values, pid, luid) &&
        actual.quality == (valid_adapter ? SampleQuality::good : expected.quality) &&
        actual.reason == (valid_adapter ? UnavailableReason::none : expected.reason);
}

int test_reused_pdh_samples() {
    struct ResetApi {
        ~ResetApi() { detail::set_pdh_gpu_api_for_testing({}); }
    } reset;
    detail::PdhGpuApi api;
    api.open = open_query;
    api.add = add_counter;
    api.collect = collect_query;
    api.array = read_counter;
    api.close = close_query;
    api.before_parse = [](std::wstring_view) { ++parse_calls; };
    detail::set_pdh_gpu_api_for_testing(api);
    constexpr std::uint64_t luid = 0xF0F0F0F000F1F1F1ULL;
    constexpr auto memory_name = L"pid_4242_luid_0xf0f0f0f0_0x00f1f1f1";
    counter_arrays = {};
    counter_arrays[0] = {
        {L"pid_4242_luid_0xf0f0f0f0_0x00f1f1f1_phys_0_eng_0_engtype_3D", 35},
        {L"pid_7777_luid_0xf0f0f0f0_0x00f1f1f1_phys_0_eng_0_engtype_3D", 40},
        {L"pid_4242_luid_0x2_0x3_phys_0_eng_0_engtype_3D", 99},
        {L"pid_4242_luid_0xf0f0f0f0_0x00f1f1f1_phys_0_eng_1_engtype_Copy", 20},
        {L"bad-name", 99},
        {L"pid_4242_luid_0xf0f0f0f0_0x00f1f1f1_phys_1_eng_1_engtype_3D", 99, 0, PDH_CSTATUS_INVALID_DATA},
    };
    counter_arrays[1] = {{memory_name, 0, 3000}, {memory_name, 0, 2000}};
    counter_arrays[2] = {{memory_name, 0, 500, PDH_CSTATUS_NEW_DATA},
        {L"pid_4242_luid_0x2_0x3", 0, -10}};
    auto sampler = PdhGpuSampler::create(4242, luid);
    CHECK(sampler.has_value());
    const auto warmup = sampler.value().sample();
    CHECK(warmup.has_value());
    CHECK(!warmup.value().gpu_percent);
    const auto first = sampler.value().sample();
    CHECK(first.has_value());
    CHECK(equivalent_metrics(first.value(), 4242, luid));
    CHECK(sampler.value().cached_instance_count_for_testing() == 7);
    parse_calls = 0;
    allocations = raw_allocations = 0;
    count_allocations = true;
    const auto second = sampler.value().sample();
    count_allocations = false;
    CHECK(second.has_value());
    if (parse_calls != 0) std::cerr << "Repeated PDH parsing: " << parse_calls << '\n';
    CHECK(parse_calls == 0);
    if (raw_allocations != 0)
        std::cerr << "Steady allocations: " << allocations << ", raw buffers: " << raw_allocations << '\n';
    CHECK(raw_allocations == 0);
    CHECK(equivalent_metrics(second.value(), 4242, luid));

    // Cached identities must never cache amounts or suppress validation.
    counter_arrays[0][0].utilization = 80;
    counter_arrays[1][0].memory = 4500;
    counter_arrays[2][0].memory = 700;
    auto changed = sampler.value().sample();
    CHECK(changed.has_value());
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    CHECK(changed.value().gpu_percent == 80);
    CHECK(changed.value().dedicated_bytes == 4500);
    CHECK(changed.value().shared_bytes == 700);
    counter_arrays[0][0].utilization = 101;
    changed = sampler.value().sample();
    CHECK(changed.has_value());
    CHECK(changed.value().reason == UnavailableReason::source_failure);
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    counter_arrays[0][0].utilization = 35;

    for (const auto failed : {0, 1, 2}) {
        failed_array = failed;
        CHECK(!sampler.value().sample().has_value());
    }
    failed_array = -1;
    fail_data_read = true;
    CHECK(!sampler.value().sample().has_value());
    fail_data_read = false;
    collection_failed = true;
    CHECK(!sampler.value().sample().has_value());
    collection_failed = false;
    changed = sampler.value().sample();
    CHECK(changed.has_value());
    CHECK(equivalent_metrics(changed.value(), 4242, luid));

    auto moved = std::move(sampler.value());
    CHECK(sampler.value().cached_instance_count_for_testing() == 0);
    CHECK(!sampler.value().sample().has_value());
    parse_calls = 0;
    CHECK(moved.sample().has_value());
    CHECK(parse_calls == 0);
    auto other = PdhGpuSampler::create(7777, luid);
    CHECK(other.has_value());
    CHECK(other.value().sample().has_value());
    parse_calls = 0;
    const auto other_metrics = other.value().sample();
    CHECK(other_metrics.has_value());
    CHECK(parse_calls == 7); // New query owns a new cache, not the first query's.
    CHECK(equivalent_metrics(other_metrics.value(), 7777, luid));
    const auto closed_before_move = closed_queries;
    other.value() = std::move(moved);
    CHECK(closed_queries == closed_before_move + 1);
    parse_calls = 0;
    CHECK(other.value().sample().has_value());
    CHECK(parse_calls == 0);

    // Grow beyond the cache ceiling without dropping any valid counters. Old
    // entries stay cached; overflow and overlong names retain uncached parsing.
    const auto retained = other.value().cached_instance_count_for_testing();
    counter_arrays = {};
    for (std::size_t at = 0; at < 5000; ++at) {
        counter_arrays[0].push_back({L"pid_" + std::to_wstring(100000 + at) +
            L"_luid_0xf0f0f0f0_0x00f1f1f1_phys_0_eng_0_engtype_3D", 0.01});
    }
    changed = other.value().sample();
    CHECK(changed.has_value());
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    CHECK(other.value().cached_instance_count_for_testing() == 4096);
    parse_calls = 0;
    changed = other.value().sample();
    CHECK(changed.has_value());
    CHECK(parse_calls == 5000 - (4096 - retained));
    CHECK(other.value().cached_instance_count_for_testing() == 4096);
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    counter_arrays = {};
    counter_arrays[0] = {{memory_name + std::wstring{L"_"} + std::wstring(513, L'x'), 0}};
    parse_calls = 0;
    changed = other.value().sample();
    CHECK(changed.has_value());
    CHECK(parse_calls == 1);
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    auto long_names = PdhGpuSampler::create(4242, luid);
    CHECK(long_names.has_value());
    CHECK(long_names.value().sample().has_value());
    parse_calls = 0;
    CHECK(long_names.value().sample().has_value());
    CHECK(long_names.value().sample().has_value());
    CHECK(parse_calls == 2);
    CHECK(long_names.value().cached_instance_count_for_testing() == 0);
    counter_arrays = {};
    changed = other.value().sample();
    CHECK(changed.has_value());
    CHECK(equivalent_metrics(changed.value(), 4242, luid));
    CHECK(!changed.value().gpu_percent && !changed.value().adapter_gpu_percent);
    // Keep the unchanged parser's numeric limits, LUID truncation, suffixes and
    // missing engine indices identical when records go through the cache.
    counter_arrays[0] = {
        {L"pid_00004242_luid_0x100000001_0x100000002_phys_0_eng_0_engtype_3D", 10},
        {L"pid_4242_luid_0xFFFFFFFFFFFFFFFF_0xFFFFFFFFFFFFFFFF_phys_0_eng_1_engtype_Copy", 20},
        {L"pid_4294967296_luid_0x1_0x2_phys_0_eng_2_engtype_3D", 99},
        {L"pid_4242_luid_0x10000000000000000_0x2", 99},
        {L"pid_4242_luid_0x1_0x2_engtype_3D", 5},
        {L"pid_4242_luid_0x1_0x2_phys_4294967296_eng_0_engtype_3D", 99},
        {L"PID_4242_luid_0x1_0x2_phys_0_eng_0_engtype_3D", 99},
    };
    auto limits = PdhGpuSampler::create(4242, 0);
    CHECK(limits.has_value());
    CHECK(limits.value().sample().has_value());
    changed = limits.value().sample();
    CHECK(changed.has_value());
    CHECK(equivalent_metrics(changed.value(), 4242, 0));
    parse_calls = 0;
    changed = limits.value().sample();
    CHECK(changed.has_value());
    CHECK(parse_calls == 0);
    CHECK(equivalent_metrics(changed.value(), 4242, 0));
    return EXIT_SUCCESS;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    using namespace kf2::telemetry;
    // Optional read-only probe of the real Windows preference boundary.
    if (argc == 2) {
        const auto configured = configured_gpu_adapter_for_process(argv[1]);
        CHECK(configured.has_value());
        std::cout << "CONFIGURED_GPU_PREFERENCE="
                  << process_gpu_preference_token(configured.value().preference)
                  << '\n';
        return EXIT_SUCCESS;
    }
    CHECK(test_reused_pdh_samples() == EXIT_SUCCESS);
    const auto adapters = enumerate_gpu_adapters();
    CHECK(adapters.has_value());
    CHECK(!adapters.value().empty());
    CHECK(!adapters.value().front().name.empty());
    CHECK(format_gpu_driver_version(0x0001000200030004ULL) == L"1.2.3.4");
    CHECK(parse_windows_gpu_preference(L"GpuPreference=0;") ==
          ProcessGpuPreference::unspecified);
    CHECK(parse_windows_gpu_preference(L" GPUPreference = 1 ;") ==
          ProcessGpuPreference::minimum_power);
    CHECK(parse_windows_gpu_preference(L"GpuPreference=2;") ==
          ProcessGpuPreference::high_performance);
    CHECK(!parse_windows_gpu_preference(L"GpuPreference=9;").has_value());
    CHECK(!parse_windows_gpu_preference(L"broken").has_value());
    CHECK(parse_windows_gpu_preference(L"AutoHDREnable=2097;") ==
          ProcessGpuPreference::unspecified);
    CHECK(parse_windows_gpu_preference(L"SwapEffectUpgradeEnable=1;") ==
          ProcessGpuPreference::unspecified);
    CHECK(parse_windows_gpu_preference(L"") == ProcessGpuPreference::unspecified);
    CHECK(parse_windows_gpu_preference(
              L"AutoHDREnable=2097;GpuPreference=1;SwapEffectUpgradeEnable=1;") ==
          ProcessGpuPreference::minimum_power);
    CHECK(parse_windows_gpu_preference(L"GpuPreference=2;AutoHDREnable=2097;") ==
          ProcessGpuPreference::high_performance);
    CHECK(parse_windows_gpu_preference(L"OtherGpuPreference=2;") ==
          ProcessGpuPreference::unspecified);
    CHECK(parse_windows_gpu_preference(L"Other=GpuPreference=2;") ==
          ProcessGpuPreference::unspecified);
    for (const auto malformed : {
             L"GpuPreference=", L"GpuPreference=20;", L"GpuPreference=2junk;",
             L"GpuPreference=2 0;", L"GpuPreference=;AutoHDREnable=2097;",
             L"GpuPreference=1;GpuPreference=2;", L"GpuPreference=2;GpuPreference=2;",
             L"GpuPreference;AutoHDREnable=2097;", L"=2;"}) {
        CHECK(!parse_windows_gpu_preference(malformed).has_value());
    }
    for (const auto& adapter : adapters.value()) {
        if (adapter.umd_driver_version) {
            CHECK(!format_gpu_driver_version(*adapter.umd_driver_version).empty());
        }
    }
    GpuAdapter first{};
    first.luid = 1;
    first.name = L"Identical GPU";
    first.physical_device_key = L"PCI\\DEVICE_A";
    GpuAdapter same_physical = first;
    same_physical.luid = 2;
    GpuAdapter separate_physical = first;
    separate_physical.luid = 3;
    separate_physical.physical_device_key = L"PCI\\DEVICE_B";
    GpuAdapter software = first;
    software.luid = 4;
    software.software = true;
    const auto physical = unique_physical_gpu_adapters(
        {first, same_physical, separate_physical, software});
    CHECK(physical.size() == 2);
    CHECK(physical[0].luid == 1);
    CHECK(physical[1].luid == 3);
    const auto exact_adapter = find_hardware_gpu_adapter_by_luid(
        {first, same_physical, separate_physical, software}, 2);
    CHECK(exact_adapter.has_value());
    CHECK(exact_adapter->luid == 2);
    CHECK(exact_adapter->name == L"Identical GPU");
    CHECK(!find_hardware_gpu_adapter_by_luid(
        {first, same_physical, separate_physical, software}, 4).has_value());
    CHECK(!find_hardware_gpu_adapter_by_luid(
        {first, same_physical, separate_physical, software}, 99).has_value());
    GpuAdapter integrated = first;
    integrated.luid = 10;
    integrated.name = L"AMD Radeon(TM) Graphics";
    integrated.dedicated_memory_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    integrated.physical_device_key = L"PCI\\AMD_APU";
    GpuAdapter discrete = first;
    discrete.luid = 11;
    discrete.name = L"NVIDIA GeForce RTX 4090";
    discrete.dedicated_memory_bytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
    discrete.physical_device_key = L"PCI\\RTX_4090";
    const auto selected_integrated = find_unique_hardware_gpu_adapter_by_name(
        {integrated, discrete}, L"  amd radeon(tm) graphics  ");
    CHECK(selected_integrated.has_value());
    CHECK(selected_integrated->luid == integrated.luid);
    CHECK(!find_unique_hardware_gpu_adapter_by_name(
        {integrated, discrete}, L"Unknown GPU").has_value());
    GpuAdapter duplicate_integrated = integrated;
    duplicate_integrated.luid = 12;
    duplicate_integrated.physical_device_key = L"PCI\\SECOND_AMD_APU";
    CHECK(!find_unique_hardware_gpu_adapter_by_name(
        {integrated, duplicate_integrated, discrete}, integrated.name)
               .has_value());
    const auto active_adapter = active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 18.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 74.0, 0, 0},
        {{7777, 3, L"3D", 0, 0}, 99.0, 0, 0},
    }, 4242);
    CHECK(active_adapter.has_value());
    CHECK(*active_adapter == 2);
    const auto hybrid_adapter = active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 18.0, 0, 0},
        {{4242, 2, L"Copy", 0, 1}, 92.0, 0, 0},
    }, 4242);
    CHECK(hybrid_adapter.has_value());
    CHECK(*hybrid_adapter == 1);
    CHECK(!active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 0.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 0.0, 0, 0},
    }, 4242).has_value());
    const auto stable_hybrid_adapter = active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 81.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 14.0, 0, 0},
    }, 4242, 2);
    CHECK(stable_hybrid_adapter.has_value());
    CHECK(*stable_hybrid_adapter == 2);
    const auto inactive_preferred_adapter = active_process_gpu_adapter_luid({
        {{4242, 1, L"Copy", 0, 0}, 81.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 14.0, 0, 0},
    }, 4242, 1);
    CHECK(inactive_preferred_adapter.has_value());
    CHECK(*inactive_preferred_adapter == 2);
    const auto higher_memory_renderer = active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 81.0, 256, 0},
        {{4242, 2, L"3D", 0, 0}, 14.0, 4096, 0},
    }, 4242, 1);
    CHECK(higher_memory_renderer.has_value());
    CHECK(*higher_memory_renderer == 2);
    constexpr std::uint64_t gib = 1ULL << 30U;
    constexpr auto maximum_memory =
        (std::numeric_limits<std::uint64_t>::max)();
    std::vector<GpuCounterValue> split_memory{
        {{4242, 1, L"3D", 0, 0}, 30.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 30.0, 0, 0},
        {{4242, 1, L"memory"}, 0.0, 2 * gib, 0},
        {{4242, 1, L"memory"}, 0.0, 0, 2 * gib},
        {{4242, 2, L"memory"}, 0.0, 3 * gib, 0},
        {{7777, 2, L"memory"}, 0.0, maximum_memory, 0},
        {{4242, 0, L"memory"}, 0.0, maximum_memory, 0},
        {{4242, 3, L"memory"}, 0.0, maximum_memory, 0},
    };
    // PDH supplies the two memory categories separately. Four GiB must beat
    // three GiB, including when the smaller allocation was bound previously.
    CHECK(active_process_gpu_adapter_luid(split_memory, 4242) == 1);
    CHECK(active_process_gpu_adapter_luid(split_memory, 4242, 1) == 1);
    CHECK(active_process_gpu_adapter_luid(split_memory, 4242, 2) == 1);
    std::reverse(split_memory.begin(), split_memory.end());
    CHECK(active_process_gpu_adapter_luid(split_memory, 4242, 2) == 1);
    // Duplicate/lower category records must not inflate or reduce the total.
    split_memory.push_back({{4242, 2, L"memory"}, 0.0, 3 * gib, 0});
    split_memory.push_back({{4242, 1, L"memory"}, 0.0, gib, gib});
    CHECK(active_process_gpu_adapter_luid(split_memory, 4242, 2) == 1);
    CHECK(active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 30.0, 2 * gib, 2 * gib},
        {{4242, 2, L"3D", 0, 0}, 30.0, 3 * gib, 0},
    }, 4242, 2) == 1);
    // If memory counters are absent/zero, utilization spikes alone must not
    // displace the already bound 3D renderer.
    for (const auto other_load : {0.0, 99.0}) {
        CHECK(active_process_gpu_adapter_luid({
            {{4242, 1, L"3D", 0, 0}, 30.0, 0, 0},
            {{4242, 2, L"3D", 0, 0}, other_load, 0, 0},
        }, 4242, 1) == 1);
        CHECK(active_process_gpu_adapter_luid({
            {{4242, 1, L"3D", 0, 0}, 30.0, 0, 0},
            {{4242, 2, L"3D", 0, 0}, other_load, 0, 0},
            {{4242, 1, L"memory"}, 0.0, 0, 0},
            {{4242, 2, L"memory"}, 0.0, 0, 0},
        }, 4242, 1) == 1);
    }

    std::vector<GpuCounterValue> overflowing_memory{
        {{4242, 1, L"3D", 0, 0}, 30.0, 0, 0},
        {{4242, 2, L"3D", 0, 0}, 30.0, 0, 0},
        {{4242, 1, L"memory"}, 0.0, maximum_memory - 2, 8},
        {{4242, 2, L"memory"}, 0.0, maximum_memory - 1, 0},
    };
    CHECK(active_process_gpu_adapter_luid(overflowing_memory, 4242, 2) == 1);
    overflowing_memory.push_back({{4242, 2, L"memory"}, 0.0, 0, 2});
    CHECK(active_process_gpu_adapter_luid(overflowing_memory, 4242, 2) == 2);
    CHECK(!active_process_gpu_adapter_luid(overflowing_memory, 4242));
    // A later strictly better candidate clears an earlier ambiguous tie.
    CHECK(active_process_gpu_adapter_luid({
        {{4242, 1, L"3D", 0, 0}, 30.0, 2 * gib, 0},
        {{4242, 2, L"3D", 0, 0}, 30.0, 2 * gib, 0},
        {{4242, 3, L"3D", 0, 0}, 30.0, 3 * gib, 0},
    }, 4242) == 3);
    CHECK(!adapter_luid_for_window(nullptr).has_value());
    CHECK(!query_gpu_memory_budget(0).has_value());
    const auto parsed = parse_gpu_instance(
        L"pid_4242_luid_0x00000001_0x00000002_phys_0_eng_3_engtype_3D");
    CHECK(parsed.has_value());
    CHECK(parsed->pid == 4242);
    CHECK(parsed->adapter_luid == 0x0000000100000002ULL);
    CHECK(parsed->physical_index == 0);
    CHECK(parsed->engine_index == 3);
    CHECK(!parse_gpu_instance(L"pid_bad_luid_0x1_0x2").has_value());

    const std::vector<GpuCounterValue> values{
        {*parsed, 35.0, 0, 0},
        {*parsed, 80.0, 0, 0},
        {{4242, parsed->adapter_luid, L"Copy"}, 20.0, 0, 0},
        {{7777, parsed->adapter_luid, L"3D"}, 99.0, 0, 0},
        {{4242, 9, L"3D"}, 99.0, 0, 0},
        {{4242, parsed->adapter_luid, L"memory"}, 0.0, 3'000, 2'000},
    };
    const auto metrics = aggregate_gpu_counters(values, 4242, parsed->adapter_luid);
    CHECK(metrics.gpu_percent.has_value());
    CHECK(*metrics.gpu_percent == 80.0);
    CHECK(metrics.dedicated_bytes == 3'000);
    CHECK(metrics.shared_bytes == 2'000);
    CHECK(metrics.quality == SampleQuality::good);

    const auto none = aggregate_gpu_counters(values, 123, parsed->adapter_luid);
    CHECK(!none.gpu_percent.has_value());
    CHECK(none.reason == UnavailableReason::no_samples);
    const auto malformed = aggregate_gpu_counters(
        {{{4242, parsed->adapter_luid, L"3D"}, -1.0, 0, 0}},
        4242, parsed->adapter_luid);
    CHECK(!malformed.gpu_percent.has_value());
    CHECK(malformed.reason == UnavailableReason::source_failure);

    const std::vector<GpuCounterValue> adapter_values{
        {{4242, parsed->adapter_luid, L"3D", 0, 3}, 35.0, 0, 0},
        {{7777, parsed->adapter_luid, L"3D", 0, 3}, 40.0, 0, 0},
        {{4242, parsed->adapter_luid, L"Copy", 0, 4}, 20.0, 0, 0},
        {{9999, 9, L"3D", 0, 3}, 99.0, 0, 0},
    };
    const auto adapter_percent = aggregate_adapter_gpu_percent(
        adapter_values, parsed->adapter_luid);
    CHECK(adapter_percent.has_value());
    CHECK(*adapter_percent == 75.0);
    CHECK(!aggregate_adapter_gpu_percent(
        {{{4242, parsed->adapter_luid, L"3D", 0, 3}, -1.0, 0, 0}},
        parsed->adapter_luid).has_value());
    CHECK(choose_total_gpu_percent(71.0, 19.0) == 71.0);
    CHECK(choose_total_gpu_percent(std::nullopt, 44.0) == 44.0);
    CHECK(!choose_total_gpu_percent(std::nullopt, std::nullopt).has_value());
    CHECK(!choose_total_gpu_percent(101.0, std::nullopt).has_value());

    constexpr std::uint64_t first_adapter = 0x100;
    constexpr std::uint64_t second_adapter = 0x200;
    constexpr std::uint64_t second = 1'000'000'000ULL;
    GpuUtilizationFilter filter;
    auto filtered = filter.update({second, first_adapter, 35.0, 40.0});
    CHECK(!filtered.decision_ready);
    CHECK(filtered.adapter_luid == first_adapter);
    filtered = filter.update({second + 500'000'000ULL, first_adapter,
                              36.0, 41.0});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 36.0);
    CHECK(filtered.adapter_percent == 41.0);
    CHECK(filtered.continuity_samples == 2);

    // One implausible edge sample is rejected instead of immediately changing
    // the value Adaptive sees.
    filtered = filter.update({second + 1'000'000'000ULL, first_adapter,
                              100.0, 100.0});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 36.0);
    CHECK(filtered.adapter_percent == 41.0);
    CHECK(filtered.sample_age_ns == 500'000'000ULL);

    // Sustained saturation is accepted on the second agreeing observation,
    // bounding detection delay to one normal sampling interval.
    filtered = filter.update({second + 1'500'000'000ULL, first_adapter,
                              100.0, 100.0});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 100.0);
    CHECK(filtered.adapter_percent == 100.0);
    CHECK(filtered.confidence >= 0.55);

    GpuUtilizationFilter zero_spike_filter;
    static_cast<void>(zero_spike_filter.update(
        {second, first_adapter, 70.0, 80.0}));
    filtered = zero_spike_filter.update(
        {second + 500'000'000ULL, first_adapter, 72.0, 82.0});
    CHECK(filtered.decision_ready);
    filtered = zero_spike_filter.update(
        {second + 1'000'000'000ULL, first_adapter, 0.0, 0.0});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 72.0);
    CHECK(filtered.adapter_percent == 82.0);

    GpuUtilizationFilter dropout_filter;
    static_cast<void>(dropout_filter.update(
        {second, first_adapter, 70.0, 80.0}));
    filtered = dropout_filter.update(
        {second + 500'000'000ULL, first_adapter, 72.0, 82.0});
    CHECK(filtered.decision_ready);
    filtered = dropout_filter.update(
        {second + 1'000'000'000ULL, first_adapter, std::nullopt,
         std::nullopt});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 72.0);
    filtered = dropout_filter.update(
        {second + 3'000'000'001ULL, first_adapter, std::nullopt,
         std::nullopt});
    CHECK(!filtered.decision_ready);
    CHECK(!filtered.process_percent.has_value());
    CHECK(!filtered.adapter_percent.has_value());
    CHECK(filtered.confidence < 0.55);

    // A different physical adapter never inherits the previous adapter's
    // history or confidence.
    filtered = filter.update({second + 2'000'000'000ULL, second_adapter,
                              20.0, 25.0});
    CHECK(!filtered.decision_ready);
    CHECK(filtered.adapter_luid == second_adapter);
    filtered = filter.update({second + 2'500'000'000ULL, second_adapter,
                              21.0, 26.0});
    CHECK(filtered.decision_ready);
    CHECK(filtered.process_percent == 21.0);
    CHECK(filtered.adapter_percent == 26.0);
    CHECK(filtered.continuity_samples == 2);

    for (const auto& adapter : unique_physical_gpu_adapters(adapters.value())) {
        const auto memory = query_gpu_memory_budget(adapter.luid);
        if (memory.has_value()) {
            CHECK(memory.value().budget_bytes > 0);
            CHECK(memory.value().current_usage_bytes <=
                  memory.value().budget_bytes * 2);
        }
        if (adapter.vendor_id != 0x10DE) continue;
        auto driver = NvidiaGpuSampler::create(adapter.name);
        if (driver.has_value()) {
            const auto sample = driver.value().sample();
            CHECK(sample.has_value());
            CHECK(sample.value() >= 0.0);
            CHECK(sample.value() <= 100.0);
            std::cout << "NVIDIA_DRIVER_GPU_SOURCE="
                      << (driver.value().source() ==
                                  NvidiaGpuSource::nvapi_dynamic_pstates
                              ? "NVAPI_DYNAMIC_PSTATES"
                              : "NVML")
                      << '\n';
            std::cout << "NVIDIA_DRIVER_GPU_PERCENT=" << sample.value() << '\n';
        }
    }

    auto sampler = PdhGpuSampler::create(GetCurrentProcessId(), 0);
    if (sampler.has_value()) {
        const auto warmup = sampler.value().sample();
        CHECK(warmup.has_value());
        CHECK(!warmup.value().gpu_percent.has_value());
        Sleep(30);
        CHECK(sampler.value().sample().has_value());
    }
    return EXIT_SUCCESS;
}
