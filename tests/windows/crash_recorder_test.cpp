#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "kf2/diagnostics/crash_recorder.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

int fallback_calls = 0;
bool fallback_received_pointers = false;
DWORD fallback_exception_code = 0;

LONG WINAPI fallback_filter(EXCEPTION_POINTERS* pointers) noexcept {
    ++fallback_calls;
    fallback_received_pointers = pointers != nullptr &&
        pointers->ExceptionRecord != nullptr;
    fallback_exception_code = fallback_received_pointers
        ? pointers->ExceptionRecord->ExceptionCode : 0;
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);
    const auto setup_root = root / L"setup-failure";
    fs::create_directories(setup_root);
    const auto setup_filter = SetUnhandledExceptionFilter(fallback_filter);
    for (const auto failure : {
             kf2::diagnostics::CrashSetupFailure::allocation,
             kf2::diagnostics::CrashSetupFailure::filesystem}) {
        kf2::diagnostics::fail_crash_setup_for_testing(failure);
        const auto failed = kf2::diagnostics::CrashRecorder::arm(setup_root, "test");
        kf2::diagnostics::fail_crash_setup_for_testing(
            kf2::diagnostics::CrashSetupFailure::none);
        CHECK(!failed.has_value());
        CHECK(failed.error().code == kf2::ErrorCode::internal_failure);
        CHECK(SetUnhandledExceptionFilter(fallback_filter) == fallback_filter);
        CHECK(fs::is_empty(setup_root));
        // A failed setup must neither leak its handle nor mark the recorder armed.
        {
            auto retry = kf2::diagnostics::CrashRecorder::arm(setup_root, "retry");
            CHECK(retry.has_value());
            CHECK(retry.value().write_for_testing(0xC0000005U, 0x1234U).has_value());
        }
        fs::remove_all(setup_root);
        fs::create_directories(setup_root);
    }
    CHECK(SetUnhandledExceptionFilter(setup_filter) == fallback_filter);
    fs::path record_path;
    {
        auto armed = kf2::diagnostics::CrashRecorder::arm(
            root, "0.0.2-alpha+test (debug)");
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

    const auto original_filter = SetUnhandledExceptionFilter(fallback_filter);
    fs::path failed_record_path;
    {
        auto armed = kf2::diagnostics::CrashRecorder::arm(
            root, "0.0.2-alpha+test (debug)");
        CHECK(armed.has_value());
        failed_record_path = armed.value().pending_path();
        kf2::diagnostics::invalidate_crash_file_for_testing();
        CHECK(kf2::diagnostics::invoke_crash_filter_for_testing(
                  0xC0000005U, 0x5678U) == EXCEPTION_CONTINUE_SEARCH);
        CHECK(fallback_calls == 1);
        CHECK(fallback_received_pointers);
        CHECK(fallback_exception_code == 0xC0000005U);
        CHECK(!armed.value().write_for_testing(
                   0xC0000005U, 0x5678U).has_value());
    }
    CHECK(!fs::exists(failed_record_path));
    CHECK(SetUnhandledExceptionFilter(original_filter) == fallback_filter);

    const auto system_filter = SetUnhandledExceptionFilter(nullptr);
    {
        auto armed = kf2::diagnostics::CrashRecorder::arm(
            root, "0.0.2-alpha+test (debug)");
        CHECK(armed.has_value());
        kf2::diagnostics::invalidate_crash_file_for_testing();
        CHECK(kf2::diagnostics::invoke_crash_filter_for_testing(
                  0xC0000005U, 0x9ABCU) == EXCEPTION_CONTINUE_SEARCH);
        CHECK(fallback_calls == 1);
    }
    CHECK(SetUnhandledExceptionFilter(system_filter) == nullptr);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
