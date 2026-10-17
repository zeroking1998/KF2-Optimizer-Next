#include "kf2/game/anchor_runtime_lab.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::game {
namespace {

constexpr wchar_t kHelperName[] = L"kf2_anchor_runtime.dll";
constexpr wchar_t kBackupName[] = L"kf2_anchor_runtime.pre-lab.dll";
constexpr wchar_t kMarkerName[] = L"anchor-runtime-transaction.marker";
constexpr std::uintmax_t kMaximumHelperBytes = 1024U * 1024U;
constexpr std::uintmax_t kMaximumMarkerBytes = 1024U;

struct Marker {
    std::string state;
    std::string installed;
    std::string original;
    std::string owner;
};

bool valid_hash(std::string_view hash) {
    return hash == "absent" ||
        (hash.size() == 64U &&
         hash.find_first_not_of("0123456789abcdef") ==
             std::string_view::npos);
}

std::string marker_text(const Marker& marker) {
    return "schema=1\nstate=" + marker.state +
        "\ninstalled_sha256=" + marker.installed +
        "\noriginal_sha256=" + marker.original +
        "\nowner_sha256=" + marker.owner + "\n";
}

Result<Marker> read_marker(const std::filesystem::path& path) {
    auto read = platform::windows::read_bounded_verified_file(
        path, kMaximumMarkerBytes);
    if (!read.has_value()) return Result<Marker>::failure(read.error());
    const auto field = [&read](std::string_view key) {
        const auto start = read.value().find(key);
        if (start == std::string::npos) return std::string{};
        const auto value = start + key.size();
        const auto end = read.value().find('\n', value);
        return end == std::string::npos
            ? std::string{}
            : read.value().substr(value, end - value);
    };
    Marker marker{field("state="), field("installed_sha256="),
                  field("original_sha256="), field("owner_sha256=")};
    if (!read.value().starts_with("schema=1\n") ||
        (marker.state != "installing" && marker.state != "installed") ||
        !valid_hash(marker.installed) || marker.installed == "absent" ||
        !valid_hash(marker.original) || marker.owner == "absent" ||
        !valid_hash(marker.owner) || read.value() != marker_text(marker)) {
        return Result<Marker>::failure(
            {ErrorCode::invalid_argument,
             L"Anchor runtime recovery marker is invalid", 0});
    }
    return Result<Marker>::success(std::move(marker));
}

Result<std::string> read_helper(const std::filesystem::path& path) {
    return platform::windows::read_bounded_verified_file(
        path, kMaximumHelperBytes);
}

Result<std::string> digest(std::string_view bytes) {
    return security::sha256_hex(bytes);
}

Result<std::string> installation_identity(
    const std::filesystem::path& game_directory) {
    HANDLE directory = CreateFileW(
        game_directory.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Anchor runtime installation identity cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(directory, &info) != FALSE &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const DWORD native = valid ? ERROR_SUCCESS : GetLastError();
    CloseHandle(directory);
    const auto file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) |
        info.nFileIndexLow;
    if (!valid || file == 0) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Anchor runtime installation identity is unsafe", native});
    }
    return security::sha256_hex(
        std::to_string(info.dwVolumeSerialNumber) + ":" +
        std::to_string(file));
}

Result<bool> inspected_exists(const std::filesystem::path& path) {
    std::error_code error;
    const bool present = std::filesystem::exists(path, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Anchor runtime path cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(present);
}

Result<bool> validate_paths(const std::filesystem::path& game,
                            const std::filesystem::path& state) {
    if (!game.is_absolute() || !state.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Anchor runtime paths must be absolute", 0});
    }
    const DWORD attributes = GetFileAttributesW(game.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Anchor runtime game directory is unsafe", GetLastError()});
    }
    std::error_code error;
    std::filesystem::create_directories(state, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Anchor runtime state directory cannot be created",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(true);
}

Result<bool> remove_installed(const std::filesystem::path& path,
                              std::string_view expected_hash,
                              bool allow_absent) {
    const auto present = inspected_exists(path);
    if (!present.has_value()) return present;
    if (!present.value()) return allow_absent
        ? Result<bool>::success(false)
        : Result<bool>::failure(
              {ErrorCode::not_found,
               L"Anchor runtime helper is missing", 0});
    auto bytes = read_helper(path);
    if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
    auto hash = digest(bytes.value());
    if (!hash.has_value()) return Result<bool>::failure(hash.error());
    if (hash.value() != expected_hash) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Anchor runtime helper changed; the foreign file was preserved",
             0});
    }
    return platform::windows::remove_file_if_unchanged(path, bytes.value());
}

}  // namespace

