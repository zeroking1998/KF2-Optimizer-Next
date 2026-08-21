#include "kf2/config/session_guard.hpp"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::config {
namespace {

constexpr std::size_t maximum_files = 256;
// A valid snapshot may contain up to one directory per INI file plus the
// files, manifest and root folders. Bound the whole tree, not just files.
constexpr std::size_t maximum_tree_entries = maximum_files * 2 + 8;
constexpr std::uintmax_t maximum_file_bytes = 16U * 1024U * 1024U;
constexpr std::uintmax_t maximum_total_bytes = 128U * 1024U * 1024U;
constexpr std::uintmax_t maximum_manifest_bytes = 1024U * 1024U;

struct FileRecord {
    std::filesystem::path relative;
    std::uintmax_t size{0};
    std::string hash;
};

struct ParsedManifest {
    int schema{0};
    std::uint64_t root_volume{0};
    std::uint64_t root_file{0};
    std::vector<FileRecord> files;
};

template <typename Integer>
bool parse_integer(std::string_view text, Integer& value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

bool valid_hash(std::string_view hash) {
    return hash.size() == 64 &&
           hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::string hex_encode(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

std::optional<std::string> hex_decode(std::string_view text) {
    if (text.empty() || (text.size() % 2) != 0 || text.size() > 8192) {
        return std::nullopt;
    }
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    std::string result;
    result.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const int high = nibble(text[index]);
        const int low = nibble(text[index + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return result;
}

std::string path_bytes(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::optional<std::filesystem::path> path_from_bytes(std::string_view bytes) {
    if (bytes.empty() || bytes.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    try {
        return std::filesystem::path{std::u8string{
            reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()}};
    } catch (...) {
        return std::nullopt;
    }
}

bool safe_relative_ini(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_path() ||
        path_bytes(relative).size() > 4096) return false;
    auto extension = relative.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return std::towlower(value); });
    if (extension != L".ini") return false;
    for (const auto& component : relative) {
        const auto value = component.wstring();
        if (value.empty() || value == L"." || value == L".." ||
            value.find(L':') != std::wstring::npos ||
            value.back() == L' ' || value.back() == L'.') return false;
        for (const wchar_t character : value) {
            if (character < 0x20) return false;
        }
    }
    return true;
}

Result<std::pair<std::uint64_t, std::uint64_t>> directory_identity(
    const std::filesystem::path& path) {
    HANDLE directory = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return Result<std::pair<std::uint64_t, std::uint64_t>>::failure(
            {ErrorCode::access_denied, L"Directory identity cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(directory, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        const DWORD native = GetLastError();
        CloseHandle(directory);
        return Result<std::pair<std::uint64_t, std::uint64_t>>::failure(
            {ErrorCode::access_denied, L"Directory identity is unsafe", native});
    }
    CloseHandle(directory);
    return Result<std::pair<std::uint64_t, std::uint64_t>>::success({
        information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
            information.nFileIndexLow});
}

Result<std::string> read_verified_file(const std::filesystem::path& path,
                                       std::uintmax_t maximum_size) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found, L"Session configuration file is missing",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &information) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 ||
        static_cast<std::uintmax_t>(size.QuadPart) > maximum_size) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Session configuration file identity or size is unsafe", native});
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, MAXDWORD));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) ||
            read == 0) {
            const DWORD native = GetLastError();
            CloseHandle(file);
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"Session configuration file cannot be read", native});
        }
        offset += read;
    }
    CloseHandle(file);
    return Result<std::string>::success(std::move(bytes));
}

Result<bool> validate_snapshot_tree(const std::filesystem::path& root) {
    auto root_identity = directory_identity(root);
    if (!root_identity.has_value()) return Result<bool>::failure(root_identity.error());
    std::error_code error;
    std::size_t entries = 0;
    for (std::filesystem::recursive_directory_iterator iterator{
             root, std::filesystem::directory_options::skip_permission_denied,
             error}, end;
         iterator != end; iterator.increment(error)) {
        if (error || ++entries > maximum_tree_entries) break;
        const DWORD attributes = GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Session snapshot contains an unsafe filesystem object", 0});
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            auto checked = read_verified_file(iterator->path(), maximum_file_bytes);
            if (!checked.has_value()) return Result<bool>::failure(checked.error());
        }
    }
    if (error || entries > maximum_tree_entries) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Session snapshot tree cannot be safely enumerated", 0});
    }
    return Result<bool>::success(true);
}

