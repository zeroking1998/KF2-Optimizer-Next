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
    kf2::flex::AdaptivePolicy policy;
    FlexControlInput input;
    input.target_fps = 60;
    input.quality_change_budget = 2;
    input.fps = 60.0;
    input.now_ms = 1000;

    auto decision = decide_flex_control(policy, input);
    CHECK(!decision.constrained);
    CHECK(decision.requested_substeps == 0);

    input.actuator_available = true;
    decision = decide_flex_control(policy, input);
    CHECK(decision.constrained);
    CHECK(decision.requested_substeps == 5);

    input.fps = 35.0;
    input.now_ms = 2000;
    CHECK(decide_flex_control(policy, input).requested_substeps == 5);
    input.now_ms = 2400;
    CHECK(decide_flex_control(policy, input).requested_substeps == 2);
    input.now_ms = 2800;
    CHECK(decide_flex_control(policy, input).requested_substeps == 2);
    input.now_ms = 3200;
    CHECK(decide_flex_control(policy, input).requested_substeps == 1);

    input.fps = 60.0;
    input.now_ms = 3600;
    CHECK(decide_flex_control(policy, input).requested_substeps == 1);
    input.now_ms = 8600;
    CHECK(decide_flex_control(policy, input).requested_substeps == 2);

    policy.reset();
    input.now_ms = 9600;
    CHECK(decide_flex_control(policy, input).requested_substeps == 5);

    input.fps.reset();
    decision = decide_flex_control(policy, input);
    CHECK(!decision.constrained);
    CHECK(decision.requested_substeps == 0);

    input.actuator_available = false;
    decision = decide_flex_control(policy, input);
    CHECK(!decision.constrained);
    CHECK(decision.requested_substeps == 0);

    kf2::optimizer::AdaptiveActionRecord pending;
    pending.action_id = 17;
    pending.control =
        kf2::optimizer::AdaptiveControlId::flex_solver_substeps;
    pending.status = kf2::optimizer::AdaptiveActionStatus::pending;
    pending.requested_value = 5.0;
    pending.observed_value = 1.0;
    pending.generation = {11, 12, 13, 14, 15};
    kf2::flex::ObservationSnapshot observed;
    observed.fresh = true;
    observed.control_fresh = true;
    observed.requested_substeps = 5;
    observed.last_forwarded_substeps = 5;
    const auto receipt = confirmed_flex_readback(&pending, observed, 99);
    CHECK(receipt.has_value());
    CHECK(receipt->status == kf2::optimizer::AdaptiveActionStatus::applied);
    CHECK(receipt->requested_value == 5.0);
    CHECK(receipt->observed_value == 5.0);
    CHECK(receipt->generation == pending.generation);
    CHECK(receipt->provider == "flex_shared_memory_readback");

    observed.last_forwarded_substeps = 4;
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
