#include "kf2/game/offline_telemetry_lab.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::game {
namespace {

// The current SDK-compiled package is about 393 KiB. Keep a bounded ceiling
// with compiler-metadata headroom while rejecting unrelated large files.
// The compiled package is currently a little above 512 KiB after adding the
// native debug-marker renderer. Keep a strict ceiling while leaving room for
// normal UnrealScript package growth.
constexpr std::uintmax_t kMaximumModuleBytes = 1024U * 1024U;
constexpr std::uintmax_t kMinimumOptimizerModuleBytes = 64U * 1024U;
constexpr std::uintmax_t kMaximumMarkerBytes = 1024U;
constexpr wchar_t kModuleName[] = L"KF2OptimizerTelemetry.u";
constexpr wchar_t kStateDirectoryName[] = L"offline-telemetry-lab";
constexpr wchar_t kMarkerName[] = L"module.marker";
constexpr DWORD kDeleteRetryCount = 100;
constexpr DWORD kDeleteRetryDelayMs = 50;

struct DirectoryIdentity {
    std::uint64_t volume{0};
    std::uint64_t file{0};
};

struct Marker {
    std::string state;
    std::string sha256;
    DirectoryIdentity root;
    std::array<bool, 2> created{};
};

Result<bool> delete_file_after_transient_release(
    const std::filesystem::path& path, std::wstring_view failure_message) {
    DWORD native = ERROR_SUCCESS;
    for (DWORD attempt = 0; attempt < kDeleteRetryCount; ++attempt) {
        if (DeleteFileW(path.c_str())) return Result<bool>::success(true);
        native = GetLastError();
        if (native != ERROR_SHARING_VIOLATION &&
            native != ERROR_LOCK_VIOLATION &&
            native != ERROR_USER_MAPPED_FILE) {
            break;
        }
        if (attempt + 1 < kDeleteRetryCount) Sleep(kDeleteRetryDelayMs);
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure, std::wstring{failure_message}, native});
}

template <typename Integer>
bool parse_integer(std::string_view text, Integer& value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

Result<DirectoryIdentity> directory_identity(
    const std::filesystem::path& path) {
    HANDLE directory = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return Result<DirectoryIdentity>::failure(
            {ErrorCode::access_denied,
             L"Offline telemetry directory identity cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool valid = GetFileInformationByHandle(directory, &information) &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const DWORD native = valid ? ERROR_SUCCESS : GetLastError();
    CloseHandle(directory);
    if (!valid) {
        return Result<DirectoryIdentity>::failure(
            {ErrorCode::access_denied,
             L"Offline telemetry directory identity is unsafe", native});
    }
    return Result<DirectoryIdentity>::success({
        information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
            information.nFileIndexLow});
}

Result<std::string> read_regular_file(const std::filesystem::path& path,
                                      std::uintmax_t maximum_bytes) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found,
             L"Offline telemetry file was not found", GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION before{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &before) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        before.nNumberOfLinks != 1 ||
        static_cast<std::uintmax_t>(size.QuadPart) > maximum_bytes) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Offline telemetry file identity or size is unsafe", native});
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) ||
            read == 0) {
            const DWORD native = GetLastError();
            CloseHandle(file);
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"Offline telemetry file cannot be read", native});
        }
        offset += read;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    const bool stable = GetFileInformationByHandle(file, &after) &&
        before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
        before.nFileIndexHigh == after.nFileIndexHigh &&
        before.nFileIndexLow == after.nFileIndexLow &&
        before.nFileSizeHigh == after.nFileSizeHigh &&
        before.nFileSizeLow == after.nFileSizeLow &&
        before.ftLastWriteTime.dwHighDateTime ==
            after.ftLastWriteTime.dwHighDateTime &&
        before.ftLastWriteTime.dwLowDateTime ==
            after.ftLastWriteTime.dwLowDateTime;
    const DWORD native = stable ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!stable) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Offline telemetry file changed while it was read", native});
    }
    return Result<std::string>::success(std::move(bytes));
}

