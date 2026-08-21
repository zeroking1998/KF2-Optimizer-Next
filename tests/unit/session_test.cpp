#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "kf2/app/session.hpp"

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
    using kf2::app::SessionGuard;
    using kf2::app::SessionIdentity;

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    const auto marker = root / L"session.marker";

    auto first = SessionGuard::start(marker, SessionIdentity{10, 20});
    CHECK(first.has_value());
    CHECK(!first.value().previous_session_unclean());
    CHECK(first.value().mark_clean().has_value());

    auto second = SessionGuard::start(marker, SessionIdentity{11, 21});
    CHECK(second.has_value());
    CHECK(!second.value().previous_session_unclean());

    {
        std::ofstream unclean(marker, std::ios::binary | std::ios::trunc);
        unclean << "version=1\npid=99\nprocess_start_id=100\nclean_shutdown=false\n";
    }
    auto recovery = SessionGuard::start(marker, SessionIdentity{99, 101});
    CHECK(recovery.has_value());
    CHECK(recovery.value().previous_session_unclean());

    {
        std::ofstream corrupt(marker, std::ios::binary | std::ios::trunc);
        corrupt << "not a marker";
    }
    auto quarantined = SessionGuard::start(marker, SessionIdentity{12, 22});
    if (!quarantined.has_value()) {
        std::wcerr << L"quarantine start failed: "
                   << quarantined.error().message << L" ("
                   << quarantined.error().native_code << L")\n";
    }
    CHECK(quarantined.has_value());
    CHECK(quarantined.value().previous_session_unclean());
    CHECK(fs::exists(fs::path{marker.wstring() + L".corrupt"}));

    for (int attempt = 0; attempt < 6; ++attempt) {
        {
            std::ofstream corrupt(marker, std::ios::binary | std::ios::trunc);
            corrupt << "repeated corrupt marker " << attempt;
        }
        auto repeated = SessionGuard::start(
            marker, SessionIdentity{13, static_cast<std::uint64_t>(23 + attempt)});
        CHECK(repeated.has_value());
        CHECK(repeated.value().previous_session_unclean());
    }
    std::size_t quarantine_count = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().wstring().starts_with(L"session.marker.corrupt"))
            ++quarantine_count;
    }
    CHECK(quarantine_count == 4);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
