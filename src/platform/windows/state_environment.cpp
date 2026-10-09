#include "kf2/platform/windows/state_environment.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace kf2::platform::windows {
namespace {

using TempPathFunction = DWORD(WINAPI*)(DWORD, LPWSTR);

#if defined(KF2_STATE_ENVIRONMENT_TESTING)
const TemporaryDirectoryTestHooks* temporary_directory_test_hooks = nullptr;
#endif

DWORD WINAPI legacy_temp_path(DWORD capacity, LPWSTR buffer) {
    struct {
        TOKEN_USER user;
        BYTE sid[SECURITY_MAX_SID_SIZE];
    } identity{};
    DWORD required = 0;
    auto query_token = GetTokenInformation;
#if defined(KF2_STATE_ENVIRONMENT_TESTING)
    if (temporary_directory_test_hooks != nullptr) {
        query_token = temporary_directory_test_hooks->query_token;
    }
#endif
    if (!query_token(GetCurrentProcessToken(), TokenUser, &identity,
                     sizeof(identity), &required)) return 0;
    // GetTempPathW lacks GetTempPath2W's SYSTEM-only directory isolation.
    if (IsWellKnownSid(identity.user.User.Sid, WinLocalSystemSid)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return GetTempPathW(capacity, buffer);
}

}  // namespace

Result<std::filesystem::path> executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::platform_failure, L"Executable path discovery failed",
             static_cast<std::uint32_t>(GetLastError())});
    }
    return Result<std::filesystem::path>::success(
        std::filesystem::path{std::wstring{buffer.data(), length}});
}

Result<std::filesystem::path> executable_directory() {
    const auto executable = executable_path();
    if (!executable.has_value()) {
        return Result<std::filesystem::path>::failure(executable.error());
    }
    return Result<std::filesystem::path>::success(
        executable.value().parent_path());
}

Result<std::filesystem::path> temporary_directory() {
    auto get_path = reinterpret_cast<TempPathFunction>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W"));
#if defined(KF2_STATE_ENVIRONMENT_TESTING)
    if (temporary_directory_test_hooks != nullptr) {
        get_path = temporary_directory_test_hooks->temp_path2;
    }
#endif
    if (get_path == nullptr) get_path = legacy_temp_path;
    std::vector<wchar_t> buffer(32768);
    const DWORD length = get_path(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0 || length >= buffer.size()) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::platform_failure, L"Temporary directory discovery failed",
             static_cast<std::uint32_t>(GetLastError())});
    }
    return Result<std::filesystem::path>::success(
        std::filesystem::path{std::wstring{buffer.data(), length}});
}

#if defined(KF2_STATE_ENVIRONMENT_TESTING)
void set_temporary_directory_test_hooks(
    const TemporaryDirectoryTestHooks* hooks) noexcept {
    temporary_directory_test_hooks = hooks;
}
#endif

Result<std::filesystem::path> local_app_data_directory() {
    PWSTR raw_path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                                KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(result) || raw_path == nullptr) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::platform_failure, L"LocalAppData discovery failed",
             static_cast<std::uint32_t>(result)});
    }
    std::filesystem::path path{raw_path};
    CoTaskMemFree(raw_path);
    return Result<std::filesystem::path>::success(std::move(path));
}

bool probe_writable_directory(const std::filesystem::path& directory) noexcept {
    try {
        const auto native_directory = extended_length_path(directory);
        std::error_code directory_error;
        std::filesystem::create_directories(native_directory, directory_error);
        if (directory_error ||
            !std::filesystem::is_directory(native_directory, directory_error) ||
            directory_error) {
            return false;
        }

        const std::wstring filename =
            L".kf2-write-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L".tmp";
        const auto probe = extended_length_path(directory / filename);
        HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        const BOOL closed = CloseHandle(file);
        const BOOL removed = DeleteFileW(probe.c_str());
        return closed != FALSE && removed != FALSE;
    } catch (...) {
        return false;
    }
}

}  // namespace kf2::platform::windows
