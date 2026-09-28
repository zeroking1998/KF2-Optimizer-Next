#include "kf2/update/update_controller.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace kf2::update {
namespace {

detail::UpdateControllerAllocationHook allocation_hook{};

void run_allocation_hook() {
    if (allocation_hook != nullptr) allocation_hook();
}

std::int64_t automatic_failure_retry_seconds(
    std::uint32_t failure_count) noexcept {
    std::int64_t delay = kAutomaticFailureRetryInitialSeconds;
    for (std::uint32_t failure = 1;
         failure < failure_count &&
         delay < kAutomaticFailureRetryMaximumSeconds;
         ++failure) {
        delay = std::min(delay * 2,
                         kAutomaticFailureRetryMaximumSeconds);
    }
    return delay;
}

}  // namespace

void detail::set_update_controller_allocation_hook_for_testing(
    UpdateControllerAllocationHook hook) noexcept {
    allocation_hook = hook;
}

UpdateController::UpdateController(std::string installed_version) {
    snapshot_.installed_version = std::move(installed_version);
}

void UpdateController::restore_preferences(
    bool automatic_checks_enabled,
    std::int64_t last_check_unix_seconds,
    bool cached_check_completed,
    std::string cached_available_version,
    std::string ignored_version,
    std::int64_t last_attempt_unix_seconds,
    std::uint32_t automatic_failure_count) {
    // Build the complete result before touching the live snapshot. Its move
    // assignment is non-throwing, so an allocation failure leaves the prior
    // coherent controller state intact.
    run_allocation_hook();
    UpdateSnapshot next = snapshot_;
    next.automatic_checks_enabled = automatic_checks_enabled;
    next.last_check_unix_seconds = last_check_unix_seconds > 0
        ? last_check_unix_seconds : 0;
    next.cached_check_completed = cached_check_completed &&
        next.last_check_unix_seconds > 0;
    next.last_attempt_unix_seconds = last_attempt_unix_seconds > 0
        ? last_attempt_unix_seconds : 0;
    next.automatic_failure_count =
        next.last_attempt_unix_seconds > 0
            ? automatic_failure_count : 0;
    next.ignored_version = std::move(ignored_version);
    if (next.cached_check_completed &&
        !cached_available_version.empty()) {
        next.cached_available_version =
            std::move(cached_available_version);
        next.status = L"A new version was found during the last check.";
        next.dismissed =
            *next.cached_available_version == next.ignored_version;
    } else if (next.cached_check_completed) {
        next.cached_available_version.reset();
        next.status = L"No newer version was available at the last check.";
    }
    static_assert(std::is_nothrow_move_assignable_v<UpdateSnapshot>);
    snapshot_ = std::move(next);
}

CheckStart UpdateController::begin_check(
    CheckTrigger trigger, std::int64_t now_unix_seconds) {
    if (snapshot_.phase == UpdatePhase::checking ||
        snapshot_.phase == UpdatePhase::installing) return CheckStart::busy;
    if (trigger == CheckTrigger::automatic) {
        if (!snapshot_.automatic_checks_enabled) {
            return CheckStart::automatic_disabled;
        }
        if (snapshot_.last_check_unix_seconds > 0 && now_unix_seconds >= 0 &&
            now_unix_seconds - snapshot_.last_check_unix_seconds <
                kAutomaticCheckIntervalSeconds) return CheckStart::throttled;
        if (snapshot_.automatic_failure_count > 0 &&
            snapshot_.last_attempt_unix_seconds > 0 &&
            now_unix_seconds >= 0 &&
            now_unix_seconds - snapshot_.last_attempt_unix_seconds <
                automatic_failure_retry_seconds(
                    snapshot_.automatic_failure_count)) {
            return CheckStart::throttled;
        }
    }
    const auto next_last_attempt = now_unix_seconds > 0
        ? now_unix_seconds : snapshot_.last_attempt_unix_seconds;
    auto next_failure_count = snapshot_.automatic_failure_count;
    if (trigger == CheckTrigger::automatic && now_unix_seconds > 0 &&
        next_failure_count <
            std::numeric_limits<std::uint32_t>::max()) {
        ++next_failure_count;
    }
    run_allocation_hook();
    std::wstring next_status = L"Checking official GitHub Releases...";
    snapshot_.last_attempt_unix_seconds = next_last_attempt;
    snapshot_.automatic_failure_count = next_failure_count;
    snapshot_.phase = UpdatePhase::checking;
    static_assert(std::is_nothrow_move_assignable_v<std::wstring>);
    snapshot_.status = std::move(next_status);
    snapshot_.available_release.reset();
    return CheckStart::started;
}

void UpdateController::complete_check(
    Result<std::optional<ReleaseInfo>> result) {
    if (snapshot_.phase != UpdatePhase::checking) return;
    if (!result.has_value()) {
        snapshot_.phase = UpdatePhase::error;
        snapshot_.status = result.error().message;
        return;
    }
    snapshot_.last_check_unix_seconds =
        snapshot_.last_attempt_unix_seconds > 0
            ? snapshot_.last_attempt_unix_seconds
            : snapshot_.last_check_unix_seconds;
    snapshot_.automatic_failure_count = 0;
    snapshot_.available_release = std::move(result.value());
    snapshot_.cached_check_completed = true;
    snapshot_.cached_available_version = snapshot_.available_release
        ? std::optional<std::string>{snapshot_.available_release->version}
        : std::nullopt;
    snapshot_.dismissed = snapshot_.cached_available_version &&
        *snapshot_.cached_available_version == snapshot_.ignored_version;
    if (snapshot_.available_release) {
        snapshot_.phase = UpdatePhase::available;
        snapshot_.status = snapshot_.available_release->install_block_reason.empty()
            ? L"A new version is available."
            : snapshot_.available_release->install_block_reason;
    } else {
        snapshot_.phase = UpdatePhase::current;
        snapshot_.status = L"The installed version is current.";
    }
}

void UpdateController::set_automatic_checks_enabled(bool enabled) noexcept {
    snapshot_.automatic_checks_enabled = enabled;
}

bool UpdateController::begin_install_with_user_consent() {
    if (snapshot_.phase != UpdatePhase::available ||
        !snapshot_.available_release ||
        snapshot_.dismissed ||
        !snapshot_.available_release->asset.has_value() ||
        !snapshot_.available_release->install_block_reason.empty()) return false;
    run_allocation_hook();
    std::wstring next_status =
        L"Downloading and verifying the approved update...";
    snapshot_.phase = UpdatePhase::installing;
    snapshot_.status = std::move(next_status);
    return true;
}

void UpdateController::complete_install_failure(std::wstring message) {
    snapshot_.phase = snapshot_.available_release
        ? UpdatePhase::available : UpdatePhase::error;
    snapshot_.status = std::move(message);
}

void UpdateController::dismiss() noexcept {
    if (snapshot_.phase == UpdatePhase::available ||
        snapshot_.cached_available_version) {
        snapshot_.dismissed = true;
    }
}

void UpdateController::ignore_available_version() {
    if (!snapshot_.cached_available_version) return;
    run_allocation_hook();
    std::string next_ignored_version = *snapshot_.cached_available_version;
    static_assert(std::is_nothrow_move_assignable_v<std::string>);
    snapshot_.ignored_version = std::move(next_ignored_version);
    snapshot_.dismissed = true;
}

const UpdateSnapshot& UpdateController::snapshot() const noexcept {
    return snapshot_;
}

}  // namespace kf2::update
