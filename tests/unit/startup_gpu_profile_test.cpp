#include "kf2/optimizer/startup_gpu_profile.hpp"

#include <iostream>
#include <utility>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; ++failures; \
} } while (false)

kf2::telemetry::GpuAdapter adapter(
    std::uint64_t luid, std::wstring name, std::uint64_t gibibytes,
    std::wstring key) {
    return {luid, std::move(name), gibibytes * 1024ULL * 1024ULL * 1024ULL,
            0, 0, std::nullopt, false, std::move(key)};
}
}  // namespace

int main() {
    using namespace kf2::optimizer;
    const auto integrated = adapter(11, L"Integrated GPU", 2, L"PCI\\IGPU");
    const auto discrete = adapter(22, L"Discrete GPU", 24, L"PCI\\DGPU");
    const std::vector hybrid{integrated, discrete};

    const auto to_integrated = resolve_startup_gpu_profile(
        hybrid, L"PCI\\IGPU", L"PCI\\DGPU", false);
    CHECK(to_integrated.has_value());
    CHECK(to_integrated->source == StartupGpuProfileSource::configured_adapter);
    CHECK(to_integrated->adapter && to_integrated->adapter->luid == 11);
    CHECK(to_integrated->profile.texture_pool_size_mb == 1000);

    const auto to_discrete = resolve_startup_gpu_profile(
        hybrid, L"PCI\\DGPU", L"PCI\\IGPU", false);
    CHECK(to_discrete.has_value());
    CHECK(to_discrete->adapter && to_discrete->adapter->luid == 22);
    CHECK(to_discrete->profile.texture_pool_size_mb == 6000);

    const auto unknown = resolve_startup_gpu_profile(
        hybrid, std::nullopt, L"PCI\\DGPU", false);
    CHECK(unknown.has_value());
    CHECK(unknown->source ==
          StartupGpuProfileSource::conservative_cross_adapter);
    CHECK(!unknown->adapter.has_value());
    CHECK(unknown->profile.texture_pool_size_mb == 1000);

    const auto unchanged = resolve_startup_gpu_profile(
        hybrid, std::nullopt, L"PCI\\DGPU", true);
    CHECK(unchanged.has_value());
    CHECK(unchanged->source ==
          StartupGpuProfileSource::previously_confirmed_adapter);
    CHECK(unchanged->adapter && unchanged->adapter->luid == 22);

    const std::vector same_name{
        adapter(31, L"Same GPU", 8, L"PCI\\FIRST"),
        adapter(32, L"Same GPU", 8, L"PCI\\SECOND")};
    const auto same_name_selected = resolve_startup_gpu_profile(
        same_name, L"PCI\\SECOND", std::nullopt, false);
    CHECK(same_name_selected.has_value());
    CHECK(same_name_selected->adapter &&
          same_name_selected->adapter->luid == 32);

    const std::vector sole{discrete};
    const auto sole_selected = resolve_startup_gpu_profile(
        sole, std::nullopt, std::nullopt, false);
    CHECK(sole_selected.has_value());
    CHECK(sole_selected->source == StartupGpuProfileSource::sole_adapter);
    const auto zero_memory = resolve_startup_gpu_profile(
        {adapter(41, L"Shared-memory GPU", 0, L"PCI\\SHARED")},
        std::nullopt, std::nullopt, false);
    CHECK(zero_memory.has_value());
    CHECK(zero_memory->profile.texture_pool_size_mb == 160);
    CHECK(!resolve_startup_gpu_profile(
        {}, std::nullopt, std::nullopt, false).has_value());

    return failures == 0 ? 0 : 1;
}
