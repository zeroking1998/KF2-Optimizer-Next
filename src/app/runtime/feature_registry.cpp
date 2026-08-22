#include "app/runtime/feature_registry.hpp"

#include <array>

namespace kf2::app::runtime {
namespace {

constexpr std::array<FeatureId, 7> kFeatureIds{{
    FeatureId::game,
    FeatureId::settings,
    FeatureId::overlay,
    FeatureId::diagnostics,
    FeatureId::backup,
    FeatureId::graphics,
    FeatureId::advanced,
}};

bool known_feature(FeatureId id) noexcept {
    for (const auto known : kFeatureIds) {
        if (known == id) return true;
    }
    return false;
}

std::size_t contract_action_count(FeatureId owner) noexcept {
    std::size_t count = 0;
    for (const auto& contract : action_definitions()) {
        if (contract.feature == owner) ++count;
    }
    return count;
}

bool feature_contains(ActionId id,
                      const FeatureDefinition& feature) noexcept {
    for (const auto& implementation : feature.actions) {
        if (implementation.id == id) return true;
    }
    return false;
}

}  // namespace

bool valid_feature_registry(
    std::span<const FeatureDefinition> features) noexcept {
    for (std::size_t feature_index = 0; feature_index < features.size();
         ++feature_index) {
        const auto& feature = features[feature_index];
        if (!known_feature(feature.id) || feature.name.empty() ||
            feature.actions.empty()) {
            return false;
        }
        for (std::size_t previous = 0; previous < feature_index; ++previous) {
            if (features[previous].id == feature.id) return false;
        }
        if (feature.actions.size() != contract_action_count(feature.id)) {
            return false;
        }
        for (std::size_t action_index = 0;
             action_index < feature.actions.size(); ++action_index) {
            const auto& implementation = feature.actions[action_index];
            const auto* contract = find_action(implementation.id);
            if (contract == nullptr || contract->feature != feature.id ||
                implementation.handler == nullptr) {
                return false;
            }
            for (std::size_t previous = 0; previous < action_index;
                 ++previous) {
                if (feature.actions[previous].id == implementation.id) {
                    return false;
                }
            }
            for (std::size_t previous_feature = 0;
                 previous_feature < feature_index; ++previous_feature) {
                if (feature_contains(implementation.id,
                                     features[previous_feature])) {
                    return false;
                }
            }
        }
        for (const auto& contract : action_definitions()) {
            if (contract.feature == feature.id &&
                !feature_contains(contract.id, feature)) {
                return false;
            }
        }
    }

    if (features.size() != kFeatureIds.size()) return false;
    for (const auto id : kFeatureIds) {
        if (find_feature_definition(id, features) == nullptr) return false;
    }
    for (const auto& contract : action_definitions()) {
        if (find_action_implementation(contract.id, features) == nullptr) {
            return false;
        }
    }
    return true;
}

const ActionImplementation* find_action_implementation(
    ActionId id, std::span<const FeatureDefinition> features) noexcept {
    for (const auto& feature : features) {
        for (const auto& implementation : feature.actions) {
            if (implementation.id == id) return &implementation;
        }
    }
    return nullptr;
}

const FeatureDefinition* find_feature_definition(
    FeatureId id, std::span<const FeatureDefinition> features) noexcept {
    for (const auto& feature : features) {
        if (feature.id == id) return &feature;
    }
    return nullptr;
}

bool payload_matches(ActionPayloadKind kind,
                     const ActionPayload& payload) noexcept {
    switch (kind) {
        case ActionPayloadKind::no_payload:
            return std::holds_alternative<NoPayload>(payload);
    }
    return false;
}

}  // namespace kf2::app::runtime
