#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "kf2/platform/windows/async_file_writer.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

int main() {
    namespace fs = std::filesystem;
    using kf2::platform::windows::AsyncFileWriter;

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    const auto target = root / L"coalesced.json";

    {
        AsyncFileWriter writer;
        std::uint64_t last_ticket = 0;
        for (int index = 0; index < 100; ++index) {
            last_ticket = writer.submit(
                target, "{\"value\":" + std::to_string(index) + "}");
            CHECK(last_ticket != 0);
        }
        CHECK(writer.wait(last_ticket, std::chrono::seconds{2}));
        CHECK(writer.wait_until_idle(std::chrono::seconds{2}));
        CHECK(read_bytes(target) == "{\"value\":99}");

        const auto first_target = root / L"first.json";
        const auto second_target = root / L"second.json";
        const auto first = writer.submit(first_target, "old");
        const auto second = writer.submit(second_target, "second");
        const auto coalesced = writer.submit(first_target, "new");
        CHECK(first != 0 && second != 0 && coalesced != 0);
        CHECK(writer.wait(second, std::chrono::seconds{2}));
        CHECK(writer.wait(coalesced, std::chrono::seconds{2}));
        CHECK(read_bytes(first_target) == "new");
        CHECK(read_bytes(second_target) == "second");

        const auto failed = writer.submit(
            root / L"missing" / L"failure.json", "failure");
        CHECK(failed != 0);
        CHECK(!writer.wait(failed, std::chrono::seconds{2}));
    }

    const auto destructor_target = root / L"destructor.json";
    {
        AsyncFileWriter writer;
        CHECK(writer.submit(destructor_target, "complete") != 0);
    }
    CHECK(read_bytes(destructor_target) == "complete");
    fs::remove_all(root);
    return EXIT_SUCCESS;
}
