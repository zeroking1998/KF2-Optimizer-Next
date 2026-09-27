#include <cstdlib>

#include "kf2/update/update_controller.hpp"

#define CHECK(expression) do { if (!(expression)) return EXIT_FAILURE; } while (false)

namespace {
kf2::update::ReleaseInfo release(bool installable = true) {
    kf2::update::ReleaseInfo value{
        .repository = "https://github.com/example/project",
        .tag = "v0.0.3-alpha",
        .version = "0.0.3-alpha",
        .published_at = "2026-08-22T12:00:00Z",
        .changelog = "Added: updater"};
    if (installable) {
        value.asset = kf2::update::ReleaseAsset{
            "KF2OptimizerNext-v0.0.3-alpha-win64.zip",
            "https://github.com/example/project/releases/download/v0.0.3-alpha/KF2OptimizerNext-v0.0.3-alpha-win64.zip",
            1024, std::string(64, 'a')};
    }
    return value;
}
}

int main() {
    using namespace kf2::update;
    constexpr std::int64_t now = 2'000'000;
    UpdateController controller{"0.0.2-alpha"};
    controller.restore_preferences(true, now - 60);
    CHECK(controller.begin_check(CheckTrigger::automatic, now) ==
          CheckStart::throttled);
    CHECK(controller.begin_check(CheckTrigger::manual, now) ==
          CheckStart::started);
    CHECK(controller.begin_check(CheckTrigger::manual, now) == CheckStart::busy);
    controller.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{release()}));
    CHECK(controller.snapshot().phase == UpdatePhase::available);
    CHECK(controller.begin_install_with_user_consent());
    CHECK(controller.snapshot().phase == UpdatePhase::installing);

    UpdateController cached_available{"0.0.2-alpha"};
    cached_available.restore_preferences(
        true, now - 60, true, "0.0.3-alpha");
    CHECK(cached_available.snapshot().cached_check_completed);
    CHECK(cached_available.snapshot().cached_available_version ==
          std::optional<std::string>{"0.0.3-alpha"});
    CHECK(cached_available.begin_check(CheckTrigger::automatic, now) ==
          CheckStart::throttled);
    cached_available.dismiss();
    CHECK(cached_available.snapshot().dismissed);

    UpdateController ignored{"0.0.2-alpha"};
    ignored.restore_preferences(
        true, now - 60, true, "0.0.3-alpha", "0.0.3-alpha");
    CHECK(ignored.snapshot().dismissed);
    ignored.ignore_available_version();
    CHECK(ignored.snapshot().ignored_version == "0.0.3-alpha");

    UpdateController newer_after_ignored{"0.0.2-alpha"};
    newer_after_ignored.restore_preferences(
        true, now - 60, true, "0.0.4-alpha", "0.0.3-alpha");
    CHECK(!newer_after_ignored.snapshot().dismissed);

    UpdateController cached_current{"0.0.2-alpha"};
    cached_current.restore_preferences(true, now - 60, true, {});
    CHECK(cached_current.snapshot().cached_check_completed);
    CHECK(!cached_current.snapshot().cached_available_version);
    CHECK(cached_current.snapshot().status.find(L"No newer version") !=
          std::wstring::npos);

    UpdateController dismissed{"0.0.2-alpha"};
    CHECK(dismissed.begin_check(CheckTrigger::manual, now) ==
          CheckStart::started);
    dismissed.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{release()}));
    dismissed.dismiss();
    CHECK(dismissed.snapshot().dismissed);
    CHECK(!dismissed.begin_install_with_user_consent());

    UpdateController disabled{"0.0.2-alpha"};
    disabled.restore_preferences(false, 0);
    CHECK(disabled.begin_check(CheckTrigger::automatic, now) ==
          CheckStart::automatic_disabled);
    CHECK(disabled.begin_check(CheckTrigger::manual, now) == CheckStart::started);
    disabled.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::nullopt));
    CHECK(disabled.snapshot().phase == UpdatePhase::current);
    CHECK(!disabled.begin_install_with_user_consent());

    UpdateController no_consent{"0.0.2-alpha"};
    no_consent.restore_preferences(true, 0);
    CHECK(no_consent.begin_check(CheckTrigger::automatic, now) ==
          CheckStart::started);
    CHECK(no_consent.snapshot().last_check_unix_seconds == 0);
    CHECK(no_consent.snapshot().last_attempt_unix_seconds == now);
    CHECK(no_consent.snapshot().automatic_failure_count == 1);
    no_consent.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{release(false)}));
    CHECK(no_consent.snapshot().last_check_unix_seconds == now);
    CHECK(no_consent.snapshot().automatic_failure_count == 0);
    CHECK(!no_consent.begin_install_with_user_consent());

    UpdateController blocked{"0.0.2-alpha"};
    CHECK(blocked.begin_check(CheckTrigger::manual, now) == CheckStart::started);
    auto blocked_release = release();
    blocked_release.install_block_reason = L"Checksum is unavailable";
    blocked.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{std::move(blocked_release)}));
    CHECK(!blocked.begin_install_with_user_consent());
    CHECK(blocked.snapshot().status == L"Checksum is unavailable");

    UpdateController failed{"0.0.2-alpha"};
    CHECK(failed.begin_check(CheckTrigger::manual, now) == CheckStart::started);
    failed.complete_check(kf2::Result<std::optional<ReleaseInfo>>::failure(
        {kf2::ErrorCode::io_failure, L"GitHub is unavailable", 0}));
    CHECK(failed.snapshot().phase == UpdatePhase::error);
    CHECK(failed.snapshot().status == L"GitHub is unavailable");

    constexpr std::int64_t old_success =
        now - kAutomaticCheckIntervalSeconds - 1;
    UpdateController transient_failure{"0.0.2-alpha"};
    transient_failure.restore_preferences(
        true, old_success, true, "0.0.3-alpha");
    CHECK(transient_failure.begin_check(CheckTrigger::automatic, now) ==
          CheckStart::started);
    transient_failure.complete_check(
        kf2::Result<std::optional<ReleaseInfo>>::failure(
            {kf2::ErrorCode::io_failure, L"Temporary failure", 0}));
    CHECK(transient_failure.snapshot().last_check_unix_seconds == old_success);
    CHECK(transient_failure.snapshot().last_attempt_unix_seconds == now);
    CHECK(transient_failure.snapshot().automatic_failure_count == 1);
    CHECK(transient_failure.snapshot().cached_check_completed);
    CHECK(transient_failure.snapshot().cached_available_version ==
          std::optional<std::string>{"0.0.3-alpha"});

    UpdateController restarted_after_failure{"0.0.2-alpha"};
    restarted_after_failure.restore_preferences(
        true,
        transient_failure.snapshot().last_check_unix_seconds,
        transient_failure.snapshot().cached_check_completed,
        *transient_failure.snapshot().cached_available_version,
        {},
        transient_failure.snapshot().last_attempt_unix_seconds,
        transient_failure.snapshot().automatic_failure_count);
    CHECK(restarted_after_failure.begin_check(
              CheckTrigger::automatic,
              now + kAutomaticFailureRetryInitialSeconds - 1) ==
          CheckStart::throttled);
    const auto second_attempt =
        now + kAutomaticFailureRetryInitialSeconds;
    CHECK(restarted_after_failure.begin_check(
              CheckTrigger::automatic, second_attempt) ==
          CheckStart::started);
    restarted_after_failure.complete_check(
        kf2::Result<std::optional<ReleaseInfo>>::failure(
            {kf2::ErrorCode::io_failure, L"Still unavailable", 0}));
    CHECK(restarted_after_failure.snapshot().automatic_failure_count == 2);
    CHECK(restarted_after_failure.begin_check(
              CheckTrigger::automatic,
              second_attempt + 2 * kAutomaticFailureRetryInitialSeconds - 1) ==
          CheckStart::throttled);
    const auto recovery_attempt =
        second_attempt + 2 * kAutomaticFailureRetryInitialSeconds;
    CHECK(restarted_after_failure.begin_check(
              CheckTrigger::automatic, recovery_attempt) ==
          CheckStart::started);
    restarted_after_failure.complete_check(
        kf2::Result<std::optional<ReleaseInfo>>::success(std::nullopt));
    CHECK(restarted_after_failure.snapshot().last_check_unix_seconds ==
          recovery_attempt);
    CHECK(restarted_after_failure.snapshot().automatic_failure_count == 0);
    CHECK(restarted_after_failure.begin_check(
              CheckTrigger::automatic, recovery_attempt + 1) ==
          CheckStart::throttled);

    UpdateController manual_bypasses_backoff{"0.0.2-alpha"};
    manual_bypasses_backoff.restore_preferences(
        true, old_success, true, {}, {}, now, 3);
    CHECK(manual_bypasses_backoff.begin_check(
              CheckTrigger::automatic, now + 1) == CheckStart::throttled);
    CHECK(manual_bypasses_backoff.begin_check(
              CheckTrigger::manual, now + 1) == CheckStart::started);
    manual_bypasses_backoff.complete_check(
        kf2::Result<std::optional<ReleaseInfo>>::success(std::nullopt));
    CHECK(manual_bypasses_backoff.snapshot().automatic_failure_count == 0);
    CHECK(manual_bypasses_backoff.snapshot().last_check_unix_seconds ==
          now + 1);
    return EXIT_SUCCESS;
}
