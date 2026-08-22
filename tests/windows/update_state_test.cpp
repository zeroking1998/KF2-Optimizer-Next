#include <cstdlib>
#include <filesystem>
#include <fstream>

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
               "0.0.4-alpha", "0.0.3-alpha"}).has_value());
    const auto loaded = kf2::update::load_update_state(path);
    CHECK(loaded.has_value());
    CHECK(loaded.value().last_check_unix_seconds == 1'765'000'000);
    CHECK(loaded.value().last_result ==
          kf2::update::PersistedCheckResult::available);
    CHECK(loaded.value().available_version == "0.0.4-alpha");
    CHECK(loaded.value().ignored_version == "0.0.3-alpha");
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << "schema_version=1\nlast_check_unix_seconds=1765000000\n";
    const auto legacy = kf2::update::load_update_state(path);
    CHECK(legacy.has_value());
    CHECK(legacy.value().last_check_unix_seconds == 0);
    CHECK(legacy.value().last_result ==
          kf2::update::PersistedCheckResult::unknown);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "damaged";
    CHECK(!kf2::update::load_update_state(path).has_value());
    CHECK(!kf2::update::save_update_state(
        path, {-1, kf2::update::PersistedCheckResult::unknown, {}, {}}).has_value());
    CHECK(!kf2::update::save_update_state(
        path, {1, kf2::update::PersistedCheckResult::available, "bad/version", {}})
              .has_value());
    return EXIT_SUCCESS;
}