Result<bool> remove_snapshot_tree(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) return Result<bool>::success(true);
    auto safe = validate_snapshot_tree(root);
    if (!safe.has_value()) return safe;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Session snapshot cannot be removed",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(true);
}

Result<ParsedManifest> parse_manifest(const std::filesystem::path& path) {
    auto document = read_verified_file(path, maximum_manifest_bytes);
    if (!document.has_value()) {
        return Result<ParsedManifest>::failure(document.error());
    }
    std::istringstream lines{document.value()};
    std::string line;
    ParsedManifest manifest;
    bool schema_seen = false, volume_seen = false, file_id_seen = false;
    std::set<std::filesystem::path> unique;
    std::uintmax_t total_size = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line == "schema=1" || line == "schema=2") {
            if (schema_seen) return Result<ParsedManifest>::failure(
                {ErrorCode::io_failure, L"Session manifest schema is duplicated", 0});
            manifest.schema = line.back() - '0';
            schema_seen = true;
            continue;
        }
        if (line.starts_with("root_volume=")) {
            if (volume_seen ||
                !parse_integer(std::string_view{line}.substr(12),
                               manifest.root_volume)) {
                return Result<ParsedManifest>::failure(
                    {ErrorCode::io_failure, L"Session root volume is invalid", 0});
            }
            volume_seen = true;
            continue;
        }
        if (line.starts_with("root_file=")) {
            if (file_id_seen ||
                !parse_integer(std::string_view{line}.substr(10),
                               manifest.root_file)) {
                return Result<ParsedManifest>::failure(
                    {ErrorCode::io_failure, L"Session root identity is invalid", 0});
            }
            file_id_seen = true;
            continue;
        }
        if (!line.starts_with("file=") || manifest.files.size() >= maximum_files) {
            return Result<ParsedManifest>::failure(
                {ErrorCode::io_failure, L"Session manifest record is invalid", 0});
        }
        FileRecord record;
        if (manifest.schema == 2) {
            const auto first = line.find('|', 5);
            const auto second = first == std::string::npos
                ? std::string::npos : line.find('|', first + 1);
            if (first == std::string::npos || second == std::string::npos ||
                line.find('|', second + 1) != std::string::npos) {
                return Result<ParsedManifest>::failure(
                    {ErrorCode::io_failure, L"Session file record is malformed", 0});
            }
            const auto decoded = hex_decode(
                std::string_view{line}.substr(5, first - 5));
            const auto relative = decoded ? path_from_bytes(*decoded) : std::nullopt;
            if (!relative || !parse_integer(
                    std::string_view{line}.substr(first + 1, second - first - 1),
                    record.size)) {
                return Result<ParsedManifest>::failure(
                    {ErrorCode::io_failure, L"Session file record is malformed", 0});
            }
            record.relative = *relative;
            record.hash = line.substr(second + 1);
        } else if (manifest.schema == 1) {
            const auto separator = line.rfind('|');
            if (separator == std::string::npos || separator <= 5) {
                return Result<ParsedManifest>::failure(
                    {ErrorCode::io_failure, L"Legacy session record is malformed", 0});
            }
            const auto relative = path_from_bytes(
                std::string_view{line}.substr(5, separator - 5));
            if (!relative) return Result<ParsedManifest>::failure(
                {ErrorCode::io_failure, L"Legacy session path is invalid", 0});
            record.relative = *relative;
            record.hash = line.substr(separator + 1);
        } else {
            return Result<ParsedManifest>::failure(
                {ErrorCode::io_failure, L"Session manifest schema must be first", 0});
        }
        if (!safe_relative_ini(record.relative) || !valid_hash(record.hash) ||
            !unique.insert(record.relative).second ||
            record.size > maximum_file_bytes ||
            total_size > maximum_total_bytes - record.size) {
            return Result<ParsedManifest>::failure(
                {ErrorCode::access_denied,
                 L"Session file record is outside the strict allowlist", 0});
        }
        total_size += record.size;
        manifest.files.push_back(std::move(record));
    }
    if (!schema_seen || manifest.files.empty() ||
        (manifest.schema == 2 && (!volume_seen || !file_id_seen)) ||
        (manifest.schema == 1 && (volume_seen || file_id_seen))) {
        return Result<ParsedManifest>::failure(
            {ErrorCode::io_failure, L"Session configuration manifest is incomplete", 0});
    }
    return Result<ParsedManifest>::success(std::move(manifest));
}

