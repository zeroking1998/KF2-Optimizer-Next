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
    CHECK(kf2::update::save_update_state(path, {1'765'000'000}).has_value());
    const auto loaded = kf2::update::load_update_state(path);
    CHECK(loaded.has_value());
    CHECK(loaded.value().last_check_unix_seconds == 1'765'000'000);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "damaged";
    CHECK(!kf2::update::load_update_state(path).has_value());
    CHECK(!kf2::update::save_update_state(path, {-1}).has_value());
    return EXIT_SUCCESS;
}
