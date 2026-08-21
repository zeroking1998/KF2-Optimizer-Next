#include <cstdlib>
#include <iostream>
#include <vector>

#include "kf2/config/setting_catalog.hpp"
#include "kf2/optimizer/adaptive_profile.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2;
    using namespace kf2::optimizer;

    CHECK(adaptive_profile_quality(Profile::stability) == 100);
    CHECK(adaptive_profile_quality(Profile::balanced) == 85);
    CHECK(adaptive_profile_quality(Profile::high_performance) == 70);
    CHECK(adaptive_profile_token(Profile::balanced) == "balanced");
    CHECK(parse_adaptive_profile("stability") == Profile::stability);
    CHECK(!parse_adaptive_profile("custom"));

    CHECK(bound_adaptive_profile(Profile::high_performance, 70, 100) ==
          Profile::high_performance);
    CHECK(bound_adaptive_profile(Profile::high_performance, 80, 100) ==
          Profile::balanced);
    CHECK(bound_adaptive_profile(Profile::stability, 70, 90) ==
          Profile::balanced);
    CHECK(!bound_adaptive_profile(Profile::balanced, 90, 95));
    CHECK(!bound_adaptive_profile(Profile::balanced, 100, 90));

    constexpr std::uint64_t start = 10'000'000'000ULL;
    AdaptiveProfilePersistenceGate gate;
    AdaptiveProfilePersistenceInput persistence{
        .current = Profile::stability,
        .recommended = Profile::high_performance,
        .active_gameplay = false,
        .telemetry_valid = true,
        .now_ns = start};
    CHECK(!gate.evaluate(persistence));
    persistence.active_gameplay = true;
    CHECK(!gate.evaluate(persistence));
    persistence.now_ns = start +
        AdaptiveProfilePersistenceGate::kDegradeDwellNs - 1;
    CHECK(!gate.evaluate(persistence));
    persistence.now_ns = start +
        AdaptiveProfilePersistenceGate::kDegradeDwellNs;
    CHECK(gate.evaluate(persistence) == Profile::high_performance);

    // The new current value prevents duplicate writes, and recovery must be
    // substantially slower than degradation.
    persistence.current = Profile::high_performance;
    CHECK(!gate.evaluate(persistence));
    persistence.recommended = Profile::stability;
    persistence.now_ns += AdaptiveProfilePersistenceGate::kCommitCooldownNs;
    // An intervention recommendation must never be mistaken for quality
    // recovery, regardless of how long it remains selected.
    CHECK(!gate.evaluate(persistence));
    persistence.now_ns += AdaptiveProfilePersistenceGate::kRecoveryDwellNs;
    CHECK(!gate.evaluate(persistence));
    persistence.recovery_eligible = true;
    persistence.now_ns += 1;
    CHECK(!gate.evaluate(persistence));
    persistence.now_ns +=
        AdaptiveProfilePersistenceGate::kRecoveryDwellNs - 1;
    CHECK(!gate.evaluate(persistence));
    ++persistence.now_ns;
    CHECK(gate.evaluate(persistence) == Profile::stability);

    // A changing recommendation restarts dwell; invalid telemetry can never
    // be persisted. Session type is diagnostic, not a global policy gate.
    gate.reset();
    persistence.current = Profile::stability;
    persistence.recommended = Profile::high_performance;
    persistence.recovery_eligible = false;
    persistence.now_ns += 1;
    CHECK(!gate.evaluate(persistence));
    persistence.recommended = Profile::balanced;
    persistence.now_ns +=
        AdaptiveProfilePersistenceGate::kDegradeDwellNs - 1;
    CHECK(!gate.evaluate(persistence));
    persistence.now_ns += 1;
    CHECK(!gate.evaluate(persistence));
    persistence.telemetry_valid = false;
    CHECK(!gate.evaluate(persistence));

    const std::vector<config::RequestedChange> changes{
        {config::SettingId::target_fps, 120,
         config::ChangeSource::adaptive, L"automatic"},
        {config::SettingId::corpse_limit, 8,
         config::ChangeSource::adaptive, L"automatic"},
    };
    config::AdaptiveLocks locks;
    locks.emplace(config::setting_token(config::SettingId::corpse_limit),
                  ManualLockState::lock_current);
    const auto filtered = filter_adaptive_locked_changes(changes, locks);
    CHECK(filtered.size() == 1);
    CHECK(filtered.front().id == config::SettingId::target_fps);
    return EXIT_SUCCESS;
}