Result<bool> validate_root_binding(const std::filesystem::path& config_root,
                                   const ParsedManifest& manifest) {
    auto current = directory_identity(config_root);
    if (!current.has_value()) return Result<bool>::failure(current.error());
    if (manifest.schema == 2 &&
        (current.value().first != manifest.root_volume ||
         current.value().second != manifest.root_file)) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Session snapshot belongs to a different KF2 configuration directory",
             0});
    }
    return Result<bool>::success(true);
}

Result<bool> ensure_safe_parent(const std::filesystem::path& root,
                                const std::filesystem::path& relative_parent) {
    auto current = root;
    for (const auto& component : relative_parent) {
        current /= component;
        std::error_code error;
        if (!std::filesystem::exists(current, error)) {
            if (error || !std::filesystem::create_directory(current, error) || error) {
                return Result<bool>::failure(
                    {ErrorCode::io_failure,
                     L"Configuration restore directory cannot be created",
                     static_cast<std::uint32_t>(error.value())});
            }
        }
        auto identity = directory_identity(current);
        if (!identity.has_value()) return Result<bool>::failure(identity.error());
    }
    return Result<bool>::success(true);
}

}  // namespace

Result<SessionConfigSnapshot> capture_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root) {
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(config_root, error);
    if (error || !std::filesystem::is_directory(canonical_root)) {
        return Result<SessionConfigSnapshot>::failure(
            {ErrorCode::invalid_argument, L"KF2 configuration root is invalid", 0});
    }
    auto root_identity = directory_identity(canonical_root);
    if (!root_identity.has_value()) {
        return Result<SessionConfigSnapshot>::failure(root_identity.error());
    }
    const auto session_root = state_root / L"session-config";
    std::filesystem::create_directories(session_root, error);
    if (error) return Result<SessionConfigSnapshot>::failure(
        {ErrorCode::io_failure, L"Session configuration directory cannot be created",
         static_cast<std::uint32_t>(error.value())});
    auto session_identity = directory_identity(session_root);
    if (!session_identity.has_value()) {
        return Result<SessionConfigSnapshot>::failure(session_identity.error());
    }
    const auto snapshot_root = session_root / L"active";
    if (std::filesystem::exists(snapshot_root, error)) {
        if (error || std::filesystem::exists(snapshot_root / L"manifest.txt")) {
            return Result<SessionConfigSnapshot>::failure(
                {ErrorCode::stale_data,
                 L"A verified session configuration snapshot is already active", 0});
        }
        auto removed = remove_snapshot_tree(snapshot_root);
        if (!removed.has_value()) {
            return Result<SessionConfigSnapshot>::failure(removed.error());
        }
    }
    std::filesystem::create_directories(snapshot_root / L"files", error);
    if (error) return Result<SessionConfigSnapshot>::failure(
        {ErrorCode::io_failure, L"Session configuration snapshot cannot be created",
         static_cast<std::uint32_t>(error.value())});

    std::vector<FileRecord> records;
    std::uintmax_t total_size = 0;
    for (std::filesystem::recursive_directory_iterator iterator{
             canonical_root,
             std::filesystem::directory_options::skip_permission_denied, error},
         end;
         iterator != end; iterator.increment(error)) {
        if (error) break;
        const DWORD attributes = GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return Result<SessionConfigSnapshot>::failure(
                {ErrorCode::access_denied,
                 L"KF2 configuration contains a reparse point", 0});
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const auto relative = std::filesystem::relative(
            iterator->path(), canonical_root, error);
        if (error) break;
        if (!safe_relative_ini(relative)) continue;
        if (records.size() >= maximum_files) {
            return Result<SessionConfigSnapshot>::failure(
                {ErrorCode::access_denied,
                 L"KF2 configuration contains too many INI files", 0});
        }
        auto bytes = read_verified_file(iterator->path(), maximum_file_bytes);
        if (!bytes.has_value()) {
            return Result<SessionConfigSnapshot>::failure(bytes.error());
        }
        if (total_size > maximum_total_bytes - bytes.value().size()) {
            return Result<SessionConfigSnapshot>::failure(
                {ErrorCode::access_denied,
                 L"KF2 configuration exceeds the session backup size limit", 0});
        }
        auto hash = security::sha256_hex(bytes.value());
        if (!hash.has_value()) return Result<SessionConfigSnapshot>::failure(hash.error());
        const auto destination = snapshot_root / L"files" / relative;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return Result<SessionConfigSnapshot>::failure(
            {ErrorCode::io_failure, L"Session snapshot subdirectory cannot be created",
             static_cast<std::uint32_t>(error.value())});
        auto written = platform::windows::atomic_replace_utf8(
            destination, bytes.value());
        if (!written.has_value()) {
            return Result<SessionConfigSnapshot>::failure(written.error());
        }
        total_size += bytes.value().size();
        records.push_back({relative, bytes.value().size(), hash.value()});
    }
    if (error || records.empty()) return Result<SessionConfigSnapshot>::failure(
        {ErrorCode::io_failure,
         error ? L"KF2 configuration enumeration failed"
               : L"KF2 configuration contains no safe INI files",
         static_cast<std::uint32_t>(error.value())});

    std::ostringstream manifest;
    manifest << "schema=2\nroot_volume=" << root_identity.value().first
             << "\nroot_file=" << root_identity.value().second << '\n';
    for (const auto& record : records) {
        manifest << "file=" << hex_encode(path_bytes(record.relative)) << '|'
                 << record.size << '|' << record.hash << '\n';
    }
    auto marked = platform::windows::atomic_replace_utf8(
        snapshot_root / L"manifest.txt", manifest.str());
    if (!marked.has_value()) {
        return Result<SessionConfigSnapshot>::failure(marked.error());
    }
    return Result<SessionConfigSnapshot>::success({
        canonical_root, snapshot_root, records.size(),
        root_identity.value().first, root_identity.value().second});
}

