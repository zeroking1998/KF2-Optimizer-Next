#include "kf2/platform/windows/atomic_file.hpp"

#include "atomic_file_retry.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <vector>
#include <string>

namespace kf2::platform::windows {
namespace {

volatile LONG temporary_sequence = 0;

std::filesystem::path native_path(const std::filesystem::path& path) {
    auto value = path.wstring();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.starts_with(L"\\\\?\\")) {
        return std::filesystem::path{value};
    }
    if (value.starts_with(L"\\\\")) {
        return std::filesystem::path{L"\\\\?\\UNC\\" + value.substr(2)};
    }
    return std::filesystem::path{L"\\\\?\\" + value};
}

Result<bool> fail(const wchar_t* message, DWORD native_code,
                  const std::filesystem::path& temporary) {
    if (!temporary.empty()) {
        const auto native_temporary = native_path(temporary);
        static_cast<void>(DeleteFileW(native_temporary.c_str()));
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure, message, static_cast<std::uint32_t>(native_code)});
}

Result<bool> unsafe_target(const wchar_t* message, DWORD native_code = 0) {
    return Result<bool>::failure(
        {ErrorCode::access_denied, message,
         static_cast<std::uint32_t>(native_code)});
}

bool safe_directory(const std::filesystem::path& path) {
    const auto native = native_path(path);
    HANDLE directory = CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
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
    const auto native = native_path(path);
    HANDLE file = CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
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
        const auto native_temporary = native_path(temporary);
        HANDLE file = CreateFileW(
            native_temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
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

Result<std::filesystem::path> write_unique_temporary(
    const std::filesystem::path& target, std::string_view bytes) {
    auto created = create_unique_temporary(target);
    if (!created.has_value()) {
        return Result<std::filesystem::path>::failure(created.error());
    }
    HANDLE file = created.value().first;
    auto temporary = std::move(created.value().second);
    const auto discard = [&](const wchar_t* message, DWORD native) {
        CloseHandle(file);
        static_cast<void>(DeleteFileW(native_path(temporary).c_str()));
        return Result<std::filesystem::path>::failure(
            {ErrorCode::io_failure, message, native});
    };
    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + total, requested, &written,
                       nullptr) || written == 0) {
            return discard(L"Temporary file write failed", GetLastError());
        }
        total += written;
    }
    if (!FlushFileBuffers(file)) {
        return discard(L"Temporary file flush failed", GetLastError());
    }
    if (!CloseHandle(file)) {
        const DWORD native = GetLastError();
        file = INVALID_HANDLE_VALUE;
        static_cast<void>(DeleteFileW(native_path(temporary).c_str()));
        return Result<std::filesystem::path>::failure(
            {ErrorCode::io_failure, L"Temporary file close failed",
             native});
    }
    file = INVALID_HANDLE_VALUE;
    return Result<std::filesystem::path>::success(std::move(temporary));
}

Result<std::filesystem::path> unique_unused_sibling(
    const std::filesystem::path& target, std::wstring_view label) {
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        const LONG sequence = InterlockedIncrement(&temporary_sequence);
        std::filesystem::path candidate{
            target.wstring() + std::wstring{label} +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetCurrentThreadId()) + L"." +
            std::to_wstring(static_cast<unsigned long>(sequence))};
        const DWORD attributes =
            GetFileAttributesW(native_path(candidate).c_str());
        const DWORD native = GetLastError();
        if (attributes == INVALID_FILE_ATTRIBUTES &&
            (native == ERROR_FILE_NOT_FOUND ||
             native == ERROR_PATH_NOT_FOUND)) {
            return Result<std::filesystem::path>::success(
                std::move(candidate));
        }
    }
    return Result<std::filesystem::path>::failure(
        {ErrorCode::io_failure, L"Unique rollback file name is unavailable",
         ERROR_FILE_EXISTS});
}

