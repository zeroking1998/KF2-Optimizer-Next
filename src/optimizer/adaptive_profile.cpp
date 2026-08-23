#include "kf2/optimizer/adaptive_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "kf2/config/setting_catalog.hpp"

namespace kf2::optimizer {

int adaptive_profile_quality(Profile profile) noexcept {
    switch (profile) {
        case Profile::stability: return 100;
        case Profile::balanced: return 85;
        case Profile::high_performance: return 10;
        case Profile::custom: return -1;
    }
    return -1;
}

std::string_view adaptive_profile_token(Profile profile) noexcept {
    switch (profile) {
        case Profile::stability: return "stability";
        case Profile::balanced: return "balanced";
        case Profile::high_performance: return "high_performance";
        case Profile::custom: return "custom";
    }
    return "custom";
}

std::wstring_view adaptive_profile_label(Profile profile) noexcept {
    switch (profile) {
        case Profile::stability: return L"Stability";
        case Profile::balanced: return L"Balanced";
        case Profile::high_performance: return L"High performance";
        case Profile::custom: return L"Custom";
    }
    return L"Custom";
}

std::optional<Profile> parse_adaptive_profile(
    std::string_view token) noexcept {
    if (token == "stability") return Profile::stability;
    if (token == "balanced") return Profile::balanced;
    if (token == "high_performance") return Profile::high_performance;
    return std::nullopt;
}

std::optional<Profile> bound_adaptive_profile(
    Profile requested, int minimum_quality, int maximum_quality) noexcept {
    if (minimum_quality < 10 || maximum_quality > 100 ||
        minimum_quality > maximum_quality) {
        return std::nullopt;
    }
    if (requested == Profile::custom) requested = Profile::balanced;
    constexpr std::array profiles{
        Profile::stability, Profile::balanced, Profile::high_performance};
    const int requested_quality = adaptive_profile_quality(requested);
    std::optional<Profile> selected;
    int selected_distance = 101;
    for (const Profile profile : profiles) {
        const int quality = adaptive_profile_quality(profile);
        if (quality < minimum_quality || quality > maximum_quality) continue;
        const int distance = std::abs(quality - requested_quality);
        if (!selected || distance < selected_distance ||
            (distance == selected_distance &&
             quality > adaptive_profile_quality(*selected))) {
            selected = profile;
            selected_distance = distance;
        }
    }
    return selected;
}

std::optional<Profile> AdaptiveProfilePersistenceGate::evaluate(
    const AdaptiveProfilePersistenceInput& input) noexcept {
    const bool raises_quality =
        adaptive_profile_quality(input.recommended) >
        adaptive_profile_quality(input.current);
    if (!input.active_gameplay || !input.telemetry_valid ||
        input.current == Profile::custom ||
        input.recommended == Profile::custom ||
        input.current == input.recommended ||
        (raises_quality && !input.recovery_eligible)) {
        candidate_.reset();
        candidate_since_ns_ = 0;
        return std::nullopt;
    }

    if (!candidate_ || *candidate_ != input.recommended) {
        candidate_ = input.recommended;
        candidate_since_ns_ = input.now_ns;
        return std::nullopt;
    }
    if (input.now_ns < candidate_since_ns_) {
        reset();
        return std::nullopt;
    }

    const bool reduces_quality =
        adaptive_profile_quality(input.recommended) <
        adaptive_profile_quality(input.current);
    const std::uint64_t dwell_ns = reduces_quality
        ? kDegradeDwellNs : kRecoveryDwellNs;
    if (input.now_ns - candidate_since_ns_ < dwell_ns) {
        return std::nullopt;
    }
    if (emitted_ &&
        (input.now_ns < last_emitted_ns_ ||
         input.now_ns - last_emitted_ns_ < kCommitCooldownNs)) {
        return std::nullopt;
    }

    const Profile selected = *candidate_;
    candidate_.reset();
    candidate_since_ns_ = 0;
    last_emitted_ns_ = input.now_ns;
    emitted_ = true;
    return selected;
}

void AdaptiveProfilePersistenceGate::reset() noexcept {
    candidate_.reset();
    candidate_since_ns_ = 0;
}

std::vector<config::RequestedChange> filter_adaptive_locked_changes(
    std::span<const config::RequestedChange> changes,
    const config::AdaptiveLocks& locks) {
    std::vector<config::RequestedChange> filtered;
    filtered.reserve(changes.size());
    for (const auto& change : changes) {
        const auto token = config::setting_token(change.id);
        const auto lock = locks.find(token);
        if (lock != locks.end() &&
            lock->second != ManualLockState::automatic) {
            continue;
        }
        filtered.push_back(change);
    }
    return filtered;
}

}  // namespace kf2::optimizer
