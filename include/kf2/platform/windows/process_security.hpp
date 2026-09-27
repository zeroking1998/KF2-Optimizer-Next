#pragma once

#include <cstdint>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

// Removes the current working directory from implicit DLL resolution and
// limits default dependency lookup to Windows plus explicitly registered
// directories. The portable product has no implicit sibling-DLL contract.
[[nodiscard]] Result<bool> harden_process_dll_search();

// Applies DLL-search hardening at process entry and reports any failure with a
// bounded, non-allocating fallback. False means startup must stop.
[[nodiscard]] bool harden_process_dll_search_for_startup() noexcept;

#if defined(KF2_PROCESS_SECURITY_TESTING)
struct ProcessSecurityTestHooks {
    bool (*set_default_dll_directories)(std::uint32_t flags);
    bool (*set_dll_directory)(const wchar_t* directory);
    bool (*set_search_path_mode)(std::uint32_t flags);
    std::uint32_t (*last_error)();
    void (*before_error_construction)();
    void (*report_failure)(const wchar_t* message,
                           std::uint32_t native_code) noexcept;
};

void set_process_security_test_hooks(
    const ProcessSecurityTestHooks* hooks) noexcept;
#endif

}  // namespace kf2::platform::windows