Result<bool> replace_existing_with_retry(
    const std::filesystem::path& target,
    const std::filesystem::path& replacement,
    const std::filesystem::path* backup,
    const wchar_t* failure_message) {
    DWORD native = ERROR_SUCCESS;
    for (unsigned attempt = 0;
         attempt != detail::atomic_replace_attempt_count; ++attempt) {
        const auto native_backup = backup
            ? native_path(*backup).wstring() : std::wstring{};
        if (ReplaceFileW(
                native_path(target).c_str(), native_path(replacement).c_str(),
                backup ? native_backup.c_str() : nullptr,
                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            return Result<bool>::success(true);
        }
        native = GetLastError();
        const bool retryable = native == ERROR_SHARING_VIOLATION ||
                               native == ERROR_ACCESS_DENIED ||
                               native == ERROR_UNABLE_TO_REMOVE_REPLACED;
        const auto backoff = detail::atomic_replace_backoff_after(
            attempt, retryable);
        if (!backoff.has_value()) break;
        Sleep(backoff.value());
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure, failure_message, native});
}

Result<bool> regular_file_matches(const std::filesystem::path& path,
                                  std::string_view expected) {
    HANDLE file = CreateFileW(
        native_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Atomic rollback file cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const std::uint64_t size = expected.size();
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 ||
        information.nFileSizeHigh != static_cast<DWORD>(size >> 32U) ||
        information.nFileSizeLow != static_cast<DWORD>(size)) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        if ((information.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
            information.nNumberOfLinks == 1) {
            return Result<bool>::success(false);
        }
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Atomic rollback file identity is unsafe", native});
    }
    std::size_t total = 0;
    while (total < expected.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            expected.size() - total, std::numeric_limits<DWORD>::max()));
        char buffer[16U * 1024U];
        const DWORD chunk = std::min<DWORD>(requested, sizeof(buffer));
        DWORD read = 0;
        if (!ReadFile(file, buffer, chunk, &read, nullptr) || read == 0) {
            const DWORD native = GetLastError();
            CloseHandle(file);
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Atomic rollback file cannot be read", native});
        }
        if (std::memcmp(buffer, expected.data() + total, read) != 0) {
            CloseHandle(file);
            return Result<bool>::success(false);
        }
        total += read;
    }
    CloseHandle(file);
    return Result<bool>::success(true);
}

Result<bool> remove_with_retry(const std::filesystem::path& path) {
    DWORD native = ERROR_SUCCESS;
    for (unsigned attempt = 0;
         attempt != detail::atomic_replace_attempt_count; ++attempt) {
        if (DeleteFileW(native_path(path).c_str())) {
            return Result<bool>::success(true);
        }
        native = GetLastError();
        if (native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND) {
            return Result<bool>::success(false);
        }
        const bool retryable = native == ERROR_SHARING_VIOLATION ||
                               native == ERROR_ACCESS_DENIED;
        const auto backoff = detail::atomic_replace_backoff_after(
            attempt, retryable);
        if (!backoff.has_value()) break;
        Sleep(backoff.value());
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure, L"Atomic rollback file cannot be removed",
         native});
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

    const auto native_target = native_path(target);
    const DWORD attributes = GetFileAttributesW(native_target.c_str());
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

    auto prepared = write_unique_temporary(target, bytes);
    if (!prepared.has_value()) return Result<bool>::failure(prepared.error());
    const auto temporary = std::move(prepared.value());

    BOOL replaced = FALSE;
    DWORD replace_error = ERROR_SUCCESS;
    // Indexers and real-time scanners can briefly open a just-created test or
    // settings file without delete sharing. Retry only these transient Windows
    // errors; all other failures remain fail-closed.
    for (unsigned attempt = 0;
         attempt != detail::atomic_replace_attempt_count; ++attempt) {
        if (target_exists) {
            const auto native_temporary = native_path(temporary);
            replaced = ReplaceFileW(native_target.c_str(),
                                    native_temporary.c_str(), nullptr,
                                    REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
        } else {
            const auto native_temporary = native_path(temporary);
            replaced = MoveFileExW(native_temporary.c_str(), native_target.c_str(),
                                   MOVEFILE_WRITE_THROUGH);
        }
        if (replaced != FALSE) break;
        replace_error = GetLastError();
        const bool retryable = replace_error == ERROR_SHARING_VIOLATION ||
                               replace_error == ERROR_ACCESS_DENIED ||
                               replace_error == ERROR_UNABLE_TO_REMOVE_REPLACED;
        const auto backoff = detail::atomic_replace_backoff_after(
            attempt, retryable);
        if (!backoff.has_value()) break;
        Sleep(backoff.value());
    }
    if (replaced == FALSE) {
        return fail(L"Atomic file replacement failed", replace_error, temporary);
    }
    return Result<bool>::success(true);
}

