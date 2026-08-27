#include <cstdlib>
#include <iostream>
#include "kf2/telemetry/gpu_metrics.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    const auto adapters = enumerate_gpu_adapters();
    CHECK(adapters.has_value());
    CHECK(!adapters.value().empty());
    CHECK(!adapters.value().front().name.empty());
    CHECK(format_gpu_driver_version(0x0001000200030004ULL) == L"1.2.3.4");
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
