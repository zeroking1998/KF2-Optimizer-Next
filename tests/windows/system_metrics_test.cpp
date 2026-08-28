#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include "kf2/game/game_session.hpp"
#include "kf2/telemetry/system_metrics.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
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

    wchar_t path[MAX_PATH]{};
    CHECK(GetModuleFileNameW(nullptr, path, MAX_PATH) > 0);
    const auto identity = kf2::game::bind_game_process(GetCurrentProcessId(), path);
    CHECK(identity.has_value());
    ProcessMetricSampler sampler{identity.value()};
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
    auto stale = identity.value(); ++stale.process_start_id;
    CHECK(!ProcessMetricSampler{stale}.sample().has_value());
    return EXIT_SUCCESS;
}
