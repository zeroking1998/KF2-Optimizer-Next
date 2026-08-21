#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kf2::optimizer {

enum class AdaptiveRole {
    sensor,
    context,
    adaptive_knob,
    safety_guard,
    recovery,
    diagnostic,
    build_release,
    permanently_protected,
};

enum class SourceTrustClass { verified, measured, model_only, unavailable };
enum class AdaptiveSafetyClass { safe, conditional, lab, protected_value,
                                 permanent_blocked };
enum class AdaptiveEvidenceState { proven, shadow, test_required, not_available,
                                   permanent_blocked };
enum class OfflineSafetyStatus { untested, test_required, safe, blocked };
enum class OnlineSafetyStatus { untested, test_required, safe, blocked,
                                permanent_blocked };
enum class ActuationClass { live_safe, live_conditional, restart_required,
                            config_only, shadow_only, permanent_blocked };
enum class ManualLockState { automatic, lock_current, lock_minimum,
                             lock_maximum, manual_value };
enum class ImpactLevel { none, very_low, low, medium, high, unknown };
enum class AdaptiveCapabilityState { available, unavailable, shadow,
                                     restart_required, failed };

struct AdaptiveSettingRecord {
    std::string name;
    std::string category;
    AdaptiveRole role{AdaptiveRole::adaptive_knob};

    std::string source{"Master specification"};
    SourceTrustClass source_trust{SourceTrustClass::model_only};

    std::optional<double> original_value;
    std::optional<double> default_value;
    std::optional<double> current_value;
    std::optional<double> target_value;
    std::optional<double> safe_minimum;
    std::optional<double> safe_maximum;
    std::optional<double> step_size;

    std::optional<double> cpu_effect;
    std::optional<double> gpu_effect;
    std::optional<double> vram_effect;
    std::optional<double> ram_effect;
    std::optional<double> physics_effect;
    std::optional<double> flex_effect;
    std::optional<double> streaming_effect;
    std::optional<double> stutter_effect;

    ImpactLevel visual_impact{ImpactLevel::unknown};
    ImpactLevel gameplay_risk{ImpactLevel::unknown};
    ImpactLevel change_cost{ImpactLevel::unknown};

    bool distance_aware{false};
    bool visibility_aware{false};
    bool offscreen_aware{false};

    ManualLockState manual_lock{ManualLockState::automatic};
    std::optional<double> quality_floor;

    OfflineSafetyStatus offline_status{OfflineSafetyStatus::test_required};
    OnlineSafetyStatus online_status{OnlineSafetyStatus::test_required};
    AdaptiveSafetyClass safety_class{AdaptiveSafetyClass::lab};
    AdaptiveEvidenceState evidence_state{AdaptiveEvidenceState::shadow};
    ActuationClass actuation_class{ActuationClass::shadow_only};

    std::uint32_t cooldown_ms{0};
    double hysteresis{0.0};
    std::uint32_t minimum_hold_ms{0};
    double maximum_change_rate{0.0};
    std::uint32_t direction_change_delay_ms{0};
    std::uint32_t recovery_delay_ms{0};

    bool canary_allowed{false};
    bool rollback_possible{false};
    bool gameplay_neutral{false};
    bool server_independent{false};
    std::string online_evidence_version;

    std::optional<double> last_measured_effect;
    std::optional<double> average_measured_effect;
    double confidence{0.0};
    std::uint32_t measurement_count{0};

    std::optional<double> restore_value;
    std::uint64_t restore_generation{0};

    std::string build_binding;
    std::string runtime_binding;
    std::string driver_binding;
};

[[nodiscard]] std::span<const AdaptiveSettingRecord>
adaptive_target_registry() noexcept;
[[nodiscard]] const AdaptiveSettingRecord* find_adaptive_setting(
    std::string_view name) noexcept;
[[nodiscard]] bool adaptive_setting_active_ready(
    const AdaptiveSettingRecord& setting,
    AdaptiveCapabilityState capability) noexcept;
[[nodiscard]] std::string_view adaptive_role_name(AdaptiveRole role) noexcept;
[[nodiscard]] std::string_view adaptive_safety_class_name(
    AdaptiveSafetyClass safety) noexcept;
[[nodiscard]] std::string_view adaptive_evidence_state_name(
    AdaptiveEvidenceState evidence) noexcept;
[[nodiscard]] std::string_view adaptive_actuation_class_name(
    ActuationClass actuation) noexcept;

}  // namespace kf2::optimizer
