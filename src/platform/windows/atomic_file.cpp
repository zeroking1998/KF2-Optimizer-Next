#include "kf2/platform/windows/atomic_file.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <string>

namespace kf2::platform::windows {
namespace {

volatile LONG temporary_sequence = 0;

Result<bool> fail(const wchar_t* message, DWORD native_code,
                  const std::filesystem::path& temporary) {
    if (!temporary.empty()) static_cast<void>(DeleteFileW(temporary.c_str()));
    return Result<bool>::failure(
        {ErrorCode::io_failure, message, static_cast<std::uint32_t>(native_code)});
}

Result<bool> unsafe_target(const wchar_t* message, DWORD native_code = 0) {
    return Result<bool>::failure(
        {ErrorCode::access_denied, message,
         static_cast<std::uint32_t>(native_code)});
}

bool safe_directory(const std::filesystem::path& path) {
    HANDLE directory = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(directory, &information) &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    CloseHandle(directory);
    return safe;
}

bool safe_existing_file(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(file, &information) &&
                      (information.dwFileAttributes &
                       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
                      information.nNumberOfLinks == 1;
    CloseHandle(file);
    return safe;
}

Result<std::pair<HANDLE, std::filesystem::path>> create_unique_temporary(
    const std::filesystem::path& target) {
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        const LONG sequence = InterlockedIncrement(&temporary_sequence);
        const std::filesystem::path temporary{
            target.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
            L"." + std::to_wstring(GetCurrentThreadId()) + L"." +
            std::to_wstring(static_cast<unsigned long>(sequence))};
        HANDLE file = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            return Result<std::pair<HANDLE, std::filesystem::path>>::success(
                {file, temporary});
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return Result<std::pair<HANDLE, std::filesystem::path>>::failure(
                {ErrorCode::io_failure, L"Unique temporary file creation failed",
                 GetLastError()});
        }
    }
    return Result<std::pair<HANDLE, std::filesystem::path>>::failure(
        {ErrorCode::io_failure, L"Unique temporary file name is unavailable",
         ERROR_FILE_EXISTS});
}

}  // namespace

Result<bool> atomic_replace_utf8(const std::filesystem::path& target,
                                 std::string_view bytes) {
    if (target.empty() || target.filename().empty()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Atomic file target is invalid", 0});
    }
    if (!target.is_absolute() || target.has_root_name() == false ||
        target.filename().wstring().find(L':') != std::wstring::npos) {
        return unsafe_target(L"Atomic file target must be an absolute normal file path");
    }
    for (const auto& component : target.relative_path()) {
        if (component == L"." || component == L"..") {
            return unsafe_target(L"Atomic file target contains an unsafe path component");
        }
    }
    if (!safe_directory(target.parent_path())) {
        return unsafe_target(L"Atomic file parent directory identity is unsafe",
                             GetLastError());
    }

    const DWORD attributes = GetFileAttributesW(target.c_str());
    const bool target_exists = attributes != INVALID_FILE_ATTRIBUTES;
    if (target_exists) {
        if (!safe_existing_file(target)) {
            return unsafe_target(L"Atomic replacement target identity is unsafe",
                                 GetLastError());
        }
    } else {
        const DWORD native = GetLastError();
        if (native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) {
            return unsafe_target(L"Atomic replacement target cannot be inspected",
                                 native);
        }
    }

    auto created = create_unique_temporary(target);
    if (!created.has_value()) {
        return Result<bool>::failure(created.error());
    }
    HANDLE file = created.value().first;
    const std::filesystem::path temporary = std::move(created.value().second);

    std::size_t written_total = 0;
    while (written_total < bytes.size()) {
        const std::size_t remaining = bytes.size() - written_total;
        const DWORD requested = static_cast<DWORD>(
            std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (WriteFile(file, bytes.data() + written_total, requested, &written,
                      nullptr) == FALSE || written == 0) {
            const DWORD error = GetLastError();
            CloseHandle(file);
            return fail(L"Temporary file write failed", error, temporary);
        }
        written_total += written;
    }

    if (FlushFileBuffers(file) == FALSE) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        return fail(L"Temporary file flush failed", error, temporary);
    }
    if (CloseHandle(file) == FALSE) {
        return fail(L"Temporary file close failed", GetLastError(), temporary);
    }

    BOOL replaced = FALSE;
    DWORD replace_error = ERROR_SUCCESS;
    // Indexers and real-time scanners can briefly open a just-created test or
    // settings file without delete sharing. Retry only these transient Windows
    // errors; all other failures remain fail-closed.
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        if (target_exists) {
            replaced = ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
                                    REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
        } else {
            replaced = MoveFileExW(temporary.c_str(), target.c_str(),
                                   MOVEFILE_WRITE_THROUGH);
        }
        if (replaced != FALSE) break;
        replace_error = GetLastError();
        if (replace_error != ERROR_SHARING_VIOLATION &&
            replace_error != ERROR_ACCESS_DENIED &&
            replace_error != ERROR_UNABLE_TO_REMOVE_REPLACED) break;
        Sleep(10U << attempt);
    }
    if (replaced == FALSE) {
        return fail(L"Atomic file replacement failed", replace_error, temporary);
    }
    return Result<bool>::success(true);
}

