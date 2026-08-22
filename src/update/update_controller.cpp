#include "kf2/update/update_controller.hpp"

#include <utility>

namespace kf2::update {

UpdateController::UpdateController(std::string installed_version) {
    snapshot_.installed_version = std::move(installed_version);
}

void UpdateController::restore_preferences(
    bool automatic_checks_enabled,
    std::int64_t last_check_unix_seconds) noexcept {
    snapshot_.automatic_checks_enabled = automatic_checks_enabled;
    snapshot_.last_check_unix_seconds = last_check_unix_seconds > 0
        ? last_check_unix_seconds : 0;
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
    }
    snapshot_.last_check_unix_seconds = now_unix_seconds > 0
        ? now_unix_seconds : snapshot_.last_check_unix_seconds;
    snapshot_.phase = UpdatePhase::checking;
    snapshot_.status = L"Checking official GitHub Releases...";
    snapshot_.dismissed = false;
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
    snapshot_.available_release = std::move(result.value());
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
    if (snapshot_.phase == UpdatePhase::available) snapshot_.dismissed = true;
}

const UpdateSnapshot& UpdateController::snapshot() const noexcept {
    return snapshot_;
}

}  // namespace kf2::update
