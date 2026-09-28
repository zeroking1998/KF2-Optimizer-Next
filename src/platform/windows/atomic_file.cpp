#include "kf2/platform/windows/atomic_file.hpp"

#include "atomic_file_retry.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace kf2::platform::windows {
namespace {

volatile LONG temporary_sequence = 0;

#if defined(KF2_ATOMIC_FILE_TESTING)
BoundedReadHook bounded_read_hook{};
AtomicFileMutationHook atomic_file_mutation_hook{};
#endif

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : value_{value} {}
    ~UniqueHandle() {
        if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_{other.release()} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = other.release();
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    void close() noexcept {
        if (value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
            value_ = INVALID_HANDLE_VALUE;
        }
    }
    [[nodiscard]] HANDLE release() noexcept {
        const HANDLE value = value_;
        value_ = INVALID_HANDLE_VALUE;
        return value;
    }

private:
    HANDLE value_;
};

struct VerifiedHandle final {
    UniqueHandle handle;
    BY_HANDLE_FILE_INFORMATION information{};
};

struct PreparedFile final {
    UniqueHandle handle;
    std::filesystem::path path;
    BY_HANDLE_FILE_INFORMATION information{};
};

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

Result<bool> unsafe_target(const wchar_t* message, DWORD native_code = 0) {
    return Result<bool>::failure(
        {ErrorCode::access_denied, message,
         static_cast<std::uint32_t>(native_code)});
}

bool safe_regular_file(const BY_HANDLE_FILE_INFORMATION& information) {
    return (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
           information.nNumberOfLinks == 1;
}

bool same_file_identity(const BY_HANDLE_FILE_INFORMATION& left,
                        const BY_HANDLE_FILE_INFORMATION& right) {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
           left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

Result<VerifiedHandle> open_verified_directory(
    const std::filesystem::path& path, bool allow_delete_sharing) {
    const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE |
                          (allow_delete_sharing ? FILE_SHARE_DELETE : 0);
    UniqueHandle directory{CreateFileW(
        native_path(path).c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
        sharing, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (directory.get() == INVALID_HANDLE_VALUE) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::io_failure, L"Directory identity cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(directory.get(), &information)) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::io_failure, L"Directory identity cannot be inspected",
             GetLastError()});
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::access_denied, L"Directory identity is unsafe",
             ERROR_ACCESS_DENIED});
    }
    return Result<VerifiedHandle>::success(
        {std::move(directory), information});
}

Result<VerifiedHandle> open_verified_regular_file(
    const std::filesystem::path& path, DWORD desired_access) {
    UniqueHandle file{CreateFileW(
        native_path(path).c_str(), desired_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE) {
        const DWORD native = GetLastError();
        return Result<VerifiedHandle>::failure(
            {native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND
                 ? ErrorCode::not_found
                 : ErrorCode::io_failure,
             L"File identity cannot be opened", native});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file.get(), &information)) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::io_failure, L"File identity cannot be inspected",
             GetLastError()});
    }
    if (!safe_regular_file(information)) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::access_denied, L"File identity is unsafe",
             ERROR_ACCESS_DENIED});
    }
    return Result<VerifiedHandle>::success({std::move(file), information});
}

Result<VerifiedHandle> lock_verified_parent(
    const std::filesystem::path& path,
    const BY_HANDLE_FILE_INFORMATION& expected) {
    auto locked = open_verified_directory(path, false);
    if (!locked.has_value()) return locked;
    if (!same_file_identity(expected, locked.value().information)) {
        return Result<VerifiedHandle>::failure(
            {ErrorCode::stale_data,
             L"Directory identity changed before filesystem mutation", 0});
    }
    return locked;
}

Result<bool> path_matches_identity(
    const std::filesystem::path& path,
    const BY_HANDLE_FILE_INFORMATION& expected) {
    auto current = open_verified_regular_file(path, FILE_READ_ATTRIBUTES);
    if (!current.has_value()) {
        if (current.error().code == ErrorCode::not_found) {
            return Result<bool>::success(false);
        }
        return Result<bool>::failure(current.error());
    }
    return Result<bool>::success(
        same_file_identity(expected, current.value().information));
}

