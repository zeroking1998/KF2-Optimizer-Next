#pragma once

#include <span>

#include "app/runtime/feature_registry.hpp"

namespace kf2::app::runtime {

DispatchResult dispatch_action(
    ::kf2::app::UiRuntime& runtime, const ActionRequest& request,
    std::span<const FeatureDefinition> features) noexcept;

}  // namespace kf2::app::runtime
