#include "kf2/optimizer/startup_gpu_profile.hpp"

#include <algorithm>
#include <utility>

namespace kf2::optimizer {
namespace {

std::optional<telemetry::GpuAdapter> find_by_physical_key(
    const std::vector<telemetry::GpuAdapter>& adapters,
    std::optional<std::wstring_view> key) {
    if (!key || key->empty()) return std::nullopt;
    const auto found = std::find_if(
        adapters.begin(), adapters.end(), [key](const auto& adapter) {
            return !adapter.physical_device_key.empty() &&
                   adapter.physical_device_key == *key;
        });
    return found == adapters.end() ? std::nullopt
                                   : std::optional{*found};
}

std::optional<StartupGpuProfileResolution> resolution_for(
    telemetry::GpuAdapter adapter, StartupGpuProfileSource source) {
    constexpr std::uint64_t conservative_floor =
        1024ULL * 1024ULL * 1024ULL;
    const auto profile = recommended_startup_memory_profile(
        std::max(adapter.dedicated_memory_bytes, conservative_floor));
    if (!profile) return std::nullopt;
    return StartupGpuProfileResolution{*profile, std::move(adapter), source};
}

}  // namespace

std::optional<StartupGpuProfileResolution> resolve_startup_gpu_profile(
    const std::vector<telemetry::GpuAdapter>& physical_adapters,
    std::optional<std::wstring_view> configured_physical_key,
    std::optional<std::wstring_view> previously_confirmed_physical_key,
    bool previous_confirmation_matches_current_preference) noexcept {
    if (physical_adapters.empty()) return std::nullopt;
    if (const auto configured = find_by_physical_key(
            physical_adapters, configured_physical_key)) {
        return resolution_for(
            *configured, StartupGpuProfileSource::configured_adapter);
    }
    if (physical_adapters.size() == 1) {
        return resolution_for(
            physical_adapters.front(), StartupGpuProfileSource::sole_adapter);
    }
    if (previous_confirmation_matches_current_preference) {
        if (const auto previous = find_by_physical_key(
                physical_adapters, previously_confirmed_physical_key)) {
            return resolution_for(
                *previous,
                StartupGpuProfileSource::previously_confirmed_adapter);
        }
    }
    const auto smallest = std::min_element(
        physical_adapters.begin(), physical_adapters.end(),
        [](const auto& left, const auto& right) {
            return left.dedicated_memory_bytes < right.dedicated_memory_bytes;
        });
    constexpr std::uint64_t conservative_floor =
        1024ULL * 1024ULL * 1024ULL;
    const auto profile = recommended_startup_memory_profile(std::max(
        smallest->dedicated_memory_bytes, conservative_floor));
    if (!profile) return std::nullopt;
    return StartupGpuProfileResolution{
        *profile, std::nullopt,
        StartupGpuProfileSource::conservative_cross_adapter};
}

std::wstring_view startup_gpu_profile_source_label(
    StartupGpuProfileSource source) noexcept {
    switch (source) {
        case StartupGpuProfileSource::sole_adapter: return L"sole adapter";
        case StartupGpuProfileSource::configured_adapter:
            return L"configured adapter";
        case StartupGpuProfileSource::previously_confirmed_adapter:
            return L"previously confirmed adapter";
        case StartupGpuProfileSource::conservative_cross_adapter:
            return L"conservative cross-adapter budget";
    }
    return L"unknown";
}

}  // namespace kf2::optimizer