Result<bool> validate_roots(const std::filesystem::path& config_root,
                            const std::filesystem::path& state_root) {
    if (config_root.empty() || state_root.empty() ||
        !config_root.is_absolute() || !state_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry paths must be absolute", 0});
    }
    auto config_name = config_root.filename().wstring();
    std::transform(config_name.begin(), config_name.end(), config_name.begin(),
                   [](wchar_t value) { return std::towlower(value); });
    if (config_name != L"config" || config_root.parent_path().empty()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry configuration root is not a KF2 Config directory",
             0});
    }
    auto config = directory_identity(config_root);
    if (!config.has_value()) return Result<bool>::failure(config.error());
    auto kf_game = directory_identity(config_root.parent_path());
    if (!kf_game.has_value()) return Result<bool>::failure(kf_game.error());
    const DWORD state_attributes = GetFileAttributesW(state_root.c_str());
    if (state_attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD native = GetLastError();
        if ((native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) ||
            !CreateDirectoryW(state_root.c_str(), nullptr)) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Offline telemetry state root cannot be created",
                 native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND
                     ? GetLastError() : native});
        }
    }
    auto state = directory_identity(state_root);
    if (!state.has_value()) return Result<bool>::failure(state.error());
    return Result<bool>::success(true);
}

Result<std::array<bool, 2>> ensure_target_directories(
    const std::filesystem::path& config_root) {
    std::array<bool, 2> created{};
    auto current = config_root.parent_path();
    constexpr std::array<std::wstring_view, 2> components{
        L"Published", L"BrewedPC"};
    for (std::size_t index = 0; index < components.size(); ++index) {
        current /= components[index];
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD native = GetLastError();
            if (native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) {
                return Result<std::array<bool, 2>>::failure(
                    {ErrorCode::io_failure,
                     L"Offline telemetry target directory cannot be inspected",
                     native});
            }
            if (!CreateDirectoryW(current.c_str(), nullptr)) {
                return Result<std::array<bool, 2>>::failure(
                    {ErrorCode::io_failure,
                     L"Offline telemetry target directory cannot be created",
                     GetLastError()});
            }
            created[index] = true;
        }
        auto identity = directory_identity(current);
        if (!identity.has_value()) {
            return Result<std::array<bool, 2>>::failure(identity.error());
        }
    }
    return Result<std::array<bool, 2>>::success(created);
}

std::filesystem::path target_module(const std::filesystem::path& config_root) {
    return config_root.parent_path() / L"Published" / L"BrewedPC" /
        kModuleName;
}

std::filesystem::path marker_path(const std::filesystem::path& state_root) {
    return state_root / kStateDirectoryName / kMarkerName;
}

std::wstring quote_command_argument(std::wstring_view value) {
    std::wstring quoted{L"\""};
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(character);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

Result<std::filesystem::path> current_executable_path() {
    std::vector<wchar_t> path(32'768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::platform_failure,
             L"Portable cleanup executable path is unavailable",
             GetLastError()});
    }
    return Result<std::filesystem::path>::success(
        std::filesystem::path{std::wstring{path.data(), length}});
}

bool optimizer_module_signature(std::string_view bytes) {
    if (bytes.size() < kMinimumOptimizerModuleBytes ||
        static_cast<unsigned char>(bytes[0]) != 0xC1U ||
        static_cast<unsigned char>(bytes[1]) != 0x83U ||
        static_cast<unsigned char>(bytes[2]) != 0x2AU ||
        static_cast<unsigned char>(bytes[3]) != 0x9EU) {
        return false;
    }
    constexpr std::array<std::string_view, 5> owned_classes{
        "KF2OptimizerTelemetryProbe",
        "KF2OptimizerTelemetryMutator",
        "KF2OptimizerTelemetryInteraction",
        "KF2OptimizerAdaptiveControlListener",
        "KF2OptimizerAdaptiveGraphics"};
    return std::ranges::all_of(owned_classes, [&bytes](const auto name) {
        return bytes.find(name) != std::string_view::npos;
    });
}

