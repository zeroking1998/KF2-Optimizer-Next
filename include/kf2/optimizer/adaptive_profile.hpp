#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "kf2/config/adaptive_locks.hpp"
#include "kf2/config/config_preview.hpp"
#include "kf2/optimizer/optimizer_engine.hpp"

namespace kf2::optimizer {

[[nodiscard]] int adaptive_profile_quality(Profile profile) noexcept;
[[nodiscard]] std::string_view adaptive_profile_token(Profile profile) noexcept;
[[nodiscard]] std::wstring_view adaptive_profile_label(Profile profile) noexcept;
[[nodiscard]] std::optional<Profile> parse_adaptive_profile(
    std::string_view token) noexcept;

// Selects the nearest named profile that stays completely inside the user's
// quality interval. No profile is returned when the interval cannot be
// represented safely by a verified named profile.
[[nodiscard]] std::optional<Profile> bound_adaptive_profile(
    Profile requested, int minimum_quality, int maximum_quality) noexcept;

struct AdaptiveProfilePersistenceInput {
    Profile current{Profile::balanced};
    Profile recommended{Profile::balanced};
    bool active_gameplay{false};
    bool telemetry_valid{false};
    bool recovery_eligible{false};
    std::uint64_t now_ns{0};
};

// Adaptive's fast state machine may react every telemetry tick, while the
// persisted profile is intentionally a slow, next-launch baseline. This gate
// prevents menu/loading samples and short pressure oscillations from rewriting
// settings.ini back and forth.
class AdaptiveProfilePersistenceGate final {
public:
    static constexpr std::uint64_t kDegradeDwellNs = 8'000'000'000ULL;
    static constexpr std::uint64_t kRecoveryDwellNs = 45'000'000'000ULL;
    static constexpr std::uint64_t kCommitCooldownNs = 30'000'000'000ULL;

    [[nodiscard]] std::optional<Profile> evaluate(
        const AdaptiveProfilePersistenceInput& input) noexcept;
    void reset() noexcept;

private:
    std::optional<Profile> candidate_;
    std::uint64_t candidate_since_ns_{0};
    std::uint64_t last_emitted_ns_{0};
    bool emitted_{false};
};

// Persisted per-setting locks are absolute: Adaptive silently omits every
// locked value instead of overriding the user or failing the complete plan.
[[nodiscard]] std::vector<config::RequestedChange>
filter_adaptive_locked_changes(
    std::span<const config::RequestedChange> changes,
    const config::AdaptiveLocks& locks);

}  // namespace kf2::optimizer
