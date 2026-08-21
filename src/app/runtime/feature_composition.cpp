#include "app/runtime/feature_composition.hpp"

#include <array>

#include "features/backup/backup_actions.hpp"
#include "features/diagnostics/diagnostics_actions.hpp"
#include "features/game/game_actions.hpp"
#include "features/navigation/navigation_actions.hpp"
#include "features/optimizer/optimizer_actions.hpp"
#include "features/overlay/overlay_actions.hpp"
#include "features/settings/settings_actions.hpp"

namespace kf2::app::runtime {
namespace {

constexpr std::array<FeatureDefinition, 7> kFeatures{{
    ::kf2::features::navigation::kFeature,
    ::kf2::features::overlay::kFeature,
    ::kf2::features::diagnostics::kFeature,
    ::kf2::features::backup::kFeature,
    ::kf2::features::optimizer::kFeature,
    ::kf2::features::settings::kFeature,
    ::kf2::features::game::kFeature,
}};

}  // namespace

std::span<const FeatureDefinition> feature_definitions() noexcept {
    return kFeatures;
}

const FeatureDefinition* find_feature(FeatureId id) noexcept {
    return find_feature_definition(id, kFeatures);
}

}  // namespace kf2::app::runtime