Result<bool> remove_orphaned_optimizer_module(
    const std::filesystem::path& config_root) {
    const auto target = target_module(config_root);
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD native = GetLastError();
        if (native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND) {
            return Result<bool>::success(false);
        }
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Offline telemetry target cannot be inspected", native});
    }
    auto bytes = read_regular_file(target, kMaximumModuleBytes);
    if (!bytes.has_value()) {
        // An unrecognized or unsafe occupant is user-owned and must remain.
        return Result<bool>::success(false);
    }
    auto captured_hash = security::sha256_hex(bytes.value());
    if (!captured_hash.has_value()) {
        return Result<bool>::failure(captured_hash.error());
    }
    if (captured_hash.value() != kOfflineTelemetryModuleSha256 &&
        !optimizer_module_signature(bytes.value())) {
        return Result<bool>::success(false);
    }
    auto live_hash = security::sha256_file_hex(target, kMaximumModuleBytes);
    if (!live_hash.has_value() || live_hash.value() != captured_hash.value()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Offline telemetry target changed during orphan cleanup", 0});
    }
    return delete_file_after_transient_release(
        target, L"Orphaned optimizer telemetry package could not be removed");
}

std::string marker_bytes(std::string_view state,
                         const DirectoryIdentity& root,
                         const std::array<bool, 2>& created) {
    return "schema=2\nstate=" + std::string{state} +
        "\nsha256=" + kOfflineTelemetryModuleSha256 +
        "\nroot_volume=" + std::to_string(root.volume) +
        "\nroot_file=" + std::to_string(root.file) +
        "\ncreated_published=" + (created[0] ? "1" : "0") +
        "\ncreated_brewedpc=" + (created[1] ? "1" : "0") +
        "\n";
}

Result<Marker> parse_marker(const std::filesystem::path& path) {
    auto bytes = read_regular_file(path, kMaximumMarkerBytes);
    if (!bytes.has_value()) return Result<Marker>::failure(bytes.error());
    Marker marker;
    bool schema = false, state = false, hash = false, volume = false,
         file = false;
    std::array<bool, 2> created_seen{};
    std::size_t offset = 0;
    while (offset < bytes.value().size()) {
        const auto end = bytes.value().find('\n', offset);
        const auto line = std::string_view{bytes.value()}.substr(
            offset, end == std::string::npos
                        ? bytes.value().size() - offset : end - offset);
        if (line == "schema=2" && !schema) {
            schema = true;
        } else if (line.starts_with("state=") && !state) {
            marker.state = std::string{line.substr(6)};
            state = marker.state == "installing" ||
                    marker.state == "installed";
        } else if (line.starts_with("sha256=") && !hash) {
            marker.sha256 = std::string{line.substr(7)};
            hash = marker.sha256.size() == 64U &&
                std::all_of(marker.sha256.begin(), marker.sha256.end(),
                    [](char value) {
                        return (value >= '0' && value <= '9') ||
                               (value >= 'a' && value <= 'f');
                    });
        } else if (line.starts_with("root_volume=") && !volume) {
            volume = parse_integer(line.substr(12), marker.root.volume);
        } else if (line.starts_with("root_file=") && !file) {
            file = parse_integer(line.substr(10), marker.root.file);
        } else {
            constexpr std::array<std::string_view, 2> names{
                "created_published=", "created_brewedpc="};
            bool matched = false;
            for (std::size_t index = 0; index < names.size(); ++index) {
                if (!line.starts_with(names[index]) || created_seen[index]) {
                    continue;
                }
                const auto value = line.substr(names[index].size());
                if (value != "0" && value != "1") {
                    return Result<Marker>::failure(
                        {ErrorCode::invalid_argument,
                         L"Offline telemetry marker flag is invalid", 0});
                }
                marker.created[index] = value == "1";
                created_seen[index] = true;
                matched = true;
                break;
            }
            if (!matched && !line.empty()) {
                return Result<Marker>::failure(
                    {ErrorCode::invalid_argument,
                     L"Offline telemetry marker contains an unknown field", 0});
            }
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (!schema || !state || !hash || !volume || !file ||
        !created_seen[0] || !created_seen[1]) {
        return Result<Marker>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry marker is incomplete or invalid", 0});
    }
    return Result<Marker>::success(std::move(marker));
}

Result<bool> ensure_state_directory(const std::filesystem::path& state_root) {
    const auto directory = state_root / kStateDirectoryName;
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD native = GetLastError();
        if ((native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) ||
            !CreateDirectoryW(directory.c_str(), nullptr)) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Offline telemetry state directory cannot be created",
                 native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND
                     ? GetLastError() : native});
        }
    }
    auto identity = directory_identity(directory);
    if (!identity.has_value()) return Result<bool>::failure(identity.error());
    return Result<bool>::success(true);
}

