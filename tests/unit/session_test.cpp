#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>

#include "kf2/app/session.hpp"

namespace {
thread_local bool fail_marker_growth{}, marker_growth_failed{};
}
void* operator new(std::size_t size) {
    if (fail_marker_growth && size >= 64) {
        fail_marker_growth = false;
        marker_growth_failed = true;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

static bool deny_status(const std::filesystem::path&,
                        std::error_code& error) {
    error = std::make_error_code(std::errc::permission_denied);
    return false;
}

static bool arm_missing_marker(const std::filesystem::path& path,
                               std::error_code& error) {
    const bool exists = std::filesystem::exists(path, error);
    if (!error && !exists) fail_marker_growth = true;
    return exists;
}

static std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, {}};
}

static int test_serialization_failure(const std::filesystem::path& root) {
    using kf2::app::SessionGuard;
    constexpr kf2::app::SessionIdentity identity{4294967295U, 18446744073709551615ULL};
    const auto clean_marker = root / L"growth-clean.marker";
    auto session = SessionGuard::start(clean_marker, identity);
    CHECK(session.has_value());
    const auto before = read_bytes(clean_marker);
    CHECK(before == "version=1\npid=4294967295\nprocess_start_id=18446744073709551615\n"
                    "clean_shutdown=false\n");
    fail_marker_growth = true;
    const auto failed_clean = session.value().mark_clean();
    fail_marker_growth = false;
    const bool clean_failure_injected = marker_growth_failed;
    const auto after = read_bytes(clean_marker);
    // Restore only the owned fixture before reporting an unfixed Main failure.
    CHECK(SessionGuard::start(clean_marker, identity).has_value());
    CHECK(read_bytes(clean_marker) == before);
    const auto missing_marker = root / L"growth-start.marker";
    marker_growth_failed = false;
    kf2::app::set_session_status_hook_for_testing(&arm_missing_marker);
    const auto failed_start = SessionGuard::start(missing_marker, identity);
    kf2::app::set_session_status_hook_for_testing(nullptr);
    fail_marker_growth = false;
    const bool created = std::filesystem::exists(missing_marker);
    if (created) CHECK(std::filesystem::remove(missing_marker));
    std::cout << "Marker growth failure: clean injected=" << clean_failure_injected
              << "; acknowledged=" << failed_clean.has_value()
              << "; before=" << before.size() << "; after=" << after.size()
              << "; start injected=" << marker_growth_failed
              << "; acknowledged=" << failed_start.has_value()
              << "; created=" << created << '\n';
    CHECK(clean_failure_injected && marker_growth_failed);
    CHECK(!failed_clean.has_value() && !failed_start.has_value());
    CHECK(failed_clean.error().code == kf2::ErrorCode::io_failure);
    CHECK(failed_start.error().code == kf2::ErrorCode::io_failure);
    CHECK(after == before && !created);
    const auto retry = SessionGuard::start(missing_marker, identity);
    CHECK(retry.has_value() && !retry.value().previous_session_unclean());
    CHECK(read_bytes(missing_marker) == before);
    CHECK(session.value().mark_clean().has_value());
    CHECK(read_bytes(clean_marker).ends_with("clean_shutdown=true\n"));
    return EXIT_SUCCESS;
}

int main() {
    namespace fs = std::filesystem;
    using kf2::app::SessionGuard;
    using kf2::app::SessionIdentity;

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    CHECK(test_serialization_failure(root) == EXIT_SUCCESS);

    const auto marker = root / L"session.marker";

    kf2::app::set_session_status_hook_for_testing(&deny_status);
    const auto inaccessible = SessionGuard::start(
        marker, SessionIdentity{9, 19});
    kf2::app::set_session_status_hook_for_testing(nullptr);
    CHECK(!inaccessible.has_value());
    CHECK(inaccessible.error().code == kf2::ErrorCode::io_failure);
    CHECK(inaccessible.error().native_code != 0);
    CHECK(!fs::exists(marker));

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

    {
        std::ofstream oversized(marker, std::ios::binary | std::ios::trunc);
        oversized << std::string(257, 'x');
    }
    const auto oversized = SessionGuard::start(
        marker, SessionIdentity{14, 30});
    CHECK(!oversized.has_value());
    CHECK(oversized.error().code == kf2::ErrorCode::access_denied);
    CHECK(fs::file_size(marker) == 257);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
