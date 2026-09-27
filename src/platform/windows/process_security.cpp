#include "kf2/platform/windows/process_security.hpp"

#include <Windows.h>

namespace kf2::platform::windows {
namespace {

thread_local std::uint32_t last_hardening_error = 0;

bool set_default_dll_directories(std::uint32_t flags) {
    return SetDefaultDllDirectories(flags) != FALSE;
}

bool set_dll_directory(const wchar_t* directory) {
    return SetDllDirectoryW(directory) != FALSE;
}

bool set_search_path_mode(std::uint32_t flags) {
    return SetSearchPathMode(flags) != FALSE;
}

std::uint32_t last_error() { return GetLastError(); }

void report_failure(const wchar_t* detail, std::uint32_t native_code) noexcept {
    wchar_t message[512]{};
    if (native_code != 0) {
        _snwprintf_s(message, _TRUNCATE, L"%ls\n\nWindows error: %lu",
                     detail, static_cast<unsigned long>(native_code));
    } else {
        wcsncpy_s(message, detail, _TRUNCATE);
    }
    MessageBoxW(nullptr, message, L"KF2 Optimizer Next", MB_OK | MB_ICONERROR);
}

struct ProcessSecurityApi {
    bool (*set_default_dll_directories)(std::uint32_t flags);
    bool (*set_dll_directory)(const wchar_t* directory);
    bool (*set_search_path_mode)(std::uint32_t flags);
    std::uint32_t (*last_error)();
    void (*before_error_construction)();
    void (*report_failure)(const wchar_t* message,
                           std::uint32_t native_code) noexcept;
};

const ProcessSecurityApi default_api{
    set_default_dll_directories, set_dll_directory, set_search_path_mode,
    last_error, nullptr, report_failure};

#if defined(KF2_PROCESS_SECURITY_TESTING)
const ProcessSecurityTestHooks* test_hooks = nullptr;
#endif

ProcessSecurityApi active_api() noexcept {
#if defined(KF2_PROCESS_SECURITY_TESTING)
    if (test_hooks != nullptr) {
        return {test_hooks->set_default_dll_directories,
                test_hooks->set_dll_directory,
                test_hooks->set_search_path_mode,
                test_hooks->last_error,
                test_hooks->before_error_construction,
                test_hooks->report_failure};
    }
#endif
    return default_api;
}

Result<bool> failure(const ProcessSecurityApi& api, const wchar_t* message) {
    last_hardening_error = api.last_error();
    if (api.before_error_construction != nullptr) {
        api.before_error_construction();
    }
    return Result<bool>::failure(
        {ErrorCode::platform_failure, message, last_hardening_error});
}

}  // namespace

Result<bool> harden_process_dll_search() {
    last_hardening_error = 0;
    const auto api = active_api();
    if (!api.set_default_dll_directories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                         LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        return failure(
            api, L"Secure default DLL directories could not be enabled");
    }
    if (!api.set_dll_directory(L"")) {
        return failure(
            api, L"The working directory could not be removed from DLL lookup");
    }
    if (!api.set_search_path_mode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                                  BASE_SEARCH_PATH_PERMANENT)) {
        return failure(api, L"Safe SearchPath order could not be made permanent");
    }
    return Result<bool>::success(true);
}

bool harden_process_dll_search_for_startup() noexcept {
    const auto api = active_api();
    try {
        const auto hardened = harden_process_dll_search();
        if (hardened.has_value()) return true;
        api.report_failure(hardened.error().message.c_str(),
                           hardened.error().native_code);
    } catch (...) {
        api.report_failure(
            L"DLL-search hardening failed before startup could safely create "
            L"a detailed error message",
            last_hardening_error);
    }
    return false;
}

#if defined(KF2_PROCESS_SECURITY_TESTING)
void set_process_security_test_hooks(
    const ProcessSecurityTestHooks* hooks) noexcept {
    test_hooks = hooks;
}
#endif

}  // namespace kf2::platform::windows