Result<std::size_t> restore_session_config(const SessionConfigSnapshot& snapshot) {
    auto tree_safe = validate_snapshot_tree(snapshot.snapshot_root);
    if (!tree_safe.has_value()) {
        return Result<std::size_t>::failure(tree_safe.error());
    }
    auto manifest = parse_manifest(snapshot.snapshot_root / L"manifest.txt");
    if (!manifest.has_value()) return Result<std::size_t>::failure(manifest.error());
    auto root_bound = validate_root_binding(snapshot.config_root, manifest.value());
    if (!root_bound.has_value()) return Result<std::size_t>::failure(root_bound.error());
    if ((snapshot.root_volume != 0 &&
         snapshot.root_volume != manifest.value().root_volume) ||
        (snapshot.root_file != 0 &&
         snapshot.root_file != manifest.value().root_file)) {
        return Result<std::size_t>::failure(
            {ErrorCode::access_denied,
             L"In-memory session root identity does not match the manifest", 0});
    }

    std::size_t restored = 0;
    for (const auto& record : manifest.value().files) {
        auto bytes = read_verified_file(
            snapshot.snapshot_root / L"files" / record.relative,
            maximum_file_bytes);
        if (!bytes.has_value()) return Result<std::size_t>::failure(bytes.error());
        if (manifest.value().schema == 2 && bytes.value().size() != record.size) {
            return Result<std::size_t>::failure(
                {ErrorCode::io_failure,
                 L"Session configuration backup size mismatch", 0});
        }
        auto hash = security::sha256_hex(bytes.value());
        if (!hash.has_value() || hash.value() != record.hash) {
            return Result<std::size_t>::failure(
                {ErrorCode::io_failure,
                 L"Session configuration backup hash mismatch", 0});
        }
        auto parent_ready = ensure_safe_parent(
            snapshot.config_root, record.relative.parent_path());
        if (!parent_ready.has_value()) {
            return Result<std::size_t>::failure(parent_ready.error());
        }
        const auto target = snapshot.config_root / record.relative;
        auto written = platform::windows::atomic_replace_utf8(target, bytes.value());
        if (!written.has_value()) return Result<std::size_t>::failure(written.error());
        auto verified = read_verified_file(target, maximum_file_bytes);
        auto verified_hash = verified.has_value()
            ? security::sha256_hex(verified.value())
            : Result<std::string>::failure(verified.error());
        if (!verified_hash.has_value() || verified_hash.value() != record.hash) {
            return Result<std::size_t>::failure(
                {ErrorCode::io_failure,
                 L"Restored configuration verification failed", 0});
        }
        ++restored;
    }
    auto removed = remove_snapshot_tree(snapshot.snapshot_root);
    if (!removed.has_value()) return Result<std::size_t>::failure(removed.error());
    return Result<std::size_t>::success(restored);
}

