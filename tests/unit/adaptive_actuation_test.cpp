#include "kf2/optimizer/adaptive_actuation.hpp"

#include <cassert>
#include <cstdlib>

// Keep this contract active in Release builds where the standard assert macro
// would otherwise erase every receipt/state transition check.
#ifdef NDEBUG
#undef assert
#define assert(condition)                         \
    do {                                          \
        if (!(condition)) std::abort();           \
    } while (false)
#endif

using namespace kf2::optimizer;

namespace {

AdaptiveGeneration generation(std::uint64_t settings = 1,
                              std::uint64_t capabilities = 15) {
    return {11, 12, 13, settings, capabilities};
}

AdaptiveActionReceipt success(const AdaptiveActionRecord& action,
                              double observed, std::uint64_t now_ns) {
    return {action.action_id, action.control, AdaptiveActionStatus::applied,
            action.requested_value, observed, action.generation, now_ns,
            "test_provider", {}, true, action.observed_value};
}

}  // namespace

int main() {
    AdaptiveActuationTracker tracker(generation());

    // A1: proposal alone never mutates the observed/effective value.
    auto action = tracker.propose(AdaptiveControlId::corpse_runtime_limit, 143.0,
        151.0, AdaptiveCapabilityState::available, 100, "corpse_provider");
    assert(action.status == AdaptiveActionStatus::proposed);
    assert(tracker.effective_value(AdaptiveControlId::corpse_runtime_limit) == 151.0);

    // A2: only a matching observed receipt changes effective state.
    assert(tracker.dispatch(AdaptiveControlId::corpse_runtime_limit, 200));
    action = *tracker.current(AdaptiveControlId::corpse_runtime_limit);
    assert(action.status == AdaptiveActionStatus::pending);
    assert(tracker.receive(success(action, 143.0, 300)) ==
           AdaptiveReceiptResult::accepted);
    assert(tracker.effective_value(AdaptiveControlId::corpse_runtime_limit) == 143.0);

    // A5: duplicate receipt is harmless and idempotent.
    assert(tracker.receive(success(action, 143.0, 301)) ==
           AdaptiveReceiptResult::duplicate);
    assert(tracker.effective_value(AdaptiveControlId::corpse_runtime_limit) == 143.0);

    // A3/A9: a late receipt from an old settings generation is ignored.
    tracker.rebase(generation(2), 400);
    assert(tracker.receive(success(action, 99.0, 500)) ==
           AdaptiveReceiptResult::stale_generation);
    assert(!tracker.effective_value(AdaptiveControlId::corpse_runtime_limit));

    // A4: a timeout is FAILED and leaves the old effective value intact.
    action = tracker.propose(AdaptiveControlId::flex_solver_substeps, 4.0, 5.0,
        AdaptiveCapabilityState::available, 600, "flex_provider");
    assert(tracker.dispatch(AdaptiveControlId::flex_solver_substeps, 700));
    tracker.poll(700 + AdaptiveActuationTracker::kAcknowledgementTimeoutNs);
    assert(tracker.current(AdaptiveControlId::flex_solver_substeps)->status ==
           AdaptiveActionStatus::failed);
    assert(tracker.effective_value(AdaptiveControlId::flex_solver_substeps) == 5.0);

    // A6/unavailable/shadow: none can claim a live applied value.
    assert(tracker.propose(AdaptiveControlId::flex_particle_budget, 3.0, {},
        AdaptiveCapabilityState::restart_required, 800, "ini", true).status ==
        AdaptiveActionStatus::restart_required);
    assert(tracker.propose(AdaptiveControlId::flex_particle_spawn, 3.0, {},
        AdaptiveCapabilityState::unavailable, 800).status ==
        AdaptiveActionStatus::skipped_unavailable);
    assert(tracker.propose(AdaptiveControlId::flex_particle_lifetime, 3.0, {},
        AdaptiveCapabilityState::shadow, 800).status ==
        AdaptiveActionStatus::shadow);

    // One in-flight action per domain; a conflicting proposal cannot replace it.
    action = tracker.propose(AdaptiveControlId::corpse_runtime_limit, 120.0, 130.0,
        AdaptiveCapabilityState::available, 900);
    assert(tracker.dispatch(AdaptiveControlId::corpse_runtime_limit, 901));
    const auto pending_id = action.action_id;
    action = tracker.propose(AdaptiveControlId::corpse_runtime_limit, 110.0, 130.0,
        AdaptiveCapabilityState::available, 902);
    assert(action.action_id == pending_id);
    assert(action.status == AdaptiveActionStatus::pending);

    // A8/A9: capability loss cancels pending debt, and late completion cannot
    // cross the new capability generation.
    const auto capability_pending = action;
    tracker.rebase(generation(2, 16), 903);
    assert(tracker.current(AdaptiveControlId::corpse_runtime_limit) == nullptr);
    assert(!tracker.effective_value(AdaptiveControlId::corpse_runtime_limit));
    assert(tracker.receive(success(capability_pending, 120.0, 904)) ==
           AdaptiveReceiptResult::stale_generation);

    // A10: disabling cancels pending work without changing the user/effective value.
    action = tracker.propose(AdaptiveControlId::corpse_runtime_limit, 120.0, 130.0,
        AdaptiveCapabilityState::available, 905);
    assert(tracker.dispatch(AdaptiveControlId::corpse_runtime_limit, 906));
    tracker.disable(907);
    assert(tracker.current(AdaptiveControlId::corpse_runtime_limit)->status ==
           AdaptiveActionStatus::failed);
    assert(tracker.effective_value(AdaptiveControlId::corpse_runtime_limit) == 130.0);

    // Repeated failures are bounded by a breaker that survives settings
    // generations inside the same process/session.
    AdaptiveActuationTracker breaker(generation(10));
    for (int i = 0; i < 3; ++i) {
        action = breaker.propose(AdaptiveControlId::flex_solver_substeps,
            4.0 - i, 5.0, AdaptiveCapabilityState::available,
            1100 + i * 1'000'000'000ULL);
        assert(breaker.dispatch(AdaptiveControlId::flex_solver_substeps,
                                1101 + i * 1'000'000'000ULL));
        AdaptiveActionReceipt failure{
            action.action_id, action.control, AdaptiveActionStatus::failed,
            action.requested_value, {}, action.generation,
            1102 + i * 1'000'000'000ULL, "flex_provider", "write_failed"};
        assert(breaker.receive(failure) == AdaptiveReceiptResult::accepted);
        if (i == 0) {
            breaker.rebase(generation(11), 500'000'000ULL);
        }
    }
    assert(breaker.circuit_open(AdaptiveControlId::flex_solver_substeps));
    assert(breaker.propose(AdaptiveControlId::flex_solver_substeps, 1.0, 5.0,
        AdaptiveCapabilityState::available, 4'000'000'000ULL).status ==
        AdaptiveActionStatus::skipped_unavailable);

    return 0;
}
