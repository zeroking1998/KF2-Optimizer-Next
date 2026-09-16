#include <cstdlib>
#include <iostream>
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
    const FlexControlEffect defaults;
    CHECK(defaults.requested_substeps == 0);
    CHECK(!defaults.constrained);
    CHECK(defaults.capability ==
          kf2::optimizer::AdaptiveCapabilityState::unavailable);

    const FlexControlEffect requested{
        3, true, kf2::optimizer::AdaptiveCapabilityState::available};
    CHECK(requested.requested_substeps == 3);
    CHECK(requested.constrained);
    CHECK(requested.capability ==
          kf2::optimizer::AdaptiveCapabilityState::available);
    return EXIT_SUCCESS;
}
