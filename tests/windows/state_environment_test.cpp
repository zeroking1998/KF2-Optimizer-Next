#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "kf2/platform/windows/state_environment.hpp"

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
    using namespace kf2::platform::windows;

    const auto executable = executable_directory();
    CHECK(executable.has_value());
    CHECK(executable.value().is_absolute());

    const auto app_data = local_app_data_directory();
    CHECK(app_data.has_value());
    CHECK(app_data.value().is_absolute());

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    CHECK(probe_writable_directory(root / L"Data"));
    CHECK(fs::is_directory(root / L"Data"));
    CHECK(fs::is_empty(root / L"Data"));

    fs::create_directories(root);
    const auto file_parent = root / L"not-a-directory";
    std::ofstream{file_parent} << "file";
    CHECK(!probe_writable_directory(file_parent / L"Data"));
    fs::remove_all(root);
    return EXIT_SUCCESS;
}
