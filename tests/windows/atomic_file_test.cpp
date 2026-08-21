#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "kf2/platform/windows/atomic_file.hpp"

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
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);

    const auto target = root / L"settings.ini";
    {
        std::ofstream old_file(target, std::ios::binary);
        old_file << "old";
    }

    const auto replaced =
        kf2::platform::windows::atomic_replace_utf8(target, "new settings\n");
    CHECK(replaced.has_value());
    CHECK(read_bytes(target) == "new settings\n");
    CHECK(!fs::exists(fs::path{target.wstring() + L".tmp"}));

    const auto missing_parent = root / L"missing" / L"settings.ini";
    const auto failed = kf2::platform::windows::atomic_replace_utf8(
        missing_parent, "must not appear");
    CHECK(!failed.has_value());
    CHECK(!fs::exists(missing_parent));
    CHECK(!fs::exists(fs::path{missing_parent.wstring() + L".tmp"}));

    const auto linked = root / L"linked.ini";
    const auto alias = root / L"linked-alias.ini";
    {
        std::ofstream output(linked, std::ios::binary);
        output << "linked old";
    }
    CHECK(CreateHardLinkW(alias.c_str(), linked.c_str(), nullptr) != FALSE);
    const auto hardlink_blocked =
        kf2::platform::windows::atomic_replace_utf8(linked, "must be blocked");
    CHECK(!hardlink_blocked.has_value());
    CHECK(hardlink_blocked.error().code == kf2::ErrorCode::access_denied);
    CHECK(read_bytes(linked) == "linked old");
    CHECK(read_bytes(alias) == "linked old");

    // A real process may hold an INI without delete sharing. The bounded
    // retry must fail closed, preserve the original bytes and remove its
    // unique temporary file instead of leaving transaction residue.
    const auto locked = root / L"locked.ini";
    {
        std::ofstream output(locked, std::ios::binary);
        output << "locked old";
    }
    HANDLE lock = CreateFileW(locked.c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    const auto lock_blocked =
        kf2::platform::windows::atomic_replace_utf8(locked, "must be blocked");
    CHECK(!lock_blocked.has_value());
    CHECK(lock_blocked.error().code == kf2::ErrorCode::io_failure);
    CHECK(lock_blocked.error().native_code == ERROR_SHARING_VIOLATION ||
          lock_blocked.error().native_code == ERROR_ACCESS_DENIED ||
          lock_blocked.error().native_code == ERROR_UNABLE_TO_REMOVE_REPLACED);
    CHECK(CloseHandle(lock) != FALSE);
    CHECK(read_bytes(locked) == "locked old");
    std::size_t locked_temporaries = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().wstring().starts_with(L"locked.ini.tmp."))
            ++locked_temporaries;
    }
    CHECK(locked_temporaries == 0);

    const auto trap_target = root / L"trap.ini";
    const auto legacy_temporary = fs::path{trap_target.wstring() + L".tmp"};
    {
        std::ofstream output(legacy_temporary, std::ios::binary);
        output << "unrelated legacy file";
    }
    const auto trap_replaced =
        kf2::platform::windows::atomic_replace_utf8(trap_target, "safe");
    CHECK(trap_replaced.has_value());
    CHECK(read_bytes(trap_target) == "safe");
    CHECK(read_bytes(legacy_temporary) == "unrelated legacy file");

    const auto corrupt = root / L"corrupt.ini";
    for (int iteration = 0; iteration < 7; ++iteration) {
        {
            std::ofstream output(corrupt, std::ios::binary);
            output << "broken " << iteration;
        }
        const auto quarantined =
            kf2::platform::windows::quarantine_regular_file(corrupt);
        CHECK(quarantined.has_value());
        CHECK(fs::exists(quarantined.value()));
        CHECK(!fs::exists(corrupt));
    }
    std::size_t quarantined_count = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().wstring().starts_with(L"corrupt.ini.corrupt"))
            ++quarantined_count;
    }
    CHECK(quarantined_count == 4);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
