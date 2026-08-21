#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>

#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    std::set<std::pair<std::uint64_t, std::wstring>> baseline_adapters;
    for (int refresh = 0; refresh < 3; ++refresh) {
        const auto hardware = query_hardware_inventory();
        const auto memory = query_system_memory_metrics();
        const auto adapters = enumerate_gpu_adapters();
        CHECK(hardware.has_value());
        CHECK(memory.has_value());
        CHECK(adapters.has_value());
        CHECK(memory.value().available_physical_bytes <=
              memory.value().total_physical_bytes);
        CHECK(memory.value().used_percent >= 0.0 &&
              memory.value().used_percent <= 100.0);
        std::set<std::pair<std::uint64_t, std::wstring>> current_adapters;
        for (const auto& adapter : adapters.value()) {
            current_adapters.emplace(adapter.luid, adapter.name);
        }
        CHECK(!current_adapters.empty());
        if (refresh == 0) baseline_adapters = std::move(current_adapters);
        else CHECK(current_adapters == baseline_adapters);
    }
    return EXIT_SUCCESS;
}