Result<bool> rename_open_file_in_place(
    HANDLE file, HANDLE verified_parent,
    const std::filesystem::path& destination_name, bool replace) {
    if (destination_name.empty() || destination_name.has_parent_path() ||
        destination_name.filename() != destination_name) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Rename target is invalid", 0});
    }
    const DWORD required = GetFinalPathNameByHandleW(
        verified_parent, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Verified rename directory cannot be resolved", GetLastError()});
    }
    std::wstring parent_name(static_cast<std::size_t>(required) + 1, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(
        verified_parent, parent_name.data(),
        static_cast<DWORD>(parent_name.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= parent_name.size()) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Verified rename directory cannot be resolved", GetLastError()});
    }
    parent_name.resize(written);
    constexpr std::wstring_view extended_prefix = L"\\\\?\\";
    if (parent_name.starts_with(extended_prefix)) {
        parent_name.replace(0, extended_prefix.size(), L"\\??\\");
    }
    auto name =
        (std::filesystem::path{parent_name} / destination_name).wstring();
    const std::size_t name_bytes = name.size() * sizeof(wchar_t);
    const std::size_t buffer_size = offsetof(FILE_RENAME_INFO, FileName) +
                                    name_bytes + sizeof(wchar_t);
    std::vector<std::byte> buffer(buffer_size);
    auto* information =
        reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    information->ReplaceIfExists = replace ? TRUE : FALSE;
    information->RootDirectory = nullptr;
    information->FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(information->FileName, name.data(), name_bytes);
    information->FileName[name.size()] = L'\0';
    if (!SetFileInformationByHandle(file, FileRenameInfo, information,
                                    static_cast<DWORD>(buffer.size()))) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Handle-anchored rename failed",
             GetLastError()});
    }
    return Result<bool>::success(true);
}

Result<bool> remove_open_file(HANDLE file) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                    sizeof(disposition))) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Handle-anchored removal failed",
             GetLastError()});
    }
    return Result<bool>::success(true);
}

Result<bool> discard_prepared_file(PreparedFile& prepared) {
    if (prepared.handle.get() != INVALID_HANDLE_VALUE) {
        return remove_open_file(prepared.handle.get());
    }
    auto current = open_verified_regular_file(
        prepared.path, DELETE | FILE_READ_ATTRIBUTES);
    if (!current.has_value()) {
        if (current.error().code == ErrorCode::not_found) {
            return Result<bool>::success(false);
        }
        return Result<bool>::failure(current.error());
    }
    if (!same_file_identity(prepared.information,
                            current.value().information)) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Temporary file identity changed before cleanup", 0});
    }
    return remove_open_file(current.value().handle.get());
}

#if defined(KF2_ATOMIC_FILE_TESTING)
void invoke_atomic_file_mutation_hook(
    AtomicFileMutationStage stage, const std::filesystem::path& path) {
    if (atomic_file_mutation_hook != nullptr) {
        atomic_file_mutation_hook(stage, path);
    }
}
#endif

bool same_file_state(const BY_HANDLE_FILE_INFORMATION& left,
                     const LARGE_INTEGER& left_size,
                     const BY_HANDLE_FILE_INFORMATION& right,
                     const LARGE_INTEGER& right_size) {
    return safe_regular_file(right) && same_file_identity(left, right) &&
           left_size.QuadPart == right_size.QuadPart &&
           CompareFileTime(&left.ftLastWriteTime, &right.ftLastWriteTime) == 0;
}

Result<PreparedFile> create_unique_temporary(
    const std::filesystem::path& target) {
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        const LONG sequence = InterlockedIncrement(&temporary_sequence);
        const std::filesystem::path temporary{
            target.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
            L"." + std::to_wstring(GetCurrentThreadId()) + L"." +
            std::to_wstring(static_cast<unsigned long>(sequence))};
        const auto native_temporary = native_path(temporary);
        UniqueHandle file{CreateFileW(
            native_temporary.c_str(),
            GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
            nullptr)};
        if (file.get() != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(file.get(), &information) ||
                !safe_regular_file(information)) {
                const DWORD native = GetLastError();
                static_cast<void>(remove_open_file(file.get()));
                return Result<PreparedFile>::failure(
                    {ErrorCode::io_failure,
                     L"Unique temporary file identity is unsafe", native});
            }
            return Result<PreparedFile>::success(
                {std::move(file), temporary, information});
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return Result<PreparedFile>::failure(
                {ErrorCode::io_failure, L"Unique temporary file creation failed",
                 GetLastError()});
        }
    }
    return Result<PreparedFile>::failure(
        {ErrorCode::io_failure, L"Unique temporary file name is unavailable",
         ERROR_FILE_EXISTS});
}

