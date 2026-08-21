#include "kf2/platform/windows/state_environment.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace kf2::platform::windows {

Result<std::filesystem::path> executable_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::platform_failure, L"Executable path discovery failed",
             static_cast<std::uint32_t>(GetLastError())});
    }
    return Result<std::filesystem::path>::success(
        std::filesystem::path{std::wstring{buffer.data(), length}}.parent_path());
}

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
        std::error_code directory_error;
        std::filesystem::create_directories(directory, directory_error);
        if (directory_error || !std::filesystem::is_directory(directory)) {
            return false;
        }

        const std::wstring filename =
            L".kf2-write-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L".tmp";
        const auto probe = directory / filename;
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
