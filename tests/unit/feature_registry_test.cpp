#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "app/runtime/feature_registry.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace kf2::app {
struct UiRuntime {};
}

namespace {

using namespace kf2::app::runtime;

DispatchResult handled(kf2::app::UiRuntime&, const ActionPayload&) {
    return DispatchResult::handled;
}

constexpr std::array<std::string_view, 7> kFeatureNames{{
    "game", "settings", "overlay", "diagnostics", "backup", "graphics",
    "advanced",
}};

struct CompleteRegistryFixture {
    std::array<std::array<ActionImplementation, 64>, 7> implementations{};
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

}  // namespace

int main() {
    using namespace kf2::app::runtime;

    CHECK(!valid_feature_registry({}));

    CompleteRegistryFixture complete;
    CHECK(action_definitions().size() == 63);
    CHECK(complete.features.size() == 7);
    CHECK(valid_feature_registry(complete.features));

    const std::array game_only{complete.features[0]};
    CHECK(!valid_feature_registry(game_only));

    const std::array duplicate_feature{
        complete.features[0], complete.features[0]};
    CHECK(!valid_feature_registry(duplicate_feature));

    auto empty_name = complete.features[0];
    empty_name.name = {};
    complete.features[0] = empty_name;
    CHECK(!valid_feature_registry(complete.features));
    CompleteRegistryFixture null_handler_fixture;

    auto null_handler_actions = null_handler_fixture.implementations[0];
    null_handler_actions[0].handler = nullptr;
    null_handler_fixture.features[0].actions = {
        null_handler_actions.data(), null_handler_fixture.counts[0]};
    CHECK(!valid_feature_registry(null_handler_fixture.features));

    CompleteRegistryFixture duplicate_action_fixture;
    auto duplicate_actions = duplicate_action_fixture.implementations[0];
    duplicate_actions[1].id = duplicate_actions[0].id;
    duplicate_action_fixture.features[0].actions = {
        duplicate_actions.data(), duplicate_action_fixture.counts[0]};
    CHECK(!valid_feature_registry(duplicate_action_fixture.features));

    CompleteRegistryFixture unknown_action_fixture;
    auto unknown_actions = unknown_action_fixture.implementations[0];
    unknown_actions[0].id = static_cast<ActionId>(999);
    unknown_action_fixture.features[0].actions = {
        unknown_actions.data(), unknown_action_fixture.counts[0]};
    CHECK(!valid_feature_registry(unknown_action_fixture.features));

    CompleteRegistryFixture wrong_owner_fixture;
    auto wrong_owner_actions = wrong_owner_fixture.implementations[0];
    wrong_owner_actions[0].id = ActionId::settings_updates_check;
    wrong_owner_fixture.features[0].actions = {
        wrong_owner_actions.data(), wrong_owner_fixture.counts[0]};
    CHECK(!valid_feature_registry(wrong_owner_fixture.features));

    CompleteRegistryFixture partial_fixture;
    partial_fixture.features[0].actions = {
        partial_fixture.implementations[0].data(),
        partial_fixture.counts[0] - 1};
    CHECK(!valid_feature_registry(partial_fixture.features));

    CHECK(find_action_implementation(
              ActionId::game_launch, game_only) != nullptr);
    CHECK(find_action_implementation(
              ActionId::overlay_toggle, game_only) == nullptr);

    return EXIT_SUCCESS;
}