Result<PreparedFile> write_unique_temporary(
    const std::filesystem::path& target, std::string_view bytes) {
    auto created = create_unique_temporary(target);
    if (!created.has_value()) {
        return Result<PreparedFile>::failure(created.error());
    }
    auto prepared = std::move(created.value());
    const auto discard = [&](const wchar_t* message, DWORD native) {
        static_cast<void>(remove_open_file(prepared.handle.get()));
        return Result<PreparedFile>::failure(
            {ErrorCode::io_failure, message, native});
    };
    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(prepared.handle.get(), bytes.data() + total, requested,
                       &written, nullptr) ||
            written == 0) {
            return discard(L"Temporary file write failed", GetLastError());
        }
        total += written;
    }
    if (!FlushFileBuffers(prepared.handle.get())) {
        return discard(L"Temporary file flush failed", GetLastError());
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(prepared.handle.get(), &after) ||
        !safe_regular_file(after) ||
        !same_file_identity(prepared.information, after)) {
        return discard(L"Temporary file identity changed while writing",
                       GetLastError());
    }
    prepared.information = after;
    return Result<PreparedFile>::success(std::move(prepared));
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

Result<VerifiedHandle> verify_existing_commit(
    const std::filesystem::path& target,
    const std::filesystem::path& backup,
    HANDLE verified_parent,
    const BY_HANDLE_FILE_INFORMATION& expected_target,
    const BY_HANDLE_FILE_INFORMATION& expected_replacement) {
    auto captured = open_verified_regular_file(
        backup, GENERIC_READ | DELETE | FILE_READ_ATTRIBUTES);
    if (!captured.has_value()) {
        return Result<VerifiedHandle>::failure(captured.error());
    }
    auto current = open_verified_regular_file(target, FILE_READ_ATTRIBUTES);
    if (!current.has_value()) {
        auto restored = rename_open_file_in_place(
            captured.value().handle.get(), verified_parent,
            target.filename(), true);
        return restored.has_value()
            ? Result<VerifiedHandle>::failure(current.error())
            : Result<VerifiedHandle>::failure(restored.error());
    }
    const bool expected_identities =
        same_file_identity(expected_target, captured.value().information) &&
        same_file_identity(expected_replacement, current.value().information);
    if (!expected_identities) {
        if (same_file_identity(expected_replacement,
                               current.value().information)) {
            auto restored = rename_open_file_in_place(
                captured.value().handle.get(), verified_parent,
                target.filename(), true);
            if (!restored.has_value()) {
                return Result<VerifiedHandle>::failure(restored.error());
            }
        }
        return Result<VerifiedHandle>::failure(
            {ErrorCode::stale_data,
             L"Atomic target identity changed before replacement", 0});
    }
    return captured;
}

Result<bool> regular_file_matches(HANDLE file, std::string_view expected) {
    BY_HANDLE_FILE_INFORMATION information{};
    const std::uint64_t size = expected.size();
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 ||
        information.nFileSizeHigh != static_cast<DWORD>(size >> 32U) ||
        information.nFileSizeLow != static_cast<DWORD>(size)) {
        const DWORD native = GetLastError();
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
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Atomic rollback file cannot be read", native});
        }
        if (std::memcmp(buffer, expected.data() + total, read) != 0) {
            return Result<bool>::success(false);
        }
        total += read;
    }
    return Result<bool>::success(true);
}

}  // namespace

#if defined(KF2_ATOMIC_FILE_TESTING)
void set_bounded_read_hook_for_testing(BoundedReadHook hook) noexcept {
    bounded_read_hook = hook;
}

void set_atomic_file_mutation_hook_for_testing(
    AtomicFileMutationHook hook) noexcept {
    atomic_file_mutation_hook = hook;
}
#endif

