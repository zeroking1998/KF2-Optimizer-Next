#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "kf2/update/update_state.hpp"

#define CHECK(expression) do { if (!(expression)) return EXIT_FAILURE; } while (false)

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    const auto path = root / L"update-state.ini";
    const auto missing = kf2::update::load_update_state(path);
    CHECK(missing.has_value());
    CHECK(missing.value().last_check_unix_seconds == 0);
    CHECK(missing.value().last_result ==
          kf2::update::PersistedCheckResult::unknown);
    CHECK(kf2::update::save_update_state(
        path, {1'765'000'000,
               kf2::update::PersistedCheckResult::available,
               "0.0.4-alpha.1+build.7",
               "0.0.3-alpha+ignored.1"}).has_value());
    const auto loaded = kf2::update::load_update_state(path);
    CHECK(loaded.has_value());
    CHECK(loaded.value().last_check_unix_seconds == 1'765'000'000);
    CHECK(loaded.value().last_result ==
          kf2::update::PersistedCheckResult::available);
    CHECK(loaded.value().available_version == "0.0.4-alpha.1+build.7");
    CHECK(loaded.value().ignored_version == "0.0.3-alpha+ignored.1");
    CHECK(loaded.value().last_attempt_unix_seconds == 0);
    CHECK(loaded.value().automatic_failure_count == 0);
    CHECK(kf2::update::save_update_state(
        path, {1'765'000'000,
               kf2::update::PersistedCheckResult::current,
               {}, {}, 1'765'000'300, 2}).has_value());
    const auto retry = kf2::update::load_update_state(path);
    CHECK(retry.has_value());
    CHECK(retry.value().last_check_unix_seconds == 1'765'000'000);
    CHECK(retry.value().last_attempt_unix_seconds == 1'765'000'300);
    CHECK(retry.value().automatic_failure_count == 2);
    CHECK(retry.value().last_result ==
          kf2::update::PersistedCheckResult::current);
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << "schema_version=2\n"
           "last_check_unix_seconds=1765000000\n"
           "last_result=current\n"
           "available_version=\n"
           "ignored_version=\n";
    const auto version_two = kf2::update::load_update_state(path);
    CHECK(version_two.has_value());
    CHECK(version_two.value().last_check_unix_seconds == 1'765'000'000);
    CHECK(version_two.value().last_attempt_unix_seconds == 0);
    CHECK(version_two.value().automatic_failure_count == 0);
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << "schema_version=1\nlast_check_unix_seconds=1765000000\n";
    const auto legacy = kf2::update::load_update_state(path);
    CHECK(legacy.has_value());
    CHECK(legacy.value().last_check_unix_seconds == 0);
    CHECK(legacy.value().last_result ==
          kf2::update::PersistedCheckResult::unknown);

    constexpr std::array<std::string_view, 9> malformed_versions{
        "1..2",
        "---",
        "1.2.3-",
        "1.2.3+",
        "1.2.3-alpha..1",
        "1.2.3+build..1",
        "1.2.3-01",
        "4294967296.0.0",
        "v1.2.3",
    };
    for (const std::string_view malformed_version : malformed_versions) {
        std::ofstream(path, std::ios::binary | std::ios::trunc)
            << "schema_version=3\n"
               "last_check_unix_seconds=1765000000\n"
               "last_attempt_unix_seconds=1765000000\n"
               "automatic_failure_count=0\n"
               "last_result=available\n"
               "available_version="
            << malformed_version <<
               "\nignored_version=\n";
        CHECK(!kf2::update::load_update_state(path).has_value());
        std::ofstream(path, std::ios::binary | std::ios::trunc)
            << "schema_version=3\n"
               "last_check_unix_seconds=1765000000\n"
               "last_attempt_unix_seconds=1765000000\n"
               "automatic_failure_count=0\n"
               "last_result=current\n"
               "available_version=\n"
               "ignored_version="
            << malformed_version << '\n';
        CHECK(!kf2::update::load_update_state(path).has_value());
        CHECK(!kf2::update::save_update_state(
            path, {1'765'000'000,
                   kf2::update::PersistedCheckResult::available,
                   std::string{malformed_version}, {}}).has_value());
        CHECK(!kf2::update::save_update_state(
            path, {1'765'000'000,
                   kf2::update::PersistedCheckResult::current,
                   {}, std::string{malformed_version}}).has_value());
    }
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "damaged";
    CHECK(!kf2::update::load_update_state(path).has_value());
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << std::string(385, 'x');
    const auto oversized = kf2::update::load_update_state(path);
    CHECK(!oversized.has_value());
    CHECK(oversized.error().code == kf2::ErrorCode::access_denied);
    CHECK(!kf2::update::save_update_state(
        path, {-1, kf2::update::PersistedCheckResult::unknown, {}, {}}).has_value());
    CHECK(!kf2::update::save_update_state(
        path, {1, kf2::update::PersistedCheckResult::available, "bad/version", {}})
              .has_value());
    CHECK(!kf2::update::save_update_state(
        path, {1, kf2::update::PersistedCheckResult::unknown,
               {}, {}, 0, 1}).has_value());
    return EXIT_SUCCESS;
}
