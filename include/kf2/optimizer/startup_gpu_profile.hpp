#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/telemetry/gpu_metrics.hpp"

namespace kf2::optimizer {

enum class StartupGpuProfileSource {
    sole_adapter,
    configured_adapter,
    previously_confirmed_adapter,
    conservative_cross_adapter,
};

struct StartupGpuProfileResolution {
    StartupMemoryProfile profile;
    std::optional<telemetry::GpuAdapter> adapter;
    StartupGpuProfileSource source{
        StartupGpuProfileSource::conservative_cross_adapter};
};

[[nodiscard]] std::optional<StartupGpuProfileResolution>
resolve_startup_gpu_profile(
    const std::vector<telemetry::GpuAdapter>& physical_adapters,
    std::optional<std::wstring_view> configured_physical_key,
    std::optional<std::wstring_view> previously_confirmed_physical_key,
    bool previous_confirmation_matches_current_preference) noexcept;

[[nodiscard]] std::wstring_view startup_gpu_profile_source_label(
    StartupGpuProfileSource source) noexcept;

}  // namespace kf2::optimizer
