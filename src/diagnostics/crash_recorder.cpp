#include "kf2/diagnostics/crash_recorder.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <system_error>
#include <utility>
#include <vector>

namespace kf2::diagnostics {
namespace {

constexpr std::size_t maximum_records = 4;
volatile PVOID crash_file{};
volatile LONG record_written{};
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter{};
std::array<char, 128> build_text{};

HANDLE current_file() noexcept {
    return static_cast<HANDLE>(InterlockedCompareExchangePointer(
        &crash_file, nullptr, nullptr));
}

bool valid_record_name(const std::filesystem::path& path) {
    const auto name = path.filename().wstring();
    return name.size() == 43 && name.starts_with(L"crash-") &&
        name.ends_with(L".json");
}

bool safe_regular_file(const std::filesystem::path& path) noexcept {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

void prune_records(const std::filesystem::path& directory) noexcept {
    try {
        std::error_code error;
        std::vector<std::filesystem::directory_entry> records;
        for (std::filesystem::directory_iterator iterator{directory, error}, end;
             !error && iterator != end; iterator.increment(error)) {
            if (!valid_record_name(iterator->path()) ||
                !safe_regular_file(iterator->path())) continue;
            const auto size = iterator->file_size(error);
            if (error) break;
            if (size == 0) {
                std::filesystem::remove(iterator->path(), error);
                if (error) break;
                continue;
            }
            records.push_back(*iterator);
        }
        if (error) return;
        std::sort(records.begin(), records.end(),
                  [](const auto& left, const auto& right) {
                      std::error_code left_error, right_error;
                      const auto left_time = left.last_write_time(left_error);
                      const auto right_time = right.last_write_time(right_error);
                      if (left_error || right_error) {
                          return left.path().filename() < right.path().filename();
                      }
                      return left_time > right_time;
                  });
        // Keep room for the currently reserved record. If the process crashes,
        // that empty reservation becomes the newest retained record.
        for (std::size_t index = maximum_records - 1;
             index < records.size(); ++index) {
            if (safe_regular_file(records[index].path())) {
                std::filesystem::remove(records[index].path(), error);
                if (error) return;
            }
        }
    } catch (...) {
        // Crash retention must never block normal startup.
    }
}

bool write_record(std::uint32_t code, std::uint32_t flags,
                  std::uintptr_t address) noexcept {
    HANDLE file = current_file();
    if (!file || file == INVALID_HANDLE_VALUE ||
        InterlockedCompareExchange(&record_written, 1, 0) != 0) return false;
    FILETIME utc{};
    GetSystemTimeAsFileTime(&utc);
    const auto filetime =
        (static_cast<unsigned long long>(utc.dwHighDateTime) << 32U) |
        utc.dwLowDateTime;
    std::array<char, 1024> payload{};
    const int length = std::snprintf(
        payload.data(), payload.size(),
        "{\"schema\":\"KF2_OPTIMIZER_CRASH_V1\",\"build_identity\":\"%s\","
        "\"exception_code\":%lu,\"exception_flags\":%lu,"
        "\"exception_address\":\"0x%llX\",\"process_id\":%lu,"
        "\"thread_id\":%lu,\"utc_filetime\":%llu,"
        "\"privacy\":\"no memory dump, stack, command line or user file content\"}",
        build_text.data(), static_cast<unsigned long>(code),
        static_cast<unsigned long>(flags),
        static_cast<unsigned long long>(address),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()), filetime);
    if (length <= 0 || static_cast<std::size_t>(length) >= payload.size()) return false;
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(file)) return false;
    DWORD written = 0;
    const bool success = WriteFile(file, payload.data(),
                                   static_cast<DWORD>(length), &written,
                                   nullptr) != FALSE &&
        written == static_cast<DWORD>(length) && FlushFileBuffers(file) != FALSE;
    return success;
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* pointers) noexcept {
    const EXCEPTION_RECORD* record = pointers ? pointers->ExceptionRecord : nullptr;
    static_cast<void>(write_record(
        record ? record->ExceptionCode : 0,
        record ? record->ExceptionFlags : 0,
        reinterpret_cast<std::uintptr_t>(
            record ? record->ExceptionAddress : nullptr)));
    return EXCEPTION_EXECUTE_HANDLER;
}

Result<bool> verify_directory(const std::filesystem::path& directory) noexcept {
    if (!directory.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Crash record directory must be absolute", 0});
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Crash record directory cannot be created",
             static_cast<std::uint32_t>(error.value())});
    }
    HANDLE handle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Crash record directory cannot be inspected",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(handle, &information) != FALSE &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const DWORD native = safe ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!safe) {
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Crash record directory identity is unsafe",
             native});
    }
    return Result<bool>::success(true);
}

}  // namespace

