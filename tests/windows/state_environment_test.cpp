#include <Windows.h>

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

    const auto executable_file = executable_path();
    CHECK(executable_file.has_value());
    CHECK(executable_file.value().is_absolute());
    const auto executable = executable_directory();
    CHECK(executable.has_value());
    CHECK(executable.value() == executable_file.value().parent_path());

    const auto temporary = temporary_directory();
    CHECK(temporary.has_value());
    CHECK(temporary.value().is_absolute());

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

    auto long_directory = root;
    while (long_directory.wstring().size() < MAX_PATH + 32) {
        long_directory /= L"long-path-segment";
    }
    CHECK(probe_writable_directory(long_directory));
    std::error_code long_error;
    CHECK(fs::is_directory(extended_length_path(long_directory), long_error));
    CHECK(!long_error);
    fs::remove_all(extended_length_path(root));
    return EXIT_SUCCESS;
}
