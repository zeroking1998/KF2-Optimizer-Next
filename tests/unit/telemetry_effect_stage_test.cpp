#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "features/telemetry/telemetry_effect_stage.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::telemetry_pipeline;
    std::vector<std::string> calls;
    const auto flex = [&](const FlexControlEffect&) {
        calls.emplace_back("flex");
    };
    const auto profile = [&](const AdaptiveProfileEffect&) {
        calls.emplace_back("profile");
    };

    apply_effects_in_order({}, flex, profile);
    CHECK(calls.empty());

    TelemetryEffectBatch batch;
    batch.adaptive_profile =
        AdaptiveProfileEffect{kf2::optimizer::Profile::stability};
    apply_effects_in_order(batch, flex, profile);
    CHECK(calls.size() == 1);
    CHECK(calls[0] == "profile");

    calls.clear();
    batch.flex_control = FlexControlEffect{
        3, true, kf2::optimizer::AdaptiveCapabilityState::available};
    apply_effects_in_order(batch, flex, profile);
    CHECK(calls.size() == 2);
    CHECK(calls[0] == "flex");
    CHECK(calls[1] == "profile");

    std::string persisted = "balanced";
    const std::string previous = persisted;
    const bool write_succeeded = false;
    const auto rollback_on_failure = [&](const AdaptiveProfileEffect&) {
        persisted = "stability";
        if (!write_succeeded) persisted = previous;
    };
    apply_effects_in_order(
        TelemetryEffectBatch{
            .adaptive_profile = AdaptiveProfileEffect{
                kf2::optimizer::Profile::stability}},
        flex, rollback_on_failure);
    CHECK(persisted == previous);
    return EXIT_SUCCESS;
}
