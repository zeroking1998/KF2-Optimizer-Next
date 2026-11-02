#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

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

    // The removed section 14A and 66-78 knobs were valid schema-1 locks.
    constexpr std::string_view removed_flex_knobs[]{
        "OccludedFlexParticleLOD", "OccludedFlexParticleBudget",
        "OccludedFlexFluidQuality", "OccludedFlexNonFluidQuality",
        "OccludedFlexSolverLOD", "OccludedFlexUpdateBudget",
        "FlexAdaptiveEnabled", "FlexQuality", "FlexPerformanceBudget",
        "FlexGPUBudget", "FlexMemoryBudget", "FlexSafetyClass",
        "FlexSolverLOD", "FlexSolverQuality", "FlexSolverPerformanceBudget",
        "FlexSolverUpdateBudget", "FlexSolverDistanceLOD",
        "FlexSolverEmergencyLOD", "FlexMinimumSubsteps", "FlexMaximumSubsteps",
        "FlexTargetSubsteps", "FlexAdaptiveSubsteps", "FlexEmergencySubsteps",
        "FlexSubstepChangeRate", "FlexSubstepRecoveryRate",
        "FlexMinimumIterations", "FlexMaximumIterations", "FlexTargetIterations",
        "FlexAdaptiveIterations", "FlexIterationChangeRate",
        "FlexFixedVariableTimePolicy", "FlexMaximumAllowedDelta",
        "FlexTimeStabilityGuard", "FlexParticleCapacity",
        "FlexActiveParticleBudget", "FlexFreeParticleReserve", "FlexParticleLOD",
        "FlexParticleDistance", "FlexParticleEmergencyBudget", "FlexFluidQuality",
        "FlexFluidParticleBudget", "FlexFluidDistanceLOD", "FlexFluidCapacity",
        "FlexFluidSolverQuality", "FlexNonFluidQuality", "FlexNonFluidBudget",
        "FlexNonFluidLOD", "FlexNonFluidCapacity", "FlexRigidQuality",
        "FlexRigidBudget", "FlexRigidDistanceLOD", "FlexRigidUpdateQuality",
        "FlexShapeMatchingQuality", "FlexCollisionQuality",
        "FlexCollisionShapeBudget", "FlexCollisionDistanceLOD",
        "FlexStaticTrianglePressureLimit", "FlexCollisionDiagnostics",
        "FlexContainerIdentity", "FlexContainerBudget",
        "FlexSolverContainerAssociation", "FlexCollisionFilterState",
        "FlexGenerationIdentity", "FlexAsyncMode", "FlexSyncPolicy",
        "FlexSynchronizationBudget", "FlexMappingPolicy",
        "FlexBlockingReadbackPolicy", "FlexTransferPolicy",
        "FlexGPUContextLossPolicy", "FlexTDRFallback",
        "FlexDriverChangeRevalidation", "FlexRuntimeRevalidation",
        "FlexEmergencyPassthrough",
    };
    constexpr std::string_view protected_user_settings[]{
        "PhysXLevel", "GoreLevel", "MaxPhysicsSubsteps",
        "FlexInvisibleFramesBeforeSleep", "FlexDistanceBeforeSleep",
        "AllowSPHFluidMipmap", "FlexRigidBodiesCollisionAtHighLevel",
    };
    constexpr std::string_view states[]{
        "AUTO", "LOCK_CURRENT", "LOCK_MINIMUM", "LOCK_MAXIMUM", "MANUAL_VALUE",
    };
    std::string legacy = encoded;
    for (const auto names : {std::span<const std::string_view>{removed_flex_knobs},
                             std::span<const std::string_view>{protected_user_settings}}) {
        for (const auto name : names) {
            for (const auto state : states) {
                const std::string line = std::string{name} + '=' +
                    std::string{state} + '\n';
                const auto migrated = parse_adaptive_locks(encoded + line);
                CHECK(migrated.has_value());
                CHECK(migrated.value() == parsed.value());
            }
            const std::string line = std::string{name} + "=LOCK_CURRENT\n";
            legacy += line;
            CHECK(!parse_adaptive_locks(encoded + line + line).has_value());
            CHECK(!parse_adaptive_locks(encoded + std::string{name} +
                                       "=INVALID\n").has_value());
        }
    }
    const auto migrated = parse_adaptive_locks(legacy);
    CHECK(migrated.has_value());
    CHECK(migrated.value() == parsed.value());
    const auto migrated_encoded = serialize_adaptive_locks(migrated.value());
    CHECK(migrated_encoded == encoded);
    const auto migrated_roundtrip = parse_adaptive_locks(migrated_encoded);
    CHECK(migrated_roundtrip.has_value());
    CHECK(migrated_roundtrip.value() == parsed.value());

    for (const auto name : {"FlexUnknownSetting", "OccludedFlexUnknownSetting",
                            "FlexTrend", "FlexTrendWeight",
                            "MissingFlexTelemetry", "FlexRuntimeIdentity"}) {
        CHECK(!parse_adaptive_locks(encoded + name + "=LOCK_CURRENT\n")
                   .has_value());
    }
    CHECK(!parse_adaptive_locks("FlexQuality=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=2\nFlexQuality=LOCK_CURRENT\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nFlexQuality\n").has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nFlexQuality=LOCK_CURRENT\nUnknownThing=AUTO\n")
               .has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nFlexQuality=AUTO\nschema_version=1\n")
               .has_value());
    CHECK(!parse_adaptive_locks(
        "schema_version=1\nflexQuality=LOCK_CURRENT\n").has_value());
    const auto retired_only = parse_adaptive_locks(
        "schema_version=1\nFlexQuality=LOCK_CURRENT\nPhysXLevel=AUTO\n");
    CHECK(retired_only.has_value());
    CHECK(retired_only.value().empty());
    CHECK(serialize_adaptive_locks(retired_only.value()) == "schema_version=1\n");

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