Result<std::filesystem::path> quarantine_regular_file(
    const std::filesystem::path& source, std::wstring_view suffix,
    std::size_t maximum_retained) {
    if (source.empty() || !source.is_absolute() || suffix.empty() ||
        suffix.find_first_of(L"\\/:") != std::wstring_view::npos ||
        maximum_retained == 0 || maximum_retained > 32) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::invalid_argument, L"Quarantine request is invalid", 0});
    }
    if (!safe_directory(source.parent_path()) || !safe_existing_file(source)) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::access_denied,
             L"Quarantine source or parent identity is unsafe", GetLastError()});
    }

    std::vector<std::filesystem::path> candidates;
    const auto base = source.filename().wstring() + std::wstring{suffix};
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(
             source.parent_path(), error)) {
        if (error) break;
        const auto name = entry.path().filename().wstring();
        if ((name == base || name.starts_with(base + L".")) &&
            entry.is_regular_file(error) && !error) {
            candidates.push_back(entry.path());
        }
        error.clear();
    }
    if (error) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::io_failure, L"Quarantine directory cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& left, const auto& right) {
                  std::error_code a, b;
                  return std::filesystem::last_write_time(left, a) <
                         std::filesystem::last_write_time(right, b);
              });
    while (candidates.size() >= maximum_retained) {
        if (!safe_existing_file(candidates.front()) ||
            DeleteFileW(candidates.front().c_str()) == FALSE) {
            return Result<std::filesystem::path>::failure(
                {ErrorCode::io_failure,
                 L"Old quarantined file cannot be removed", GetLastError()});
        }
        candidates.erase(candidates.begin());
    }

    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        std::filesystem::path destination{source.wstring() + std::wstring{suffix}};
        if (attempt != 0) destination += L"." + std::to_wstring(attempt + 1);
        if (MoveFileExW(source.c_str(), destination.c_str(),
                        MOVEFILE_WRITE_THROUGH) != FALSE) {
            return Result<std::filesystem::path>::success(std::move(destination));
        }
        const DWORD native = GetLastError();
        if (native != ERROR_FILE_EXISTS && native != ERROR_ALREADY_EXISTS) {
            return Result<std::filesystem::path>::failure(
                {ErrorCode::io_failure, L"File quarantine failed", native});
        }
    }
    return Result<std::filesystem::path>::failure(
        {ErrorCode::io_failure, L"No bounded quarantine name is available",
         ERROR_FILE_EXISTS});
}

}  // namespace kf2::platform::windows
