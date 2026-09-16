#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_flex_stage.hpp"

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
    auto decision = decide_flex_control(false);
    CHECK(!decision.constrained);
    CHECK(decision.requested_substeps == 0);

    decision = decide_flex_control(true);
    CHECK(decision.constrained);
    CHECK(decision.requested_substeps == 1);

    kf2::optimizer::AdaptiveActionRecord pending;
    pending.action_id = 17;
    pending.control =
        kf2::optimizer::AdaptiveControlId::flex_solver_substeps;
    pending.status = kf2::optimizer::AdaptiveActionStatus::pending;
    pending.requested_value = 1.0;
    pending.observed_value = 1.0;
    pending.generation = {11, 12, 13, 14, 15};
    kf2::flex::ObservationSnapshot observed;
    observed.fresh = true;
    observed.control_fresh = true;
    observed.requested_substeps = 1;
    observed.last_forwarded_substeps = 1;
    const auto receipt = confirmed_flex_readback(&pending, observed, 99);
    CHECK(receipt.has_value());
    CHECK(receipt->status == kf2::optimizer::AdaptiveActionStatus::applied);
    CHECK(receipt->requested_value == 1.0);
    CHECK(receipt->observed_value == 1.0);
    CHECK(receipt->generation == pending.generation);
    CHECK(receipt->provider == "flex_shared_memory_readback");

    observed.last_forwarded_substeps = 2;
    CHECK(!confirmed_flex_readback(&pending, observed, 100).has_value());
    observed.last_forwarded_substeps = 2;
    observed.requested_substeps = 0;
    pending.requested_value = 0.0;
    CHECK(confirmed_flex_readback(&pending, observed, 101).has_value());
    observed.control_fresh = false;
    CHECK(!confirmed_flex_readback(&pending, observed, 102).has_value());
    observed.control_fresh = true;
    pending.status = kf2::optimizer::AdaptiveActionStatus::applied;
    CHECK(!confirmed_flex_readback(&pending, observed, 103).has_value());
    pending.status = kf2::optimizer::AdaptiveActionStatus::pending;
    CHECK(!confirmed_flex_readback(&pending, observed, 0).has_value());
    return EXIT_SUCCESS;
}
