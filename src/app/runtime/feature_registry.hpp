#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "app/runtime/action_contract.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::app::runtime {

enum class DispatchResult : std::uint8_t {
    handled,
    unknown_action,
    invalid_payload,
    invalid_registry,
};

using FeatureActionHandler = DispatchResult (*)(
    ::kf2::app::UiRuntime&, const ActionPayload&);

using NoPayloadHandler = DispatchResult (*)(
    ::kf2::app::UiRuntime&, const NoPayload&);

template <NoPayloadHandler Handler>
DispatchResult bind_no_payload(::kf2::app::UiRuntime& runtime,
                               const ActionPayload& payload) {
    const auto* no_payload = std::get_if<NoPayload>(&payload);
    if (no_payload == nullptr) return DispatchResult::invalid_payload;
    return Handler(runtime, *no_payload);
}

struct ActionImplementation {
    ActionId id{};
    FeatureActionHandler handler{};
};

struct FeatureDefinition {
    FeatureId id{};
    std::string_view name;
    std::span<const ActionImplementation> actions;
};

bool valid_feature_registry(
    std::span<const FeatureDefinition> features) noexcept;

const ActionImplementation* find_action_implementation(
    ActionId id, std::span<const FeatureDefinition> features) noexcept;

const FeatureDefinition* find_feature_definition(
    FeatureId id, std::span<const FeatureDefinition> features) noexcept;

bool payload_matches(ActionPayloadKind kind,
                     const ActionPayload& payload) noexcept;

}  // namespace kf2::app::runtime