Result<bool> install_anchor_runtime_lab(
    const AnchorRuntimeLabOptions& options) {
    if (options.game_running) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"KF2 must be stopped before the anchor runtime is installed", 0});
    }
    auto valid = validate_paths(options.game_directory,
                                options.state_directory);
    if (!valid.has_value()) return valid;
    if (options.helper_asset.filename() != kHelperName) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Anchor runtime helper has an unexpected name", 0});
    }

    const auto marker_path = options.state_directory / kMarkerName;
    auto marker_exists = inspected_exists(marker_path);
    if (!marker_exists.has_value()) return marker_exists;
    if (marker_exists.value()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"An unfinished anchor runtime transaction requires recovery",
             0});
    }

    auto source = read_helper(options.helper_asset);
    if (!source.has_value()) return Result<bool>::failure(source.error());
    auto source_hash = digest(source.value());
    if (!source_hash.has_value()) {
        return Result<bool>::failure(source_hash.error());
    }

    auto owner = installation_identity(options.game_directory);
    if (!owner.has_value()) return Result<bool>::failure(owner.error());
    const auto target = options.game_directory / kHelperName;
    const auto backup = options.state_directory / kBackupName;
    auto target_exists = inspected_exists(target);
    if (!target_exists.has_value()) return target_exists;
    std::string original = "absent";
    if (target_exists.value()) {
        auto bytes = read_helper(target);
        if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
        auto hash = digest(bytes.value());
        if (!hash.has_value()) return Result<bool>::failure(hash.error());
        original = hash.value();
        auto backed_up = platform::windows::atomic_replace_utf8(
            backup, bytes.value());
        if (!backed_up.has_value()) return backed_up;
    }

    Marker marker{"installing", source_hash.value(), original,
                  owner.value()};
    auto marked = platform::windows::atomic_replace_utf8(
        marker_path, marker_text(marker));
    if (!marked.has_value()) return marked;
    auto installed = platform::windows::atomic_replace_utf8(
        target, source.value());
    if (!installed.has_value() || options.simulate_failure_after_install) {
        const auto restored = restore_anchor_runtime_lab(
            options.game_directory, options.state_directory, false);
        if (!restored.has_value()) return restored;
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Anchor runtime installation failed and was rolled back", 0});
    }
    marker.state = "installed";
    auto committed = platform::windows::atomic_replace_utf8(
        marker_path, marker_text(marker));
    if (!committed.has_value()) {
        const auto restored = restore_anchor_runtime_lab(
            options.game_directory, options.state_directory, false);
        if (!restored.has_value()) return restored;
        return committed;
    }
    return Result<bool>::success(true);
}

Result<bool> restore_anchor_runtime_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running) {
    if (game_running) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Running KF2 still owns the anchor runtime", 0});
    }
    auto valid = validate_paths(game_directory, state_directory);
    if (!valid.has_value()) return valid;
    const auto marker_path = state_directory / kMarkerName;
    auto marker_exists = inspected_exists(marker_path);
    if (!marker_exists.has_value()) return marker_exists;
    if (!marker_exists.value()) return Result<bool>::failure(
        {ErrorCode::not_found,
         L"Anchor runtime recovery marker is missing", 0});
    auto marker = read_marker(marker_path);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());
    auto owner = installation_identity(game_directory);
    if (!owner.has_value()) return Result<bool>::failure(owner.error());
    if (owner.value() != marker.value().owner) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Anchor runtime recovery belongs to another KF2 installation",
             0});
    }

    const auto target = game_directory / kHelperName;
    const auto backup = state_directory / kBackupName;
    if (marker.value().original == "absent") {
        auto removed = remove_installed(
            target, marker.value().installed, marker.value().state == "installing");
        if (!removed.has_value()) return removed;
    } else {
        auto backup_bytes = read_helper(backup);
        if (!backup_bytes.has_value()) {
            return Result<bool>::failure(backup_bytes.error());
        }
        auto backup_hash = digest(backup_bytes.value());
        if (!backup_hash.has_value() ||
            backup_hash.value() != marker.value().original) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Anchor runtime recovery backup is invalid", 0});
        }
        const auto target_exists = inspected_exists(target);
        if (!target_exists.has_value()) return target_exists;
        if (target_exists.value()) {
            auto current = read_helper(target);
            if (!current.has_value()) return Result<bool>::failure(current.error());
            auto current_hash = digest(current.value());
            if (!current_hash.has_value() ||
                (current_hash.value() != marker.value().installed &&
                 current_hash.value() != marker.value().original)) {
                return Result<bool>::failure(
                    {ErrorCode::stale_data,
                     L"Anchor runtime helper changed; the foreign file was preserved",
                     0});
            }
        }
        auto restored = platform::windows::atomic_replace_utf8(
            target, backup_bytes.value());
        if (!restored.has_value()) return restored;
        auto removed_backup = remove_installed(
            backup, marker.value().original, false);
        if (!removed_backup.has_value()) return removed_backup;
    }

    auto marker_bytes = platform::windows::read_bounded_verified_file(
        marker_path, kMaximumMarkerBytes);
    if (!marker_bytes.has_value()) {
        return Result<bool>::failure(marker_bytes.error());
    }
    auto removed_marker = platform::windows::remove_file_if_unchanged(
        marker_path, marker_bytes.value());
    if (!removed_marker.has_value()) return removed_marker;
    std::error_code ignored;
    std::filesystem::remove(state_directory, ignored);
    return Result<bool>::success(true);
}

Result<bool> recover_anchor_runtime_lab(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& state_directory,
    bool game_running) {
    const auto marker_path = state_directory / kMarkerName;
    auto marker_exists = inspected_exists(marker_path);
    if (!marker_exists.has_value()) return marker_exists;
    if (!marker_exists.value()) return Result<bool>::success(false);
    auto marker = read_marker(marker_path);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());
    if (game_running) {
        if (marker.value().state != "installed") {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Running KF2 anchor runtime state is incomplete", 0});
        }
        const auto target = game_directory / kHelperName;
        auto bytes = read_helper(target);
        if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
        auto hash = digest(bytes.value());
        if (!hash.has_value() || hash.value() != marker.value().installed) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Running KF2 anchor runtime identity is invalid", 0});
        }
        return Result<bool>::success(false);
    }
    return restore_anchor_runtime_lab(
        game_directory, state_directory, false);
}

}  // namespace kf2::game
