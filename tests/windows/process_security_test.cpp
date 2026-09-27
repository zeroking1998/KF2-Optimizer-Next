#include <Windows.h>

#include <array>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <new>

#include "kf2/platform/windows/process_security.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

enum class FailingCall { none, default_directories, dll_directory, search_path };

FailingCall failing_call = FailingCall::none;
std::uint32_t reported_code = 0;
std::array<wchar_t, 512> reported_message{};
bool throw_before_error = false;

bool set_default_directories(std::uint32_t) {
    return failing_call != FailingCall::default_directories;
}

bool set_dll_directory(const wchar_t*) {
    return failing_call != FailingCall::dll_directory;
}

bool set_search_path_mode(std::uint32_t) {
    return failing_call != FailingCall::search_path;
}

std::uint32_t last_error() { return 1234; }

void before_error_construction() {
    if (throw_before_error) throw std::bad_alloc{};
}

void report_failure(const wchar_t* message,
                    std::uint32_t native_code) noexcept {
    reported_code = native_code;
    wcsncpy_s(reported_message.data(), reported_message.size(), message,
              _TRUNCATE);
}

const kf2::platform::windows::ProcessSecurityTestHooks hooks{
    set_default_directories,
    set_dll_directory,
    set_search_path_mode,
    last_error,
    before_error_construction,
    report_failure,
};

int verify_failure(FailingCall call, const wchar_t* expected_message) {
    failing_call = call;
    const auto hardened =
        kf2::platform::windows::harden_process_dll_search();
    CHECK(!hardened.has_value());
    CHECK(hardened.error().code == kf2::ErrorCode::platform_failure);
    CHECK(hardened.error().native_code == 1234);
    CHECK(hardened.error().message == expected_message);
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    using namespace kf2::platform::windows;
    set_process_security_test_hooks(&hooks);

    CHECK(verify_failure(
              FailingCall::default_directories,
              L"Secure default DLL directories could not be enabled") ==
          EXIT_SUCCESS);
    CHECK(verify_failure(
              FailingCall::dll_directory,
              L"The working directory could not be removed from DLL lookup") ==
          EXIT_SUCCESS);
    CHECK(verify_failure(
              FailingCall::search_path,
              L"Safe SearchPath order could not be made permanent") ==
          EXIT_SUCCESS);

    failing_call = FailingCall::default_directories;
    throw_before_error = true;
    reported_code = 0;
    reported_message.fill(L'\0');
    CHECK(!harden_process_dll_search_for_startup());
    CHECK(reported_code == 1234);
    CHECK(std::wcsstr(reported_message.data(), L"DLL-search hardening failed") !=
          nullptr);

    throw_before_error = false;
    failing_call = FailingCall::none;
    CHECK(harden_process_dll_search_for_startup());

    set_process_security_test_hooks(nullptr);
    return EXIT_SUCCESS;
}
