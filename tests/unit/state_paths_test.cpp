#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "kf2/app/state_paths.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    namespace fs = std::filesystem;
    using kf2::app::StateLocationKind;
    using kf2::app::choose_state_location;

    const auto portable = choose_state_location(
        fs::path{L"C:/Portable"}, fs::path{L"C:/User/AppData"},
        [](const fs::path&) { return true; });
    CHECK(portable.has_value());
    CHECK(portable.value().root == fs::path{L"C:/Portable/Data"});
    CHECK(portable.value().kind == StateLocationKind::portable);
    CHECK(portable.value().reason.empty());

    const auto fallback = choose_state_location(
        fs::path{L"C:/ReadOnly"}, fs::path{L"C:/User/AppData"},
        [](const fs::path& candidate) {
            return candidate == fs::path{L"C:/User/AppData/KF2OptimizerNext/Data"};
        });
    CHECK(fallback.has_value());
    CHECK(fallback.value().root ==
          fs::path{L"C:/User/AppData/KF2OptimizerNext/Data"});
    CHECK(fallback.value().kind == StateLocationKind::per_user_fallback);
    CHECK(!fallback.value().reason.empty());

    const auto unavailable = choose_state_location(
        fs::path{L"C:/ReadOnly"}, fs::path{},
        [](const fs::path&) { return false; });
    CHECK(!unavailable.has_value());
    CHECK(unavailable.error().code == kf2::ErrorCode::access_denied);

    const auto invalid = choose_state_location(
        fs::path{}, fs::path{L"C:/User/AppData"},
        [](const fs::path&) { return true; });
    CHECK(!invalid.has_value());
    CHECK(invalid.error().code == kf2::ErrorCode::invalid_argument);
    return EXIT_SUCCESS;
}
