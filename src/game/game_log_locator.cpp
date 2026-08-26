#include "kf2/game/game_log_locator.hpp"

#include <Windows.h>

#include <string_view>
#include <utility>

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

}  // namespace kf2::game
