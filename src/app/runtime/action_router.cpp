#include "app/runtime/action_router.hpp"
#include "app/runtime/feature_composition.hpp"

namespace kf2::app::runtime {
namespace {

DispatchResult dispatch_from_registry(
    ::kf2::app::UiRuntime& runtime, const ActionRequest& request,
    std::span<const FeatureDefinition> features, bool fixed_registry) noexcept {
    const auto* contract = find_action(request.id);
    if (contract == nullptr) return DispatchResult::unknown_action;
    if (!payload_matches(contract->payload_kind, request.payload)) {
        return DispatchResult::invalid_payload;
    }
    // Only the no-span overload supplies the immutable app composition.
    // Caller-provided registries must still be checked on every dispatch.
    if (fixed_registry) {
        static const bool valid = valid_feature_registry(features);
        if (!valid) return DispatchResult::invalid_registry;
    } else if (!valid_feature_registry(features)) {
        return DispatchResult::invalid_registry;
    }

    const auto* implementation =
        find_action_implementation(request.id, features);
    if (implementation == nullptr) return DispatchResult::invalid_registry;
    try {
        return implementation->handler(runtime, request.payload);
    } catch (...) {
        return DispatchResult::handler_failure;
    }
}

}  // namespace

DispatchResult dispatch_action(
    ::kf2::app::UiRuntime& runtime, const ActionRequest& request) noexcept {
    return dispatch_from_registry(runtime, request, feature_definitions(), true);
}

DispatchResult dispatch_action(
    ::kf2::app::UiRuntime& runtime, const ActionRequest& request,
    std::span<const FeatureDefinition> features) noexcept {
    return dispatch_from_registry(runtime, request, features, false);
}

}  // namespace kf2::app::runtime
