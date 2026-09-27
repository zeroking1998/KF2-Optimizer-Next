#include "app/runtime/action_router.hpp"

namespace kf2::app::runtime {

DispatchResult dispatch_action(
    ::kf2::app::UiRuntime& runtime, const ActionRequest& request,
    std::span<const FeatureDefinition> features) noexcept {
    const auto* contract = find_action(request.id);
    if (contract == nullptr) return DispatchResult::unknown_action;
    if (!payload_matches(contract->payload_kind, request.payload)) {
        return DispatchResult::invalid_payload;
    }
    if (!valid_feature_registry(features)) {
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

}  // namespace kf2::app::runtime
