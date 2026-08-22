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
    CHECK(no_consent.snapshot().last_check_unix_seconds == now);
    no_consent.complete_check(kf2::Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{release(false)}));
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
    return EXIT_SUCCESS;
}