Result<bool> atomic_replace_utf8_if_unchanged(
    const std::filesystem::path& target, std::string_view expected_bytes,
    std::string_view replacement_bytes) {
    if (target.empty() || target.filename().empty() || !target.is_absolute() ||
        !target.has_root_name() ||
        target.filename().wstring().find(L':') != std::wstring::npos) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Conditional atomic file target is invalid", 0});
    }
    for (const auto& component : target.relative_path()) {
        if (component == L"." || component == L"..") {
            return unsafe_target(
                L"Conditional atomic target contains an unsafe path component");
        }
    }
    if (!safe_directory(target.parent_path()) || !safe_existing_file(target)) {
        return unsafe_target(
            L"Conditional atomic target identity is unsafe", GetLastError());
    }
    auto prepared = write_unique_temporary(target, replacement_bytes);
    if (!prepared.has_value()) return Result<bool>::failure(prepared.error());
    auto rollback = unique_unused_sibling(target, L".rollback.");
    if (!rollback.has_value()) {
        static_cast<void>(DeleteFileW(native_path(prepared.value()).c_str()));
        return Result<bool>::failure(rollback.error());
    }
    // ReplaceFile captures the exact pre-replacement target in rollback as
    // part of the same filesystem operation. Comparing that captured file
    // avoids a separate check-to-use window before the commit.
    auto replaced = replace_existing_with_retry(
        target, prepared.value(), &rollback.value(),
        L"Conditional atomic replacement failed");
    if (!replaced.has_value()) {
        static_cast<void>(DeleteFileW(native_path(prepared.value()).c_str()));
        return replaced;
    }
    auto matches = regular_file_matches(rollback.value(), expected_bytes);
    if (matches.has_value() && matches.value()) {
        auto removed = remove_with_retry(rollback.value());
        if (removed.has_value()) return Result<bool>::success(true);
        auto restored = replace_existing_with_retry(
            target, rollback.value(), nullptr,
            L"Atomic cleanup rollback failed");
        return restored.has_value()
            ? Result<bool>::failure(removed.error())
            : Result<bool>::failure(restored.error());
    }
    auto restored = replace_existing_with_retry(
        target, rollback.value(), nullptr,
        L"Concurrent configuration rollback failed");
    if (!restored.has_value()) return restored;
    if (!matches.has_value()) return matches;
    return Result<bool>::failure(
        {ErrorCode::stale_data,
         L"Configuration changed before atomic replacement", 0});
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
            DeleteFileW(native_path(candidates.front()).c_str()) == FALSE) {
            return Result<std::filesystem::path>::failure(
                {ErrorCode::io_failure,
                 L"Old quarantined file cannot be removed", GetLastError()});
        }
        candidates.erase(candidates.begin());
    }

    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        std::filesystem::path destination{source.wstring() + std::wstring{suffix}};
        if (attempt != 0) destination += L"." + std::to_wstring(attempt + 1);
        if (MoveFileExW(native_path(source).c_str(),
                        native_path(destination).c_str(),
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