Result<std::string> read_bounded_verified_file(
    const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    if (path.empty() || path.filename().empty() || !path.is_absolute() ||
        !path.has_root_name() ||
        path.filename().wstring().find(L':') != std::wstring::npos) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Bounded file path is invalid", 0});
    }
    for (const auto& component : path.relative_path()) {
        if (component == L"." || component == L"..") {
            return Result<std::string>::failure(
                {ErrorCode::access_denied,
                 L"Bounded file path contains an unsafe component", 0});
        }
    }

    const auto native = native_path(path);
    UniqueHandle file{CreateFileW(
        native.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE) {
        const DWORD native_error = GetLastError();
        return Result<std::string>::failure(
            {native_error == ERROR_FILE_NOT_FOUND ||
                     native_error == ERROR_PATH_NOT_FOUND
                 ? ErrorCode::not_found
                 : ErrorCode::io_failure,
             L"Bounded file cannot be opened for reading", native_error});
    }

    BY_HANDLE_FILE_INFORMATION before{};
    LARGE_INTEGER before_size{};
    if (!GetFileInformationByHandle(file.get(), &before) ||
        !GetFileSizeEx(file.get(), &before_size)) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"Bounded file cannot be inspected", GetLastError()});
    }
    if (!safe_regular_file(before)) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Bounded file identity is unsafe", ERROR_ACCESS_DENIED});
    }
    if (before_size.QuadPart < 0 ||
        static_cast<std::uintmax_t>(before_size.QuadPart) > maximum_bytes ||
        static_cast<std::uintmax_t>(before_size.QuadPart) >
            std::numeric_limits<std::size_t>::max()) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Bounded file exceeds its size limit", ERROR_FILE_TOO_LARGE});
    }

#if defined(KF2_ATOMIC_FILE_TESTING)
    if (bounded_read_hook != nullptr) bounded_read_hook(path);
