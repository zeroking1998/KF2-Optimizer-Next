#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "kf2/diagnostics/crash_recorder.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path record_path;
    {
        auto armed = kf2::diagnostics::CrashRecorder::arm(
            root, "0.1.0+test (debug)");
        CHECK(armed.has_value());
        record_path = armed.value().pending_path();
        CHECK(fs::exists(record_path));
        CHECK(fs::file_size(record_path) == 0);
        CHECK(armed.value().write_for_testing(0xC0000005U, 0x1234U).has_value());
        CHECK(fs::file_size(record_path) > 0);
        std::ifstream input(record_path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        CHECK(text.find("KF2_OPTIMIZER_CRASH_V1") != std::string::npos);
        CHECK(text.find("\"exception_code\":3221225477") != std::string::npos);
        CHECK(text.find("\"exception_address\":\"0x1234\"") !=
              std::string::npos);
        CHECK(text.find("command line") != std::string::npos);
        CHECK(text.find(root.string()) == std::string::npos);
        CHECK(kf2::diagnostics::retained_crash_record_count(root) == 1);
    }
    CHECK(fs::exists(record_path));
    fs::remove_all(root);
    return EXIT_SUCCESS;
}
