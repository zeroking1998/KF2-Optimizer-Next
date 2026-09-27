#include "kf2/update/update_controller.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace kf2::update {
namespace {

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
    std::uint32_t automatic_failure_count) noexcept {
    snapshot_.automatic_checks_enabled = automatic_checks_enabled;
    snapshot_.last_check_unix_seconds = last_check_unix_seconds > 0
        ? last_check_unix_seconds : 0;
    snapshot_.cached_check_completed = cached_check_completed &&
        snapshot_.last_check_unix_seconds > 0;
    snapshot_.last_attempt_unix_seconds = last_attempt_unix_seconds > 0
        ? last_attempt_unix_seconds : 0;
    snapshot_.automatic_failure_count =
        snapshot_.last_attempt_unix_seconds > 0
            ? automatic_failure_count : 0;
    snapshot_.ignored_version = std::move(ignored_version);
    if (snapshot_.cached_check_completed &&
        !cached_available_version.empty()) {
        snapshot_.cached_available_version =
            std::move(cached_available_version);
        snapshot_.status = L"A new version was found during the last check.";
        snapshot_.dismissed =
            *snapshot_.cached_available_version == snapshot_.ignored_version;
    } else if (snapshot_.cached_check_completed) {
        snapshot_.cached_available_version.reset();
        snapshot_.status = L"No newer version was available at the last check.";
    }
}

CheckStart UpdateController::begin_check(
    CheckTrigger trigger, std::int64_t now_unix_seconds) noexcept {
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
    snapshot_.last_attempt_unix_seconds = now_unix_seconds > 0
        ? now_unix_seconds : snapshot_.last_attempt_unix_seconds;
    if (trigger == CheckTrigger::automatic && now_unix_seconds > 0 &&
        snapshot_.automatic_failure_count <
            std::numeric_limits<std::uint32_t>::max()) {
        ++snapshot_.automatic_failure_count;
    }
    snapshot_.phase = UpdatePhase::checking;
    snapshot_.status = L"Checking official GitHub Releases...";
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

bool UpdateController::begin_install_with_user_consent() noexcept {
    if (snapshot_.phase != UpdatePhase::available ||
        !snapshot_.available_release ||
        snapshot_.dismissed ||
        !snapshot_.available_release->asset.has_value() ||
        !snapshot_.available_release->install_block_reason.empty()) return false;
    snapshot_.phase = UpdatePhase::installing;
    snapshot_.status = L"Downloading and verifying the approved update...";
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

void UpdateController::ignore_available_version() noexcept {
    if (!snapshot_.cached_available_version) return;
    snapshot_.ignored_version = *snapshot_.cached_available_version;
    snapshot_.dismissed = true;
}

const UpdateSnapshot& UpdateController::snapshot() const noexcept {
    return snapshot_;
}

}  // namespace kf2::update