#endif

    std::string bytes;
    try {
        bytes.resize(static_cast<std::size_t>(before_size.QuadPart));
    } catch (const std::bad_alloc&) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"Bounded file buffer cannot be allocated",
             ERROR_NOT_ENOUGH_MEMORY});
    }
    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file.get(), bytes.data() + total, requested, &read,
                      nullptr)) {
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"Bounded file cannot be read", GetLastError()});
        }
        if (read == 0) {
            return Result<std::string>::failure(
                {ErrorCode::stale_data,
                 L"Bounded file ended before its inspected size",
                 ERROR_HANDLE_EOF});
        }
        total += read;
    }

    BY_HANDLE_FILE_INFORMATION after{};
    LARGE_INTEGER after_size{};
    if (!GetFileInformationByHandle(file.get(), &after) ||
        !GetFileSizeEx(file.get(), &after_size)) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"Bounded file cannot be re-inspected", GetLastError()});
    }
    if (!same_file_state(before, before_size, after, after_size)) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Bounded file changed while it was being read", 0});
    }

    UniqueHandle current{CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (current.get() == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Bounded file path changed while it was being read",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION current_information{};
    LARGE_INTEGER current_size{};
    if (!GetFileInformationByHandle(current.get(), &current_information) ||
        !GetFileSizeEx(current.get(), &current_size)) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Bounded file path cannot be re-inspected", GetLastError()});
    }
    if (!same_file_state(before, before_size, current_information,
                         current_size)) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Bounded file identity changed while it was being read", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

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
    auto observed_parent =
        open_verified_directory(target.parent_path(), true);
    if (!observed_parent.has_value()) {
        return Result<bool>::failure(observed_parent.error());
    }
    auto observed_target =
        open_verified_regular_file(target, FILE_READ_ATTRIBUTES);
    const bool target_exists = observed_target.has_value();
    if (!target_exists && observed_target.error().code != ErrorCode::not_found) {
        return Result<bool>::failure(observed_target.error());
    }
    observed_parent.value().handle.close();
    if (target_exists) observed_target.value().handle.close();

#if defined(KF2_ATOMIC_FILE_TESTING)
    invoke_atomic_file_mutation_hook(
        AtomicFileMutationStage::atomic_after_validation, target);
#endif

    auto locked_parent = lock_verified_parent(
        target.parent_path(), observed_parent.value().information);
    if (!locked_parent.has_value()) {
        return Result<bool>::failure(locked_parent.error());
    }
    if (target_exists) {
        auto unchanged = path_matches_identity(
            target, observed_target.value().information);
        if (!unchanged.has_value()) return unchanged;
        if (!unchanged.value()) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Atomic target identity changed before replacement", 0});
        }
    } else {
        auto appeared = open_verified_regular_file(target, FILE_READ_ATTRIBUTES);
        if (appeared.has_value()) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Atomic target appeared before replacement", 0});
        }
        if (appeared.error().code != ErrorCode::not_found) {
            return Result<bool>::failure(appeared.error());
        }
    }

    auto prepared = write_unique_temporary(target, bytes);
    if (!prepared.has_value()) return Result<bool>::failure(prepared.error());
    if (!target_exists) {
        auto committed = rename_open_file_in_place(
            prepared.value().handle.get(), locked_parent.value().handle.get(),
            target.filename(), false);
        if (!committed.has_value()) {
            static_cast<void>(remove_open_file(prepared.value().handle.get()));
            return committed;
        }
        return Result<bool>::success(true);
    }

    auto rollback = unique_unused_sibling(target, L".rollback.");
    if (!rollback.has_value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return Result<bool>::failure(rollback.error());
    }
    auto temporary_unchanged = path_matches_identity(
        prepared.value().path, prepared.value().information);
    if (!temporary_unchanged.has_value() || !temporary_unchanged.value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return temporary_unchanged.has_value()
            ? Result<bool>::failure(
                  {ErrorCode::stale_data,
                   L"Atomic temporary identity changed before replacement", 0})
            : Result<bool>::failure(temporary_unchanged.error());
    }
    prepared.value().handle.close();
    auto replaced = replace_existing_with_retry(
        target, prepared.value().path, &rollback.value(),
        L"Atomic file replacement failed");
    if (!replaced.has_value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return replaced;
    }
    auto captured = verify_existing_commit(
        target, rollback.value(), locked_parent.value().handle.get(),
        observed_target.value().information,
        prepared.value().information);
    if (!captured.has_value()) {
        return Result<bool>::failure(captured.error());
    }
    auto removed = remove_open_file(captured.value().handle.get());
    if (removed.has_value()) return Result<bool>::success(true);
    auto restored = rename_open_file_in_place(
        captured.value().handle.get(), locked_parent.value().handle.get(),
        target.filename(), true);
    return restored.has_value()
        ? Result<bool>::failure(removed.error())
        : Result<bool>::failure(restored.error());
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
    auto observed_parent =
        open_verified_directory(target.parent_path(), true);
    if (!observed_parent.has_value()) {
        return Result<bool>::failure(observed_parent.error());
    }
    auto observed_target =
        open_verified_regular_file(target, FILE_READ_ATTRIBUTES);
    if (!observed_target.has_value()) {
        return Result<bool>::failure(observed_target.error());
    }
    observed_parent.value().handle.close();
    observed_target.value().handle.close();

#if defined(KF2_ATOMIC_FILE_TESTING)
    invoke_atomic_file_mutation_hook(
        AtomicFileMutationStage::conditional_after_validation, target);