CrashRecorder::CrashRecorder(std::filesystem::path pending_path) noexcept
    : pending_path_{std::move(pending_path)}, active_{true} {}

CrashRecorder::CrashRecorder(CrashRecorder&& other) noexcept
    : pending_path_{std::move(other.pending_path_)},
      active_{std::exchange(other.active_, false)} {}

CrashRecorder& CrashRecorder::operator=(CrashRecorder&& other) noexcept {
    if (this != &other) {
        disarm();
        pending_path_ = std::move(other.pending_path_);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

CrashRecorder::~CrashRecorder() { disarm(); }

Result<CrashRecorder> CrashRecorder::arm(
    const std::filesystem::path& directory,
    std::string_view build_identity) noexcept {
    if (current_file()) {
        return Result<CrashRecorder>::failure(
            {ErrorCode::already_running, L"Crash recorder is already armed", 0});
    }
    const auto verified = verify_directory(directory);
    if (!verified.has_value()) {
        return Result<CrashRecorder>::failure(verified.error());
    }
    prune_records(directory);
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(),
                        static_cast<ULONG>(random.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return Result<CrashRecorder>::failure(
            {ErrorCode::platform_failure,
             L"A unique crash record identity could not be created", 0});
    }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring name = L"crash-";
    name.reserve(43);
    for (const auto value : random) {
        name.push_back(digits[value >> 4U]);
        name.push_back(digits[value & 0x0FU]);
    }
    name += L".json";
    const auto path = directory / name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<CrashRecorder>::failure(
            {ErrorCode::io_failure, L"Crash record file cannot be reserved",
             GetLastError()});
    }
    build_text.fill('\0');
    const auto count = std::min(build_identity.size(), build_text.size() - 1);
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(build_identity[index]);
        build_text[index] = character >= 0x20 && character < 0x7f &&
                            character != '"' && character != '\\'
            ? static_cast<char>(character) : '_';
    }
    InterlockedExchange(&record_written, 0);
    InterlockedExchangePointer(&crash_file, file);
    previous_filter = SetUnhandledExceptionFilter(unhandled_exception_filter);
    return Result<CrashRecorder>::success(CrashRecorder{path});
}

Result<bool> CrashRecorder::write_for_testing(
    std::uint32_t exception_code, std::uintptr_t address) noexcept {
    if (!active_ || !write_record(exception_code, 0, address)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Crash record could not be written", 0});
    }
    return Result<bool>::success(true);
}

const std::filesystem::path& CrashRecorder::pending_path() const noexcept {
    return pending_path_;
}

void CrashRecorder::disarm() noexcept {
    if (!active_) return;
    active_ = false;
    SetUnhandledExceptionFilter(previous_filter);
    previous_filter = nullptr;
    HANDLE file = static_cast<HANDLE>(
        InterlockedExchangePointer(&crash_file, nullptr));
    if (file && file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (InterlockedCompareExchange(&record_written, 0, 0) == 0 &&
        !pending_path_.empty()) {
        DeleteFileW(pending_path_.c_str());
    }
}

std::size_t retained_crash_record_count(
    const std::filesystem::path& directory) noexcept {
    try {
        std::error_code error;
        std::size_t count = 0;
        for (std::filesystem::directory_iterator iterator{directory, error}, end;
            !error && iterator != end && count <= maximum_records;
             iterator.increment(error)) {
            if (!valid_record_name(iterator->path()) ||
                !safe_regular_file(iterator->path())) continue;
            const auto size = iterator->file_size(error);
            if (!error && size > 0 && size <= 4096) ++count;
        }
        return error ? 0 : count;
    } catch (...) {
        return 0;
    }
}

}  // namespace kf2::diagnostics