void remove_created_empty_directories(
    const std::filesystem::path& config_root,
    const std::array<bool, 2>& created) noexcept {
    const auto root = config_root.parent_path() / L"Published";
    const auto brewed = root / L"BrewedPC";
    const std::array<std::filesystem::path, 2> paths{brewed, root};
    const std::array<bool, 2> flags{created[1], created[0]};
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (flags[index]) {
            static_cast<void>(RemoveDirectoryW(paths[index].c_str()));
        }
    }
}

Result<bool> validate_binding(const std::filesystem::path& config_root,
                              const Marker& marker) {
    auto identity = directory_identity(config_root.parent_path());
    if (!identity.has_value()) return Result<bool>::failure(identity.error());
    if (identity.value().volume != marker.root.volume ||
        identity.value().file != marker.root.file) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Offline telemetry marker belongs to a different KF2 profile",
             0});
    }
    return Result<bool>::success(true);
}

}  // namespace

Result<bool> install_offline_telemetry_lab(
    const OfflineTelemetryLabOptions& options) {
    if (options.game_running) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"KF2 must be stopped before offline telemetry is installed", 0});
    }
    auto roots = validate_roots(options.config_root, options.state_root);
    if (!roots.has_value()) return roots;
    if (options.module_asset.filename() != kModuleName) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry package has an unexpected name", 0});
    }
    auto source = read_regular_file(options.module_asset, kMaximumModuleBytes);
    if (!source.has_value()) return Result<bool>::failure(source.error());
    auto source_hash = security::sha256_hex(source.value());
    if (!source_hash.has_value()) return Result<bool>::failure(source_hash.error());
    if (source_hash.value() != kOfflineTelemetryModuleSha256) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Offline telemetry package does not match the pinned build", 0});
    }
    const auto marker = marker_path(options.state_root);
    const auto target = target_module(options.config_root);
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"An unfinished offline telemetry session requires recovery", 0});
    }
    auto orphan_removed = remove_orphaned_optimizer_module(options.config_root);
    if (!orphan_removed.has_value()) {
        return Result<bool>::failure(orphan_removed.error());
    }
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"The offline telemetry target is already occupied; it was not overwritten",
             0});
    }
    auto directories = ensure_target_directories(options.config_root);
    if (!directories.has_value()) {
        return Result<bool>::failure(directories.error());
    }
    auto state_directory = ensure_state_directory(options.state_root);
    if (!state_directory.has_value()) {
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(state_directory.error());
    }
    auto root_identity = directory_identity(options.config_root.parent_path());
    if (!root_identity.has_value()) {
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(root_identity.error());
    }
    auto marked = platform::windows::atomic_replace_utf8(
        marker, marker_bytes("installing", root_identity.value(),
                             directories.value()));
    if (!marked.has_value()) {
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(marked.error());
    }
    auto installed = platform::windows::atomic_replace_utf8(
        target, source.value());
    if (!installed.has_value()) {
        static_cast<void>(DeleteFileW(marker.c_str()));
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(installed.error());
    }
    auto target_hash = security::sha256_file_hex(target, kMaximumModuleBytes);
    if (!target_hash.has_value() ||
        target_hash.value() != kOfflineTelemetryModuleSha256) {
        static_cast<void>(DeleteFileW(target.c_str()));
        static_cast<void>(DeleteFileW(marker.c_str()));
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Offline telemetry installation hash verification failed", 0});
    }
    auto committed = platform::windows::atomic_replace_utf8(
        marker, marker_bytes("installed", root_identity.value(),
                             directories.value()));
    if (!committed.has_value()) {
        static_cast<void>(DeleteFileW(target.c_str()));
        static_cast<void>(DeleteFileW(marker.c_str()));
        remove_created_empty_directories(options.config_root,
                                         directories.value());
        return Result<bool>::failure(committed.error());
    }
    return Result<bool>::success(true);
}

