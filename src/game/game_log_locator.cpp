#include "kf2/game/game_log_locator.hpp"

#include <Windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kf2/game/game_log_session.hpp"

namespace kf2::game {
namespace {

bool is_launch_log_name(std::wstring_view name) noexcept {
    constexpr std::wstring_view prefix = L"Launch_";
    constexpr std::wstring_view suffix = L".log";
    if (name == L"Launch.log") return true;
    if (!name.starts_with(prefix) || !name.ends_with(suffix) ||
        name.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    const auto index = name.substr(
        prefix.size(), name.size() - prefix.size() - suffix.size());
    for (const wchar_t character : index) {
        if (character < L'0' || character > L'9') return false;
    }
    return true;
}

bool is_in_use(const std::filesystem::path& path) noexcept {
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

std::optional<std::wstring> parse_render_adapter_name(
    std::string_view contents) {
    constexpr std::string_view marker = "Log: Adapter :";
    const auto marker_offset = contents.find(marker);
    if (marker_offset == std::string_view::npos) return std::nullopt;
    auto value = contents.substr(marker_offset + marker.size());
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    const auto line_end = value.find_first_of("\r\n");
    if (line_end != std::string_view::npos) value = value.substr(0, line_end);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    if (value.empty() || value.size() > 256) return std::nullopt;
    if (std::ranges::any_of(value, [](unsigned char character) {
            return character < 0x20 || character == 0x7f;
        })) {
        return std::nullopt;
    }

    const auto convert = [value](UINT code_page, DWORD flags)
        -> std::optional<std::wstring> {
        const int characters = MultiByteToWideChar(
            code_page, flags, value.data(), static_cast<int>(value.size()),
            nullptr, 0);
        if (characters <= 0 || characters > 256) return std::nullopt;
        std::wstring result(static_cast<std::size_t>(characters), L'\0');
        if (MultiByteToWideChar(code_page, flags, value.data(),
                static_cast<int>(value.size()), result.data(), characters) !=
            characters) {
            return std::nullopt;
        }
        return result;
    };
    if (auto utf8 = convert(CP_UTF8, MB_ERR_INVALID_CHARS)) return utf8;
    return convert(CP_ACP, 0);
}

}  // namespace

Result<std::optional<ActiveGameLog>> find_active_game_log(
    const std::filesystem::path& log_directory,
    std::uint64_t process_start_filetime) {
    if (log_directory.empty() || !log_directory.is_absolute() ||
        process_start_filetime == 0) {
        return Result<std::optional<ActiveGameLog>>::failure({
            ErrorCode::invalid_argument,
            L"KF2 log discovery requires an absolute directory and process identity",
            0});
    }

    const auto search = log_directory / L"Launch*.log";
    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW(search.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return Result<std::optional<ActiveGameLog>>::success(std::nullopt);
        }
        return Result<std::optional<ActiveGameLog>>::failure({
            ErrorCode::platform_failure,
            L"KF2 launch logs could not be enumerated", error});
    }

    std::optional<ActiveGameLog> selected;
    std::uint64_t selected_creation = 0;
    bool selected_in_use = false;
    do {
        if (!is_launch_log_name(data.cFileName) ||
            (data.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            continue;
        }
        const auto last_write =
            (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime)
             << 32U) |
            data.ftLastWriteTime.dwLowDateTime;
        if (!game_log_belongs_to_process(last_write, process_start_filetime)) {
            continue;
        }
        const auto creation =
            (static_cast<std::uint64_t>(data.ftCreationTime.dwHighDateTime)
             << 32U) |
            data.ftCreationTime.dwLowDateTime;
        const auto path = log_directory / data.cFileName;
        const bool candidate_in_use = is_in_use(path);
        if (!selected || (candidate_in_use && !selected_in_use) ||
            (candidate_in_use == selected_in_use &&
             (last_write > selected->last_write_filetime ||
              (last_write == selected->last_write_filetime &&
               creation > selected_creation)))) {
            selected = ActiveGameLog{path, last_write};
            selected_creation = creation;
            selected_in_use = candidate_in_use;
        }
    } while (FindNextFileW(handle, &data));
    const auto enumeration_error = GetLastError();
    FindClose(handle);
    if (enumeration_error != ERROR_NO_MORE_FILES) {
        return Result<std::optional<ActiveGameLog>>::failure({
            ErrorCode::platform_failure,
            L"KF2 launch-log enumeration did not complete", enumeration_error});
    }
    return Result<std::optional<ActiveGameLog>>::success(std::move(selected));
}

Result<std::optional<std::wstring>> find_last_render_adapter_name(
    const std::filesystem::path& log_directory) {
    if (log_directory.empty() || !log_directory.is_absolute()) {
        return Result<std::optional<std::wstring>>::failure({
            ErrorCode::invalid_argument,
            L"KF2 renderer discovery requires an absolute log directory", 0});
    }

    const auto search = log_directory / L"Launch*.log";
    WIN32_FIND_DATAW data{};
    HANDLE enumeration = FindFirstFileW(search.c_str(), &data);
    if (enumeration == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return Result<std::optional<std::wstring>>::success(std::nullopt);
        }
        return Result<std::optional<std::wstring>>::failure({
            ErrorCode::platform_failure,
            L"KF2 renderer logs could not be enumerated", error});
    }

    std::optional<std::filesystem::path> selected;
    std::uint64_t selected_write = 0;
    std::uint64_t selected_creation = 0;
    do {
        if (!is_launch_log_name(data.cFileName) ||
            (data.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            continue;
        }
        const auto last_write =
            (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime)
             << 32U) |
            data.ftLastWriteTime.dwLowDateTime;
        const auto creation =
            (static_cast<std::uint64_t>(data.ftCreationTime.dwHighDateTime)
             << 32U) |
            data.ftCreationTime.dwLowDateTime;
        if (!selected || last_write > selected_write ||
            (last_write == selected_write && creation > selected_creation)) {
            selected = log_directory / data.cFileName;
            selected_write = last_write;
            selected_creation = creation;
        }
    } while (FindNextFileW(enumeration, &data));
    const auto enumeration_error = GetLastError();
    FindClose(enumeration);
    if (enumeration_error != ERROR_NO_MORE_FILES) {
        return Result<std::optional<std::wstring>>::failure({
            ErrorCode::platform_failure,
            L"KF2 renderer-log enumeration did not complete",
            enumeration_error});
    }
    if (!selected) {
        return Result<std::optional<std::wstring>>::success(std::nullopt);
    }

    HANDLE file = CreateFileW(selected->c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::optional<std::wstring>>::failure({
            ErrorCode::platform_failure,
            L"The latest KF2 renderer log could not be opened", GetLastError()});
    }
    constexpr DWORD maximum_bytes = 1024U * 1024U;
    std::vector<char> buffer(maximum_bytes);
    DWORD bytes_read = 0;
    const BOOL read = ReadFile(file, buffer.data(), maximum_bytes,
                               &bytes_read, nullptr);
    const auto read_error = read ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!read) {
        return Result<std::optional<std::wstring>>::failure({
            ErrorCode::platform_failure,
            L"The latest KF2 renderer log could not be read", read_error});
    }
    return Result<std::optional<std::wstring>>::success(
        parse_render_adapter_name(
            std::string_view{buffer.data(), bytes_read}));
}

}  // namespace kf2::game
