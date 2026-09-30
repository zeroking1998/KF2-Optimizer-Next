#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

#include "kf2/update/update_controller.hpp"

#define CHECK(expression) do { if (!(expression)) return EXIT_FAILURE; } while (false)

namespace {
void throw_allocation_failure() {
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        nullptr);
    throw std::bad_alloc{};
}

template <typename Action>
bool throws_on_next_allocation(Action&& action) {
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        throw_allocation_failure);
    try {
        std::forward<Action>(action)();
    } catch (const std::bad_alloc&) {
        kf2::update::detail::set_update_controller_allocation_hook_for_testing(
            nullptr);
        return true;
    } catch (...) {
        kf2::update::detail::set_update_controller_allocation_hook_for_testing(
            nullptr);
        return false;
    }
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        nullptr);
    return false;
}

bool same_asset(const std::optional<kf2::update::ReleaseAsset>& left,
                const std::optional<kf2::update::ReleaseAsset>& right) {
    if (left.has_value() != right.has_value()) return false;
    return !left ||
        (left->file_name == right->file_name &&
         left->download_url == right->download_url &&
         left->size_bytes == right->size_bytes &&
         left->sha256 == right->sha256);
}

bool same_release(const std::optional<kf2::update::ReleaseInfo>& left,
                  const std::optional<kf2::update::ReleaseInfo>& right) {
    if (left.has_value() != right.has_value()) return false;
    return !left ||
        (left->repository == right->repository && left->tag == right->tag &&
         left->version == right->version &&
         left->published_at == right->published_at &&
         left->changelog == right->changelog &&
         same_asset(left->asset, right->asset) &&
         left->install_block_reason == right->install_block_reason);
}

bool same_snapshot(const kf2::update::UpdateSnapshot& left,
                   const kf2::update::UpdateSnapshot& right) {
    return left.installed_version == right.installed_version &&
        left.automatic_checks_enabled == right.automatic_checks_enabled &&
        left.last_check_unix_seconds == right.last_check_unix_seconds &&
        left.last_attempt_unix_seconds == right.last_attempt_unix_seconds &&
        left.automatic_failure_count == right.automatic_failure_count &&
        left.phase == right.phase &&
        same_release(left.available_release, right.available_release) &&
        left.cached_check_completed == right.cached_check_completed &&
        left.cached_available_version == right.cached_available_version &&
        left.ignored_version == right.ignored_version &&
        left.status == right.status && left.dismissed == right.dismissed;
}

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

    // Future persisted wall-clock times are not elapsed-time cooldowns.
    for (const auto future : {
             now + 3600, std::numeric_limits<std::int64_t>::max()}) {
        UpdateController backward_clock{"0.0.2-alpha"};
        backward_clock.restore_preferences(
            true, future, true, "0.0.3-alpha", "0.0.3-alpha");
        CHECK(backward_clock.snapshot().dismissed);
        CHECK(backward_clock.snapshot().status.find(L"new version") !=
              std::wstring::npos);
        const auto started = backward_clock.begin_check(
            CheckTrigger::automatic, now);
        if (started != CheckStart::started) {
            std::cerr << "Future last-check timestamp " << future
                      << " blocked automatic checking at " << now << '\n';
        }
        CHECK(started == CheckStart::started);
        CHECK(backward_clock.snapshot().last_attempt_unix_seconds == now);
        CHECK(backward_clock.snapshot().cached_check_completed);
        CHECK(backward_clock.snapshot().cached_available_version ==
              std::optional<std::string>{"0.0.3-alpha"});
        CHECK(backward_clock.snapshot().ignored_version == "0.0.3-alpha");
        CHECK(backward_clock.snapshot().dismissed);
        backward_clock.complete_check(
            kf2::Result<std::optional<ReleaseInfo>>::success(std::nullopt));
        CHECK(backward_clock.snapshot().last_check_unix_seconds == now);
        CHECK(backward_clock.begin_check(CheckTrigger::automatic,
              now + kAutomaticCheckIntervalSeconds - 1) ==
              CheckStart::throttled);
        CHECK(backward_clock.begin_check(CheckTrigger::automatic,
              now + kAutomaticCheckIntervalSeconds) == CheckStart::started);

        UpdateController future_attempt{"0.0.2-alpha"};
        future_attempt.restore_preferences(
            true, 0, false, {}, {}, future, 1);
        CHECK(future_attempt.begin_check(CheckTrigger::automatic, now) ==
              CheckStart::started);
        CHECK(future_attempt.snapshot().last_attempt_unix_seconds == now);
        CHECK(future_attempt.snapshot().automatic_failure_count == 2);
        future_attempt.complete_check(
            kf2::Result<std::optional<ReleaseInfo>>::failure(
                {kf2::ErrorCode::io_failure, L"Temporary failure", 0}));
        CHECK(future_attempt.begin_check(CheckTrigger::automatic,
              now + 2 * kAutomaticFailureRetryInitialSeconds - 1) ==
              CheckStart::throttled);
        CHECK(future_attempt.begin_check(CheckTrigger::automatic,
              now + 2 * kAutomaticFailureRetryInitialSeconds) ==
              CheckStart::started);

        UpdateController future_disabled{"0.0.2-alpha"};
        future_disabled.restore_preferences(false, future);
        CHECK(future_disabled.begin_check(CheckTrigger::automatic, now) ==
              CheckStart::automatic_disabled);
        CHECK(future_disabled.begin_check(CheckTrigger::manual, now) ==
              CheckStart::started);
    }

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

    UpdateController restore_allocation_failure{
        "0.0.2-alpha-with-a-long-installed-version"};
    const auto restore_before = restore_allocation_failure.snapshot();
    std::string cached_version(64, 'c');
    std::string ignored_version(64, 'i');
    CHECK(throws_on_next_allocation([&] {
        restore_allocation_failure.restore_preferences(
            false, now, true, std::move(cached_version),
            std::move(ignored_version), now, 3);
    }));
    CHECK(same_snapshot(restore_allocation_failure.snapshot(),
                        restore_before));

    UpdateController check_allocation_failure{"0.0.2-alpha"};
    const auto check_before = check_allocation_failure.snapshot();
    CHECK(throws_on_next_allocation([&] {
        static_cast<void>(check_allocation_failure.begin_check(
            CheckTrigger::manual, now));
    }));
    CHECK(same_snapshot(check_allocation_failure.snapshot(), check_before));

    UpdateController install_allocation_failure{"0.0.2-alpha"};
    CHECK(install_allocation_failure.begin_check(
              CheckTrigger::manual, now) == CheckStart::started);
    install_allocation_failure.complete_check(
        kf2::Result<std::optional<ReleaseInfo>>::success(
            std::optional<ReleaseInfo>{release()}));
    const auto install_before = install_allocation_failure.snapshot();
    CHECK(throws_on_next_allocation([&] {
        static_cast<void>(
            install_allocation_failure.begin_install_with_user_consent());
    }));
    CHECK(same_snapshot(install_allocation_failure.snapshot(), install_before));

    UpdateController ignore_allocation_failure{"0.0.2-alpha"};
    ignore_allocation_failure.restore_preferences(
        true, now, true, std::string(64, 'v'));
    const auto ignore_before = ignore_allocation_failure.snapshot();
    CHECK(throws_on_next_allocation(
        [&] { ignore_allocation_failure.ignore_available_version(); }));
    CHECK(same_snapshot(ignore_allocation_failure.snapshot(), ignore_before));
    return EXIT_SUCCESS;
}