Result<bool> restore_offline_telemetry_lab(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running) {
    if (game_running) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Running KF2 still owns the offline telemetry package", 0});
    }
    auto roots = validate_roots(config_root, state_root);
    if (!roots.has_value()) return roots;
    const auto marker_file = marker_path(state_root);
    if (GetFileAttributesW(marker_file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        auto orphan_removed = remove_orphaned_optimizer_module(config_root);
        if (!orphan_removed.has_value()) {
            return Result<bool>::failure(orphan_removed.error());
        }
        if (orphan_removed.value()) return Result<bool>::success(true);
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"No active offline telemetry session exists", 0});
    }
    auto marker = parse_marker(marker_file);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());
    auto bound = validate_binding(config_root, marker.value());
    if (!bound.has_value()) return bound;
    const auto target = target_module(config_root);
    const DWORD target_attributes = GetFileAttributesW(target.c_str());
    if (target_attributes != INVALID_FILE_ATTRIBUTES) {
        auto bytes = read_regular_file(target, kMaximumModuleBytes);
        if (!bytes.has_value()) {
            return Result<bool>::failure(bytes.error());
        }
        auto hash = security::sha256_hex(bytes.value());
        if (!hash.has_value() || hash.value() != marker.value().sha256 ||
            !optimizer_module_signature(bytes.value())) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"Offline telemetry target changed; the foreign file was preserved",
                 0});
        }
        auto removed = delete_file_after_transient_release(
            target, L"Offline telemetry package could not be removed");
        if (!removed.has_value()) return removed;
    } else {
        const DWORD native = GetLastError();
        if (native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Offline telemetry target cannot be inspected", native});
        }
    }
    if (!DeleteFileW(marker_file.c_str())) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Offline telemetry marker could not be removed", GetLastError()});
    }
    remove_created_empty_directories(config_root, marker.value().created);
    static_cast<void>(RemoveDirectoryW(
        (state_root / kStateDirectoryName).c_str()));
    return Result<bool>::success(true);
}

