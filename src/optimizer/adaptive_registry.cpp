#include "kf2/optimizer/adaptive_registry.hpp"

#include "kf2/config/kf2_catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kf2::optimizer {
namespace {

struct SpecItem {
    std::string_view section;
    std::string_view name;
};

constexpr SpecItem kSpecItems[]{
#define KF2_ADAPTIVE_SPEC_ITEM(section, name) {section, name},
#include "adaptive_spec_items.inc"
#undef KF2_ADAPTIVE_SPEC_ITEM
};

constexpr std::array kAdditionalModelItems{
    SpecItem{"5", "FrameTime"},
    SpecItem{"5", "MedianFrameTime"},
    SpecItem{"5", "P95FrameTime"},
    SpecItem{"5", "P99FrameTime"},
    SpecItem{"5", "OnePercentLow"},
    SpecItem{"5", "PointOnePercentLow"},
    SpecItem{"5", "FrameTimeVariance"},
    SpecItem{"5", "SingleFrameSpikes"},
    SpecItem{"5", "StutterEpisodes"},
    SpecItem{"6", "PredictionUncertainty"},
    SpecItem{"7", "ContradictingSignals"},
    SpecItem{"94", "ProcessIdentity"},
    SpecItem{"94", "ProcessStartIdentity"},
    SpecItem{"94", "AdapterLuid"},
    SpecItem{"94", "TelemetryFreshness"},
    SpecItem{"91", "SessionSafetyState"},
    SpecItem{"97", "LastSafeState"},
    SpecItem{"97", "OriginalState"},
    SpecItem{"98", "ControllerIterationBudget"},
    SpecItem{"93", "EnemyAudioCues"},
    SpecItem{"93", "AttackWarnings"},
    SpecItem{"93", "GameplayRelevantSound"},
    SpecItem{"93", "UnknownProcessMemoryAddresses"},
    SpecItem{"93", "ReadProcessMemory"},
    SpecItem{"93", "WriteProcessMemory"},
    SpecItem{"93", "RemoteThreads"},
    SpecItem{"93", "Detours"},
    SpecItem{"93", "Injection"},
    SpecItem{"93", "AntiCheatBypass"},
    SpecItem{"93", "KernelDriverSecurityBypass"},
    SpecItem{"101", "DecisionHistory"},
    SpecItem{"102", "HotPathBudget"},
    SpecItem{"103", "UserExplainability"},
    SpecItem{"111", "CompletenessContract"},
    SpecItem{"112", "DualLoopController"},
    SpecItem{"113", "ActionConflictResolver"},
    SpecItem{"114", "QualityDebtLedger"},
    SpecItem{"115", "TemporalQualityMemory"},
    SpecItem{"116", "ImportanceAwareVisualPriority"},
    SpecItem{"117", "SubsystemBudgetReservations"},
    SpecItem{"118", "BoundedBurstReserve"},
    SpecItem{"119", "VerifiedSessionPatternLearning"},
    SpecItem{"120", "CPUIdentity"},
    SpecItem{"120", "GPUIdentity"},
    SpecItem{"120", "RAMClass"},
    SpecItem{"120", "GPUDriver"},
    SpecItem{"120", "WindowsBuild"},
    SpecItem{"120", "KF2Build"},
    SpecItem{"120", "OptimizerBuild"},
    SpecItem{"120", "FlexRuntimeIdentity"},
    SpecItem{"120", "RelevantConfigGeneration"},
    SpecItem{"121", "ColdStartConfidenceRamp"},
    SpecItem{"122", "CausalImpactEstimator"},
    SpecItem{"123", "CrashCorrelationQuarantine"},
    SpecItem{"124", "LongSessionDriftDetection"},
    SpecItem{"125", "IOStreamingClassifier"},
    SpecItem{"126", "NetworkLagIsolation"},
    SpecItem{"127", "OnlineSessionLatch"},
    SpecItem{"128", "HostListenServerPolicy"},
    SpecItem{"129", "OnlineSafePromotionPolicy"},
    SpecItem{"130", "ConfigVsLiveSeparation"},
    SpecItem{"131", "DisplayRefreshContext"},
    SpecItem{"132", "TargetStabilityBand"},
    SpecItem{"133", "TelemetryOverheadBudget"},
    SpecItem{"134", "DeterministicReplay"},
    SpecItem{"135", "CounterfactualLab"},
    SpecItem{"136", "DecisionHistoryUI"},
    SpecItem{"137", "AdaptiveSelfPerformance"},
    SpecItem{"138", "AbsoluteCompletenessRule"},
};

int section_number(std::string_view section) noexcept {
    int value = 0;
    const auto result = std::from_chars(
        section.data(), section.data() + section.size(), value);
    return result.ec == std::errc{} ? value : 0;
}

std::string category_for(int section) {
    if (section == 4) return "user-control";
    if (section == 5) return "frame-time-control";
    if (section == 6) return "prediction";
    if (section == 7) return "bottleneck-analysis";
    if (section >= 8 && section <= 13) return "hardware-and-headroom";
    if (section == 14) return "spatial-visibility-occlusion";
    if (section >= 15 && section <= 29) return "living-enemy-visuals";
    if (section >= 30 && section <= 39) return "physics-corpses-ragdolls-gibs";
    if (section >= 40 && section <= 53) return "gore-decals-and-effects";
    if (section >= 54 && section <= 65) return "particles";
    if (section >= 66 && section <= 78) return "flex";
    if (section >= 79 && section <= 80) return "profiles-and-view-distance";
    if (section >= 81 && section <= 90) return "adaptive-control-quality";
    if (section >= 91 && section <= 99) return "safety-recovery-and-trust";
    if (section == 100) return "optimizer-overhead";
    if (section >= 101 && section <= 103) return "logging-ui-and-hot-path";
    if (section >= 111 && section <= 119) return "controller-orchestration";
    if (section == 120) return "build-runtime-fingerprint";
    if (section >= 121 && section <= 138) return "extended-safety-and-evidence";
    return "target-model";
}

AdaptiveRole role_for(int section) noexcept {
    if (section == 5) return AdaptiveRole::sensor;
    if (section == 6 || (section >= 8 && section <= 13)) {
        return AdaptiveRole::context;
    }
    if (section == 7 || section == 86 || section == 87 ||
        section == 94 || section == 100) {
        return AdaptiveRole::diagnostic;
    }
    if (section == 88 || section == 96 || section == 97) {
        return AdaptiveRole::recovery;
    }
    if (section == 114 || section == 123) return AdaptiveRole::recovery;
    if (section == 115 || section == 116 || section == 119 ||
        section == 131) {
        return AdaptiveRole::context;
    }
    if (section == 120) return AdaptiveRole::build_release;
    if (section == 101 || section == 103 || section == 111 ||
        section == 122 || section == 124 || section == 125 ||
        section == 134 || section == 135 || section == 136 ||
        section == 138) {
        return AdaptiveRole::diagnostic;
    }
    if (section == 102 || section == 112 || section == 113 ||
        section == 117 || section == 118 || (section >= 121 && section <= 133) ||
        section == 137) {
        return AdaptiveRole::safety_guard;
    }
    if ((section >= 81 && section <= 85) || section == 89 ||
        section == 90 || (section >= 91 && section <= 95) ||
        section == 98 || section == 99) {
        return AdaptiveRole::safety_guard;
    }
    return AdaptiveRole::adaptive_knob;
}

bool contains(std::string_view value, std::string_view needle) noexcept {
    return value.find(needle) != std::string_view::npos;
}

bool one_of(std::string_view value,
            std::span<const std::string_view> values) noexcept {
    return std::ranges::find(values, value) != values.end();
}

bool ascii_iequals(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto fold = [](unsigned char character) noexcept {
            return character >= 'A' && character <= 'Z'
                ? static_cast<unsigned char>(character - 'A' + 'a')
                : character;
        };
        if (fold(static_cast<unsigned char>(left[index])) !=
            fold(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool protected_name(std::string_view value) noexcept;

constexpr std::array<std::string_view, 33> kPermanentProtected{
    "EnemyCount", "SpawnCount", "SpawnRate", "SpawnLogic", "Difficulty",
    "AI", "Perception", "Navigation", "RealMovement", "AttackTiming",
    "DamageTruth", "HitTruth", "HitZones", "GameplayCollision",
    "NetworkReplicationTruth", "NetworkAuthority", "ServerAuthority",
    "HitCollision", "MovementPhysics", "AttackPhysics", "ServerPhysics",
    "NetworkPhysics", "EnemyAudioCues", "AttackWarnings",
    "GameplayRelevantSound", "UnknownProcessMemoryAddresses",
    "ReadProcessMemory", "WriteProcessMemory", "RemoteThreads", "Detours",
    "Injection", "AntiCheatBypass", "KernelDriverSecurityBypass",
};

bool protected_name(std::string_view value) noexcept {
    return std::ranges::any_of(kPermanentProtected,
        [value](std::string_view candidate) noexcept {
            return ascii_iequals(value, candidate);
        });
}

constexpr std::array<std::string_view, 15> kSafeControllerSettings{
    "OperatingMode", "TargetFPS", "TargetFrameTime", "AdaptiveEnabled",
    "AdaptiveAggressiveness", "MinimumQuality", "MaximumQuality",
    "QualityChangeBudget", "PerformanceHeadroom", "EmergencyModeEnabled",
    "QualityRecoveryEnabled", "ManualLocksEnabled", "ShadowMode",
    "CalibrationEnabled", "AdaptiveLogging",
};

constexpr std::array<std::string_view, 15> kVerifiedConfigOnly{
    "MaxDeadBodies", "DynamicDecals", "StaticDecals",
    "DecalCullDistanceScale", "ParticleLODBias", "SkeletalMeshLODBias",
    "MaxPhysicsSubsteps", "MaxBloodDecals", "MaxWoundDecals",
    "BloodDecalLifetime", "WoundDecalLifetime", "GibLifetime",
    "CorpseLifetime", "ShadowResolution", "TextureMemoryBudget",
};

constexpr std::array<std::string_view, 15> kContextOnlyItems{
    "Distance", "ScreenSize", "Visibility", "Occlusion",
    "ViewDirection", "ObjectType", "CurrentPerformancePressure",
    "PartialOcclusionRatio", "FullOcclusionState", "OcclusionDuration",
    "OccluderStability", "CameraMotion", "PlayerViewRotation",
    "PredictedVisibilityReturn", "CurrentBottleneck",
};

AdaptiveSettingRecord make_record(const SpecItem& item) {
    const int section = section_number(item.section);
    AdaptiveSettingRecord record;
    record.name = item.name;
    record.category = category_for(section);
    record.role = role_for(section);

    record.distance_aware = contains(item.name, "Near") ||
        contains(item.name, "Mid") || contains(item.name, "Far") ||
        contains(item.name, "Distance");
    record.visibility_aware = contains(item.name, "Visible") ||
        contains(item.name, "Visibility") || contains(item.name, "Occlu");
    record.offscreen_aware = contains(item.name, "Offscreen") ||
        contains(item.name, "BehindCamera");

    if (protected_name(item.name)) {
        record.role = AdaptiveRole::permanently_protected;
        record.source_trust = SourceTrustClass::verified;
        record.safety_class = AdaptiveSafetyClass::permanent_blocked;
        record.evidence_state = AdaptiveEvidenceState::permanent_blocked;
        record.offline_status = OfflineSafetyStatus::blocked;
        record.online_status = OnlineSafetyStatus::permanent_blocked;
        record.actuation_class = ActuationClass::permanent_blocked;
        record.gameplay_risk = ImpactLevel::high;
        return record;
    }

    if (one_of(item.name, kContextOnlyItems)) {
        record.role = AdaptiveRole::context;
    }

    if (item.name == "OnlineAdaptiveAllowed") {
        record.role = AdaptiveRole::diagnostic;
        record.source = "Legacy migration input only";
        record.source_trust = SourceTrustClass::verified;
        record.safety_class = AdaptiveSafetyClass::protected_value;
        record.evidence_state = AdaptiveEvidenceState::not_available;
        record.offline_status = OfflineSafetyStatus::blocked;
        record.online_status = OnlineSafetyStatus::blocked;
        record.actuation_class = ActuationClass::shadow_only;
        record.visual_impact = ImpactLevel::none;
        record.gameplay_risk = ImpactLevel::none;
        record.rollback_possible = false;
        record.confidence = 1.0;
        return record;
    }

    if (one_of(item.name, kSafeControllerSettings)) {
        record.source = "Portable optimizer setting";
        record.source_trust = SourceTrustClass::verified;
        record.safety_class = AdaptiveSafetyClass::safe;
        record.evidence_state = AdaptiveEvidenceState::proven;
        record.offline_status = OfflineSafetyStatus::safe;
        record.online_status = OnlineSafetyStatus::safe;
        record.actuation_class = ActuationClass::config_only;
        record.visual_impact = ImpactLevel::none;
        record.gameplay_risk = ImpactLevel::none;
        record.change_cost = ImpactLevel::very_low;
        record.rollback_possible = true;
        record.confidence = 1.0;
        return record;
    }

    if (one_of(item.name, kVerifiedConfigOnly)) {
        record.source = "Verified KF2 INI catalog";
        record.source_trust = SourceTrustClass::verified;
        record.safety_class = AdaptiveSafetyClass::conditional;
        record.evidence_state = AdaptiveEvidenceState::test_required;
        record.offline_status = OfflineSafetyStatus::test_required;
        record.online_status = OnlineSafetyStatus::test_required;
        record.actuation_class = ActuationClass::config_only;
        record.rollback_possible = true;
        record.canary_allowed = false;
        return record;
    }

    if (record.role != AdaptiveRole::adaptive_knob) {
        record.safety_class = AdaptiveSafetyClass::protected_value;
        record.evidence_state = AdaptiveEvidenceState::not_available;
        record.offline_status = OfflineSafetyStatus::blocked;
        record.online_status = OnlineSafetyStatus::blocked;
        record.actuation_class = ActuationClass::shadow_only;
        record.visual_impact = ImpactLevel::none;
        record.rollback_possible = false;
    }
    return record;
}

std::vector<AdaptiveSettingRecord> build_registry() {
    std::vector<AdaptiveSettingRecord> records;
    records.reserve(std::size(kSpecItems) + kAdditionalModelItems.size());
    std::unordered_set<std::string> seen;
    const auto append = [&](const SpecItem& item) {
        if (seen.insert(std::string{item.name}).second) {
            records.push_back(make_record(item));
        }
    };
    for (const auto& item : kSpecItems) append(item);
    for (const auto& item : kAdditionalModelItems) append(item);
    for (const auto& definition : config::all_settings()) {
        std::string name;
        name.reserve(definition.key.size());
        bool ascii = true;
        for (const wchar_t character : definition.key) {
            if (character > 0x7F) {
                ascii = false;
                break;
            }
            name.push_back(static_cast<char>(character));
        }
        if (!ascii || name.empty()) continue;
        auto found = std::ranges::find(records, name,
            &AdaptiveSettingRecord::name);
        if (found == records.end()) {
            AdaptiveSettingRecord record;
            record.name = name;
            record.category = "verified-kf2-config";
            records.push_back(std::move(record));
            found = std::prev(records.end());
            seen.insert(name);
        }
        if (found->role == AdaptiveRole::permanently_protected) continue;
        found->role = AdaptiveRole::adaptive_knob;
        found->source = "Verified KF2 INI catalog";
        found->source_trust = SourceTrustClass::verified;
        found->safe_minimum = definition.minimum;
        found->safe_maximum = definition.maximum;
        if (definition.editor_step > 0.0) {
            found->step_size = definition.editor_step;
        }
        found->safety_class = AdaptiveSafetyClass::conditional;
        found->evidence_state = AdaptiveEvidenceState::test_required;
        found->offline_status = OfflineSafetyStatus::test_required;
        found->online_status = OnlineSafetyStatus::test_required;
        found->actuation_class = ActuationClass::config_only;
        found->rollback_possible = true;
        found->gameplay_risk = definition.adaptive_allowed
            ? ImpactLevel::medium : ImpactLevel::high;
    }
    return records;
}

}  // namespace

std::span<const AdaptiveSettingRecord> adaptive_target_registry() noexcept {
    static const std::vector<AdaptiveSettingRecord> records = build_registry();
    return records;
}

const AdaptiveSettingRecord* find_adaptive_setting(
    std::string_view name) noexcept {
    const auto records = adaptive_target_registry();
    const auto found = std::ranges::find(records, name,
        &AdaptiveSettingRecord::name);
    return found == records.end() ? nullptr : &*found;
}

bool adaptive_setting_active_ready(const AdaptiveSettingRecord& setting,
                                   AdaptiveCapabilityState capability) noexcept {
    const bool complete_value_contract = setting.current_value &&
        setting.target_value && setting.safe_minimum && setting.safe_maximum &&
        setting.step_size && setting.restore_value &&
        *setting.safe_minimum <= *setting.safe_maximum &&
        *setting.step_size > 0.0 &&
        *setting.current_value >= *setting.safe_minimum &&
        *setting.current_value <= *setting.safe_maximum &&
        *setting.target_value >= *setting.safe_minimum &&
        *setting.target_value <= *setting.safe_maximum;
    if (capability != AdaptiveCapabilityState::available ||
        setting.role != AdaptiveRole::adaptive_knob ||
        setting.source_trust != SourceTrustClass::verified ||
        setting.manual_lock != ManualLockState::automatic ||
        setting.evidence_state != AdaptiveEvidenceState::proven ||
        !complete_value_contract || !setting.canary_allowed ||
        !setting.rollback_possible ||
        setting.maximum_change_rate <= 0.0 ||
        setting.confidence < 0.80 || setting.measurement_count == 0 ||
        setting.build_binding.empty() || setting.runtime_binding.empty() ||
        (setting.safety_class != AdaptiveSafetyClass::safe &&
         setting.safety_class != AdaptiveSafetyClass::conditional) ||
        (setting.actuation_class != ActuationClass::live_safe &&
         setting.actuation_class != ActuationClass::live_conditional)) {
        return false;
    }
    return setting.offline_status == OfflineSafetyStatus::safe ||
           setting.online_status == OnlineSafetyStatus::safe;
}

std::string_view adaptive_role_name(AdaptiveRole role) noexcept {
    switch (role) {
        case AdaptiveRole::sensor: return "SENSOR";
        case AdaptiveRole::context: return "CONTEXT";
        case AdaptiveRole::adaptive_knob: return "ADAPTIVE_KNOB";
        case AdaptiveRole::safety_guard: return "SAFETY_GUARD";
        case AdaptiveRole::recovery: return "RECOVERY";
        case AdaptiveRole::diagnostic: return "DIAGNOSTIC";
        case AdaptiveRole::build_release: return "BUILD_RELEASE";
        case AdaptiveRole::permanently_protected:
            return "PERMANENTLY_PROTECTED";
    }
    return "DIAGNOSTIC";
}

std::string_view adaptive_safety_class_name(
    AdaptiveSafetyClass safety) noexcept {
    switch (safety) {
        case AdaptiveSafetyClass::safe: return "SAFE";
        case AdaptiveSafetyClass::conditional: return "CONDITIONAL";
        case AdaptiveSafetyClass::lab: return "LAB";
        case AdaptiveSafetyClass::protected_value: return "PROTECTED";
        case AdaptiveSafetyClass::permanent_blocked:
            return "PERMANENT_BLOCKED";
    }
    return "LAB";
}

std::string_view adaptive_evidence_state_name(
    AdaptiveEvidenceState evidence) noexcept {
    switch (evidence) {
        case AdaptiveEvidenceState::proven: return "PROVEN";
        case AdaptiveEvidenceState::shadow: return "SHADOW";
        case AdaptiveEvidenceState::test_required: return "TEST_REQUIRED";
        case AdaptiveEvidenceState::not_available: return "NOT_AVAILABLE";
        case AdaptiveEvidenceState::permanent_blocked:
            return "PERMANENT_BLOCKED";
    }
    return "NOT_AVAILABLE";
}

std::string_view adaptive_actuation_class_name(
    ActuationClass actuation) noexcept {
    switch (actuation) {
        case ActuationClass::live_safe: return "LIVE_SAFE";
        case ActuationClass::live_conditional: return "LIVE_CONDITIONAL";
        case ActuationClass::restart_required: return "RESTART_REQUIRED";
        case ActuationClass::config_only: return "CONFIG_ONLY";
        case ActuationClass::shadow_only: return "SHADOW_ONLY";
        case ActuationClass::permanent_blocked: return "PERMANENT_BLOCKED";
    }
    return "SHADOW_ONLY";
}

}  // namespace kf2::optimizer
