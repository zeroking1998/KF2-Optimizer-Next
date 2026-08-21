#include <cstdlib>
#include <iostream>

#include "kf2/config/adaptive_locks.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::config;
    using kf2::optimizer::ManualLockState;

    const auto parsed = parse_adaptive_locks(
        "schema_version=1\n"
        "MaxDeadBodies=LOCK_CURRENT\n"
        "TargetFPS=MANUAL_VALUE\n"
        "FarParticleLOD=LOCK_MINIMUM\n");
    CHECK(parsed.has_value());
    CHECK(parsed.value().at("MaxDeadBodies") ==
          ManualLockState::lock_current);
    CHECK(parsed.value().at("TargetFPS") ==
          ManualLockState::manual_value);
    CHECK(parsed.value().at("FarParticleLOD") ==
          ManualLockState::lock_minimum);
    const auto encoded = serialize_adaptive_locks(parsed.value());
    CHECK(encoded ==
        "schema_version=1\n"
        "FarParticleLOD=LOCK_MINIMUM\n"
        "MaxDeadBodies=LOCK_CURRENT\n"
        "TargetFPS=MANUAL_VALUE\n");
    const auto roundtrip = parse_adaptive_locks(encoded);
    CHECK(roundtrip.has_value());
    CHECK(roundtrip.value() == parsed.value());

    CHECK(!parse_adaptive_locks("TargetFPS=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks("schema_version=2\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nTargetFPS=AUTO\nTargetFPS=LOCK_CURRENT\n")
        .has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nUnknownThing=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nFPS=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nOptimizerBuild=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nAI=LOCK_CURRENT\n").has_value());
    CHECK(adaptive_lock_name(ManualLockState::lock_maximum) ==
          "LOCK_MAXIMUM");
    return EXIT_SUCCESS;
}
