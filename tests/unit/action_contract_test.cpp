#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>

#include "app/runtime/action_contract.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::app::runtime;

    constexpr std::array<std::string_view, 3> kExistingControls{{
        "overlay-scale-slider", "settings-corpses-slider",
        "settings-target-slider",
    }};

    CHECK(action_bindings().size() == 64);
    CHECK(action_definitions().size() == 62);
    CHECK(control_definitions().size() == kExistingControls.size() + 4);

    for (const auto name : kExistingControls) {
        CHECK(find_control(name) != nullptr);
    }
    CHECK(!parse_action("unknown-action").has_value());
    CHECK(!parse_action("settings-mode-manual").has_value());
    CHECK(!parse_action("settings-mode-adaptive").has_value());
    CHECK(!parse_action("settings-session-config").has_value());
    CHECK(!parse_action("settings-gore-down").has_value());
    CHECK(!parse_action("optimizer-manual-create").has_value());
    CHECK(!parse_action("optimizer-manual-load").has_value());
    CHECK(!parse_action("header-guide").has_value());
    CHECK(!parse_action("guide-next").has_value());
    CHECK(!parse_action("settings-flex-down").has_value());
    CHECK(!parse_action("settings-animations").has_value());
    CHECK(!parse_action("settings-advanced-toggle").has_value());
    CHECK(!parse_action("dashboard-settings").has_value());
    CHECK(!parse_action("settings-finetuning").has_value());
    CHECK(!parse_action("optimizer-open-settings").has_value());
    CHECK(!parse_action("game-offline-telemetry").has_value());
    CHECK(!parse_action("game-open-install").has_value());
    CHECK(!parse_action("game-open-logs").has_value());
    CHECK(!parse_action("optimizer-apply").has_value());
    CHECK(!parse_action("optimizer-export").has_value());
    CHECK(!parse_action("optimizer-import").has_value());
    CHECK(!parse_action("optimizer-open-backups").has_value());
    CHECK(!parse_action("optimizer-preview").has_value());
    CHECK(!parse_action("dashboard-diagnostics").has_value());
    CHECK(!parse_action("dashboard-overlay").has_value());
    CHECK(!parse_action("dashboard-graphics").has_value());
    CHECK(!parse_action("header-diagnostics").has_value());
    CHECK(find_control("unknown-slider") == nullptr);

    std::set<std::string_view> binding_names;
    std::set<ActionId> binding_ids;
    for (const auto& binding : action_bindings()) {
        CHECK(binding_names.insert(binding.name).second);
        binding_ids.insert(binding.id);
    }
    CHECK(binding_ids.size() == action_definitions().size());

    std::set<ActionId> definition_ids;
    std::set<std::string_view> canonical_names;
    std::array<std::size_t, 7> feature_counts{};
    for (const auto& definition : action_definitions()) {
        CHECK(definition_ids.insert(definition.id).second);
        CHECK(canonical_names.insert(definition.canonical_name).second);
        CHECK(definition.payload_kind == ActionPayloadKind::no_payload);
        ++feature_counts[static_cast<std::size_t>(definition.feature)];
        const auto parsed = parse_action(definition.canonical_name);
        CHECK(parsed.has_value());
        CHECK(parsed->id == definition.id);
    }

    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::game)] == 3);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::settings)] == 5);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::overlay)] == 8);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::diagnostics)] == 7);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::backup)] == 1);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::graphics)] == 23);
    CHECK(feature_counts[static_cast<std::size_t>(FeatureId::advanced)] == 15);


    std::set<ControlId> control_ids;
    std::set<std::string_view> control_names;
    std::size_t overlay_controls = 0;
    std::size_t settings_controls = 0;
    for (const auto& control : control_definitions()) {
        CHECK(control_ids.insert(control.id).second);
        CHECK(control_names.insert(control.name).second);
        if (control.feature == FeatureId::overlay) ++overlay_controls;
        if (control.feature == FeatureId::settings) ++settings_controls;
    }
    CHECK(overlay_controls == 1);
    CHECK(settings_controls == 2);
    CHECK(find_control("graphics-film-grain-slider") != nullptr);
    CHECK(find_control("graphics-film-grain-slider")->minimum == 0);
    CHECK(find_control("graphics-film-grain-slider")->maximum == 200);
    CHECK(find_control("advanced-screen-percentage-slider") != nullptr);
    CHECK(find_control("advanced-screen-percentage-slider")->minimum == 50);
    CHECK(find_control("advanced-screen-percentage-slider")->maximum == 200);
    CHECK(find_control("advanced-particle-percentage-slider") != nullptr);
    CHECK(find_control("advanced-decal-lifetime-slider") != nullptr);

    CHECK(!requires_normal_mode(ActionId::game_launch, {.protected_game_launch = false}));
    CHECK(requires_normal_mode(ActionId::game_launch, {.protected_game_launch = true}));
    CHECK(requires_normal_mode(ActionId::optimizer_backup, {}));
    CHECK(!requires_normal_mode(ActionId::diagnostics_repair_package, {}));
    CHECK(!requires_normal_mode(ActionId::diagnostics_auto_repair, {}));

    const auto launch = resolve_action(
        "dashboard-launch", {.protected_game_launch = true});
    CHECK(launch.has_value());
    CHECK(launch->id == ActionId::game_launch);
    CHECK(launch->canonical_name == "dashboard-launch");
    CHECK(launch->normal_mode_required);

    CHECK(!resolve_action("settings-adaptive-online", {}).has_value());


    CHECK(!resolve_action("unknown-action", {}).has_value());

    const auto* target = find_control("settings-target-slider");
    const auto* corpses = find_control("settings-corpses-slider");
    const auto* gore = find_control("settings-gore-slider");
    const auto* scale = find_control("overlay-scale-slider");
    CHECK(target && target->minimum == 30 && target->maximum == 240);
    CHECK(corpses && corpses->minimum == 4 && corpses->maximum == 2000);
    CHECK(gore == nullptr);
    CHECK(find_control("settings-flex-slider") == nullptr);
    CHECK(scale && scale->minimum == 60 && scale->maximum == 200);
    CHECK(clamp_control_value(*target, 10) == 30);
    CHECK(clamp_control_value(*target, 500) == 240);
    for (int value = 4; value <= 2000; ++value) {
        const auto request = resolve_control("settings-corpses-slider", value);
        CHECK(request.has_value());
        CHECK(request->value == value);
    }

    for (int requested = 30; requested <= 240; ++requested) {
        const auto exact = resolve_control("settings-target-slider", requested);
        CHECK(exact.has_value());
        CHECK(exact->value == requested);
    }

    const auto target_request = resolve_control("settings-target-slider", 500);
    CHECK(target_request.has_value());
    CHECK(target_request->id == ControlId::target_fps);
    CHECK(target_request->value == 240);

    const auto corpse_request = resolve_control("settings-corpses-slider", 1);
    CHECK(corpse_request.has_value());
    CHECK(corpse_request->id == ControlId::corpse_limit);
    CHECK(corpse_request->value == 4);

    CHECK(!resolve_control("unknown-slider", 100).has_value());

    return EXIT_SUCCESS;
}