Result<std::optional<SessionConfigSnapshot>> resume_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root) {
    const auto snapshot_root = state_root / L"session-config" / L"active";
    if (!std::filesystem::exists(snapshot_root)) {
        return Result<std::optional<SessionConfigSnapshot>>::success(std::nullopt);
    }
    const auto manifest_path = snapshot_root / L"manifest.txt";
    if (!std::filesystem::exists(manifest_path)) {
        auto removed = remove_snapshot_tree(snapshot_root);
        if (!removed.has_value()) {
            return Result<std::optional<SessionConfigSnapshot>>::failure(
                removed.error());
        }
        return Result<std::optional<SessionConfigSnapshot>>::success(std::nullopt);
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(config_root, error);
    if (error || !std::filesystem::is_directory(canonical_root)) {
        return Result<std::optional<SessionConfigSnapshot>>::failure(
            {ErrorCode::invalid_argument, L"KF2 configuration root is invalid", 0});
    }
    auto tree_safe = validate_snapshot_tree(snapshot_root);
    if (!tree_safe.has_value()) {
        return Result<std::optional<SessionConfigSnapshot>>::failure(
            tree_safe.error());
    }
    auto manifest = parse_manifest(manifest_path);
    if (!manifest.has_value()) {
        return Result<std::optional<SessionConfigSnapshot>>::failure(manifest.error());
    }
    auto bound = validate_root_binding(canonical_root, manifest.value());
    if (!bound.has_value()) {
        return Result<std::optional<SessionConfigSnapshot>>::failure(bound.error());
    }
    for (const auto& record : manifest.value().files) {
        auto bytes = read_verified_file(
            snapshot_root / L"files" / record.relative, maximum_file_bytes);
        if (!bytes.has_value()) {
            return Result<std::optional<SessionConfigSnapshot>>::failure(bytes.error());
        }
        auto hash = security::sha256_hex(bytes.value());
        if (!hash.has_value() || hash.value() != record.hash ||
            (manifest.value().schema == 2 && bytes.value().size() != record.size)) {
            return Result<std::optional<SessionConfigSnapshot>>::failure(
                {ErrorCode::io_failure,
                 L"Session configuration backup failed verification", 0});
        }
    }
    return Result<std::optional<SessionConfigSnapshot>>::success(
        SessionConfigSnapshot{canonical_root, snapshot_root,
                              manifest.value().files.size(),
                              manifest.value().root_volume,
                              manifest.value().root_file});
}

Result<std::size_t> recover_session_config(
    const std::filesystem::path& config_root,
    const std::filesystem::path& state_root,
    bool game_running) {
    auto resumed = resume_session_config(config_root, state_root);
    if (!resumed.has_value()) return Result<std::size_t>::failure(resumed.error());
    if (!resumed.value()) return Result<std::size_t>::success(0);
    if (game_running) return Result<std::size_t>::failure(
        {ErrorCode::access_denied,
         L"KF2 is still running; deferred INI recovery cannot start", 0});
    return restore_session_config(*resumed.value());
}

}  // namespace kf2::config