#endif

    auto locked_parent = lock_verified_parent(
        target.parent_path(), observed_parent.value().information);
    if (!locked_parent.has_value()) {
        return Result<bool>::failure(locked_parent.error());
    }
    auto unchanged = path_matches_identity(
        target, observed_target.value().information);
    if (!unchanged.has_value()) return unchanged;
    if (!unchanged.value()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Conditional atomic target identity changed", 0});
    }
    auto prepared = write_unique_temporary(target, replacement_bytes);
    if (!prepared.has_value()) return Result<bool>::failure(prepared.error());
    auto rollback = unique_unused_sibling(target, L".rollback.");
    if (!rollback.has_value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return Result<bool>::failure(rollback.error());
    }
    auto temporary_unchanged = path_matches_identity(
        prepared.value().path, prepared.value().information);
    if (!temporary_unchanged.has_value() || !temporary_unchanged.value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return temporary_unchanged.has_value()
            ? Result<bool>::failure(
                  {ErrorCode::stale_data,
                   L"Conditional temporary identity changed", 0})
            : Result<bool>::failure(temporary_unchanged.error());
    }
    prepared.value().handle.close();
    // ReplaceFile captures the exact pre-replacement target in rollback as
    // part of the same filesystem operation. Comparing that captured file
    // avoids a separate check-to-use window before the commit.
    auto replaced = replace_existing_with_retry(
        target, prepared.value().path, &rollback.value(),
        L"Conditional atomic replacement failed");
    if (!replaced.has_value()) {
        static_cast<void>(discard_prepared_file(prepared.value()));
        return replaced;
    }
    auto captured = verify_existing_commit(
        target, rollback.value(), locked_parent.value().handle.get(),
        observed_target.value().information,
        prepared.value().information);
    if (!captured.has_value()) {
        return Result<bool>::failure(captured.error());
    }
    auto matches = regular_file_matches(
        captured.value().handle.get(), expected_bytes);
    if (matches.has_value() && matches.value()) {
        auto removed = remove_open_file(captured.value().handle.get());
        if (removed.has_value()) return Result<bool>::success(true);
        auto restored = rename_open_file_in_place(
            captured.value().handle.get(), locked_parent.value().handle.get(),
            target.filename(), true);
        return restored.has_value()
            ? Result<bool>::failure(removed.error())
            : Result<bool>::failure(restored.error());
    }
    auto restored = rename_open_file_in_place(
        captured.value().handle.get(), locked_parent.value().handle.get(),
        target.filename(), true);
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
    auto observed_parent =
        open_verified_directory(source.parent_path(), true);
    if (!observed_parent.has_value()) {
        return Result<std::filesystem::path>::failure(
            observed_parent.error());
    }
    auto observed_source = open_verified_regular_file(
        source, DELETE | FILE_READ_ATTRIBUTES);
    if (!observed_source.has_value()) {
        return Result<std::filesystem::path>::failure(
            observed_source.error());
    }
    observed_parent.value().handle.close();

#if defined(KF2_ATOMIC_FILE_TESTING)
    invoke_atomic_file_mutation_hook(
        AtomicFileMutationStage::quarantine_after_source_validation, source);
#endif

    auto locked_parent = lock_verified_parent(
        source.parent_path(), observed_parent.value().information);
    if (!locked_parent.has_value()) {
        return Result<std::filesystem::path>::failure(
            locked_parent.error());
    }
    auto source_unchanged = path_matches_identity(
        source, observed_source.value().information);
    if (!source_unchanged.has_value()) {
        return Result<std::filesystem::path>::failure(
            source_unchanged.error());
    }
    if (!source_unchanged.value()) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::stale_data,
             L"Quarantine source identity changed before mutation", 0});
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
        auto candidate = open_verified_regular_file(
            candidates.front(), DELETE | FILE_READ_ATTRIBUTES);
        if (!candidate.has_value()) {
            return Result<std::filesystem::path>::failure(
                candidate.error());
        }
#if defined(KF2_ATOMIC_FILE_TESTING)
        invoke_atomic_file_mutation_hook(
            AtomicFileMutationStage::quarantine_after_candidate_validation,
            candidates.front());
#endif
        auto candidate_unchanged = path_matches_identity(
            candidates.front(), candidate.value().information);
        if (!candidate_unchanged.has_value()) {
            return Result<std::filesystem::path>::failure(
                candidate_unchanged.error());
        }
        if (!candidate_unchanged.value()) {
            return Result<std::filesystem::path>::failure(
                {ErrorCode::stale_data,
                 L"Quarantine candidate identity changed before removal", 0});
        }
        auto removed = remove_open_file(candidate.value().handle.get());
        if (!removed.has_value()) {
            return Result<std::filesystem::path>::failure(removed.error());
        }
        candidates.erase(candidates.begin());
    }

    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        std::filesystem::path destination{source.wstring() + std::wstring{suffix}};
        if (attempt != 0) destination += L"." + std::to_wstring(attempt + 1);
        auto moved = rename_open_file_in_place(
            observed_source.value().handle.get(),
            locked_parent.value().handle.get(), destination.filename(), false);
        if (moved.has_value()) {
            return Result<std::filesystem::path>::success(std::move(destination));
        }
        const DWORD native = moved.error().native_code;
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
