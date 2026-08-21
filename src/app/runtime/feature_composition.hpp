#pragma once

#include <span>

#include "app/runtime/feature_registry.hpp"

namespace kf2::app::runtime {

std::span<const FeatureDefinition> feature_definitions() noexcept;

const FeatureDefinition* find_feature(FeatureId id) noexcept;

}  // namespace kf2::app::runtime
