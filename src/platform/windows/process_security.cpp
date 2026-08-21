#include "kf2/platform/windows/process_security.hpp"

#include <Windows.h>

namespace kf2::platform::windows {

Result<bool> harden_process_dll_search() noexcept {
    if (SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                LOAD_LIBRARY_SEARCH_USER_DIRS) == FALSE) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Secure default DLL directories could not be enabled",
             GetLastError()});
    }
    if (SetDllDirectoryW(L"") == FALSE) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"The working directory could not be removed from DLL lookup",
             GetLastError()});
    }
    if (SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                          BASE_SEARCH_PATH_PERMANENT) == FALSE) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Safe SearchPath order could not be made permanent",
             GetLastError()});
    }
    return Result<bool>::success(true);
}

}  // namespace kf2::platform::windows
