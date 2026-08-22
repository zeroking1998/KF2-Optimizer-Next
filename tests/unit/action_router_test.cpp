#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include "app/runtime/action_router.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace kf2::app {
struct UiRuntime {
    int calls{0};
};
}

namespace {

using namespace kf2::app::runtime;

DispatchResult handled(kf2::app::UiRuntime& runtime, const ActionPayload&) {
    ++runtime.calls;
    return DispatchResult::handled;
}

constexpr std::array<std::string_view, 7> kFeatureNames{{
    "navigation", "game", "settings", "overlay",
    "diagnostics", "optimizer", "backup",
}};

struct CompleteRegistryFixture {
    std::array<std::array<ActionImplementation, 65>, 7> implementations{};
    std::array<std::size_t, 7> counts{};
    std::array<FeatureDefinition, 7> features{};

    CompleteRegistryFixture() {
        for (const auto& contract : action_definitions()) {
            const auto feature = static_cast<std::size_t>(contract.feature);
            implementations[feature][counts[feature]++] =
                {contract.id, &handled};
        }
        for (std::size_t feature = 0; feature < features.size(); ++feature) {
            features[feature] = {
                static_cast<FeatureId>(feature), kFeatureNames[feature],
                std::span<const ActionImplementation>{
                    implementations[feature].data(), counts[feature]}};
        }
    }
};

ActionRequest request(ActionId id, std::string_view received_name,
                      ActionPayload payload = NoPayload{}) {
    return {id, received_name, std::move(payload)};
}

}  // namespace

int main() {
    using namespace kf2::app::runtime;

    CHECK(action_definitions().size() == 65);
    CHECK(action_bindings().size() == 76);

    CompleteRegistryFixture complete;
    CHECK(valid_feature_registry(complete.features));

    kf2::app::UiRuntime runtime;
    for (const auto& definition : action_definitions()) {
        runtime = {};
        CHECK(dispatch_action(
                  runtime,
                  request(definition.id, definition.canonical_name),
                  complete.features) == DispatchResult::handled);
        CHECK(runtime.calls == 1);
    }

    for (const auto& binding : action_bindings()) {
        const auto parsed = parse_action(binding.name);
        CHECK(parsed.has_value());
        CHECK(parsed->id == binding.id);
        const auto* contract = find_action(parsed->id);
        CHECK(contract != nullptr);
        const auto* feature =
            find_feature_definition(contract->feature, complete.features);
        CHECK(feature != nullptr);
        const auto* implementation =
            find_action_implementation(parsed->id, complete.features);
        CHECK(implementation != nullptr);

        std::size_t implementation_count = 0;
        for (const auto& candidate_feature : complete.features) {
            for (const auto& candidate : candidate_feature.actions) {
                if (candidate.id == parsed->id) ++implementation_count;
            }
        }
        CHECK(implementation_count == 1);

        runtime = {};
        CHECK(dispatch_action(runtime, *parsed, complete.features) ==
              DispatchResult::handled);
        CHECK(runtime.calls == 1);
    }

    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(static_cast<ActionId>(999), "unknown"),
              complete.features) == DispatchResult::unknown_action);
    CHECK(runtime.calls == 0);

    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(ActionId::game_launch, "game-launch",
                      std::monostate{}),
              complete.features) == DispatchResult::invalid_payload);
    CHECK(runtime.calls == 0);

    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(ActionId::game_launch, "game-launch"), {}) ==
          DispatchResult::invalid_registry);
    CHECK(runtime.calls == 0);

    CompleteRegistryFixture duplicate_feature_fixture;
    duplicate_feature_fixture.features[1] =
        duplicate_feature_fixture.features[0];
    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(ActionId::navigate_diagnostics, "header-diagnostics"),
              duplicate_feature_fixture.features) ==
          DispatchResult::invalid_registry);
    CHECK(runtime.calls == 0);

    CompleteRegistryFixture wrong_owner_fixture;
    auto wrong_owner_actions = wrong_owner_fixture.implementations[0];
    wrong_owner_actions[0].id = ActionId::game_launch;
    wrong_owner_fixture.features[0].actions = {
        wrong_owner_actions.data(), wrong_owner_fixture.counts[0]};
    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(ActionId::navigate_diagnostics, "header-diagnostics"),
              wrong_owner_fixture.features) ==
          DispatchResult::invalid_registry);
    CHECK(runtime.calls == 0);

    CompleteRegistryFixture partial_fixture;
    partial_fixture.features[0].actions = {
        partial_fixture.implementations[0].data(),
        partial_fixture.counts[0] - 1};
    runtime = {};
    CHECK(dispatch_action(
              runtime,
              request(ActionId::navigate_diagnostics, "header-diagnostics"),
              partial_fixture.features) == DispatchResult::invalid_registry);
    CHECK(runtime.calls == 0);

    return EXIT_SUCCESS;
}