Result<OfflineTelemetryRecovery> recover_offline_telemetry_lab(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running) {
    const auto marker_file = marker_path(state_root);
    const DWORD attributes = GetFileAttributesW(marker_file.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD native = GetLastError();
        if (native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND) {
            if (game_running) {
                return Result<OfflineTelemetryRecovery>::success({});
            }
            auto roots = validate_roots(config_root, state_root);
            if (!roots.has_value()) {
                return Result<OfflineTelemetryRecovery>::failure(roots.error());
            }
            auto orphan_removed = remove_orphaned_optimizer_module(config_root);
            if (!orphan_removed.has_value()) {
                return Result<OfflineTelemetryRecovery>::failure(
                    orphan_removed.error());
            }
            if (orphan_removed.value()) {
                return Result<OfflineTelemetryRecovery>::success(
                    {false, true});
            }
            return Result<OfflineTelemetryRecovery>::success({});
        }
        return Result<OfflineTelemetryRecovery>::failure(
            {ErrorCode::io_failure,
             L"Offline telemetry marker cannot be inspected", native});
    }
    auto marker = parse_marker(marker_file);
    if (!marker.has_value()) {
        return Result<OfflineTelemetryRecovery>::failure(marker.error());
    }
    auto bound = validate_binding(config_root, marker.value());
    if (!bound.has_value()) {
        return Result<OfflineTelemetryRecovery>::failure(bound.error());
    }
    const auto target = target_module(config_root);
    auto hash = security::sha256_file_hex(target, kMaximumModuleBytes);
    const bool exact_target = hash.has_value() &&
        hash.value() == kOfflineTelemetryModuleSha256 &&
        marker.value().sha256 == kOfflineTelemetryModuleSha256;
    if (game_running) {
        if (marker.value().state != "installed" || !exact_target) {
            return Result<OfflineTelemetryRecovery>::failure(
                {ErrorCode::stale_data,
                 L"Running KF2 offline telemetry state is incomplete", 0});
        }
        return Result<OfflineTelemetryRecovery>::success({true, false});
    }
    auto restored = restore_offline_telemetry_lab(
        config_root, state_root, false);
    if (!restored.has_value()) {
        return Result<OfflineTelemetryRecovery>::failure(restored.error());
    }
    return Result<OfflineTelemetryRecovery>::success({false, true});
}

Result<bool> launch_offline_telemetry_cleanup_helper(
    const OfflineTelemetryCleanupHelperOptions& options) {
    if (options.wait_process_id == 0 || options.config_root.empty() ||
        options.state_root.empty() || !options.config_root.is_absolute() ||
        !options.state_root.is_absolute() || options.wait_timeout_ms == 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry cleanup helper input is invalid", 0});
    }
    const auto executable = current_executable_path();
    if (!executable.has_value()) {
        return Result<bool>::failure(executable.error());
    }
    std::wstring command = quote_command_argument(executable.value().wstring()) +
        L" --offline-telemetry-cleanup " +
        std::to_wstring(options.wait_process_id) + L" " +
        quote_command_argument(options.config_root.wstring()) + L" " +
        quote_command_argument(options.state_root.wstring()) + L" " +
        std::to_wstring(options.wait_timeout_ms);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.value().c_str(), command.data(), nullptr,
            nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
            executable.value().parent_path().c_str(), &startup, &process)) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Temporary telemetry cleanup helper could not start",
             GetLastError()});
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return Result<bool>::success(true);
}

Result<bool> run_offline_telemetry_cleanup_helper(
    const OfflineTelemetryCleanupHelperOptions& options) {
    if (options.wait_process_id == 0 || options.config_root.empty() ||
        options.state_root.empty() || !options.config_root.is_absolute() ||
        !options.state_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline telemetry cleanup helper input is invalid", 0});
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, options.wait_process_id);
    if (process != nullptr) {
        const DWORD waited = WaitForSingleObject(process,
            options.wait_timeout_ms);
        CloseHandle(process);
        if (waited == WAIT_TIMEOUT) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"KF2 still owns the telemetry package after the bounded cleanup wait",
                 WAIT_TIMEOUT});
        }
        if (waited != WAIT_OBJECT_0) {
            return Result<bool>::failure(
                {ErrorCode::platform_failure,
                 L"Telemetry cleanup could not verify process termination",
                 GetLastError()});
        }
    } else {
        const DWORD native = GetLastError();
        if (native != ERROR_INVALID_PARAMETER) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Telemetry cleanup could not inspect the owning process",
                 native});
        }
    }
    return restore_offline_telemetry_lab(
        options.config_root, options.state_root, false);
}

}  // namespace kf2::game
