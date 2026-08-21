#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

#include "kf2/config/kf2_catalog.hpp"
#include "kf2/optimizer/adaptive_registry.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::optimizer;
    const auto registry = adaptive_target_registry();
    CHECK(registry.size() >= 900);

    std::unordered_set<std::string> names;
    std::array<bool, 8> roles{};
    for (const auto& record : registry) {
        CHECK(!record.name.empty());
        CHECK(names.insert(record.name).second);
        roles[static_cast<std::size_t>(record.role)] = true;
        if (record.evidence_state == AdaptiveEvidenceState::proven) {
            CHECK(record.source_trust == SourceTrustClass::verified);
            CHECK(record.rollback_possible);
        }
        if (record.role == AdaptiveRole::permanently_protected) {
            CHECK(record.safety_class ==
                  AdaptiveSafetyClass::permanent_blocked);
            CHECK(record.evidence_state ==
                  AdaptiveEvidenceState::permanent_blocked);
            CHECK(record.actuation_class == ActuationClass::permanent_blocked);
            CHECK(!adaptive_setting_active_ready(
                record, AdaptiveCapabilityState::available));
        }
    }
    for (const bool present : roles) CHECK(present);

    const auto* fps = find_adaptive_setting("FPS");
    CHECK(fps && fps->role == AdaptiveRole::sensor);
    const auto* build = find_adaptive_setting("OptimizerBuild");
    CHECK(build && build->role == AdaptiveRole::build_release);
    const auto* visibility = find_adaptive_setting("Visibility");
    CHECK(visibility && visibility->role == AdaptiveRole::context);
    const auto* protected_ai = find_adaptive_setting("AI");
    CHECK(protected_ai && protected_ai->role ==
          AdaptiveRole::permanently_protected);
    CHECK(find_adaptive_setting("detours") &&
          find_adaptive_setting("detours")->role ==
              AdaptiveRole::permanently_protected);
    CHECK(find_adaptive_setting("injection") &&
          find_adaptive_setting("injection")->role ==
              AdaptiveRole::permanently_protected);
    const auto* target = find_adaptive_setting("TargetFPS");
    CHECK(target && target->evidence_state == AdaptiveEvidenceState::proven);
    CHECK(target->actuation_class == ActuationClass::config_only);
    CHECK(!adaptive_setting_active_ready(
        *target, AdaptiveCapabilityState::available));
    const auto* retired_online_gate =
        find_adaptive_setting("OnlineAdaptiveAllowed");
    CHECK(retired_online_gate != nullptr);
    CHECK(retired_online_gate->role == AdaptiveRole::diagnostic);
    CHECK(retired_online_gate->actuation_class == ActuationClass::shadow_only);
    CHECK(retired_online_gate->evidence_state ==
          AdaptiveEvidenceState::not_available);

    AdaptiveSettingRecord ready;
    ready.name = "SyntheticProvenCanary";
    ready.role = AdaptiveRole::adaptive_knob;
    ready.source_trust = SourceTrustClass::verified;
    ready.current_value = 5.0;
    ready.target_value = 4.0;
    ready.safe_minimum = 1.0;
    ready.safe_maximum = 10.0;
    ready.step_size = 1.0;
    ready.restore_value = 5.0;
    ready.offline_status = OfflineSafetyStatus::safe;
    ready.online_status = OnlineSafetyStatus::safe;
    ready.safety_class = AdaptiveSafetyClass::conditional;
    ready.evidence_state = AdaptiveEvidenceState::proven;
    ready.actuation_class = ActuationClass::live_conditional;
    ready.maximum_change_rate = 1.0;
    ready.canary_allowed = true;
    ready.rollback_possible = true;
    ready.confidence = 0.90;
    ready.measurement_count = 12;
    ready.build_binding = "kf2-test-build";
    ready.runtime_binding = "runtime-test-hash";
    CHECK(adaptive_setting_active_ready(
        ready, AdaptiveCapabilityState::available));
    CHECK(!adaptive_setting_active_ready(
        ready, AdaptiveCapabilityState::unavailable));
    CHECK(!adaptive_setting_active_ready(
        ready, AdaptiveCapabilityState::restart_required));
    ready.runtime_binding.clear();
    CHECK(!adaptive_setting_active_ready(
        ready, AdaptiveCapabilityState::available));
    CHECK(adaptive_role_name(AdaptiveRole::permanently_protected) ==
          "PERMANENTLY_PROTECTED");
    CHECK(adaptive_safety_class_name(AdaptiveSafetyClass::lab) == "LAB");
    CHECK(adaptive_evidence_state_name(
              AdaptiveEvidenceState::test_required) == "TEST_REQUIRED");
    CHECK(adaptive_actuation_class_name(ActuationClass::config_only) ==
          "CONFIG_ONLY");

    // Every setting that the verified 213-entry KF2 catalog can edit must also
    // be present in the target model, but it remains TEST_REQUIRED for runtime
    // Adaptive use until a separate reversible A/B proof exists.
    for (const auto& definition : kf2::config::all_settings()) {
        std::string name;
        for (const wchar_t character : definition.key) {
            CHECK(character <= 0x7F);
            name.push_back(static_cast<char>(character));
        }
        const auto* record = find_adaptive_setting(name);
        CHECK(record);
        CHECK(record->role == AdaptiveRole::adaptive_knob);
        CHECK(record->source_trust == SourceTrustClass::verified);
        CHECK(record->evidence_state == AdaptiveEvidenceState::test_required);
        CHECK(record->safe_minimum == definition.minimum);
        CHECK(record->safe_maximum == definition.maximum);
        CHECK(record->rollback_possible);
        CHECK(!adaptive_setting_active_ready(
            *record, AdaptiveCapabilityState::available));
    }
    return EXIT_SUCCESS;
}
