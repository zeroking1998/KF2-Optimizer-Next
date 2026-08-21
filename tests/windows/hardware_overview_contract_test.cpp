#include <cstdlib>
#include <iostream>

#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    const auto hardware = query_hardware_inventory();
    CHECK(hardware.has_value());
    CHECK(hardware.value().logical_processors >= 1);
    CHECK(hardware.value().processor_groups >= 1);
    CHECK(hardware.value().installed_memory_bytes > 0);
    CHECK(hardware.value().available_memory_bytes <=
          hardware.value().installed_memory_bytes);

    const auto adapters = enumerate_gpu_adapters();
    CHECK(adapters.has_value());
    CHECK(!adapters.value().empty());
    bool physical_adapter = false;
    for (const auto& adapter : adapters.value()) {
        CHECK(!adapter.name.empty());
        if (!adapter.software) physical_adapter = true;
        if (adapter.umd_driver_version) {
            CHECK(!format_gpu_driver_version(*adapter.umd_driver_version).empty());
        }
    }
    CHECK(physical_adapter);
    CHECK(!unique_physical_gpu_adapters(adapters.value()).empty());
    return EXIT_SUCCESS;
}
