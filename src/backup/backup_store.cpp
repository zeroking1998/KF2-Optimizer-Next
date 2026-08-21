#include "kf2/backup/backup_store.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <sstream>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::backup {
namespace {

constexpr std::uintmax_t max_manifest_bytes = 256U * 1024U;
constexpr std::uintmax_t max_object_bytes = 16U * 1024U * 1024U;
constexpr std::size_t max_snapshot_count = 3;

Result<std::string> read_bounded_regular_file(
    const std::filesystem::path& path, std::uintmax_t maximum_size) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found, L"Backup file cannot be opened", GetLastError()});
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
             L"Backup file identity or size is unsafe", native});
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
                {ErrorCode::io_failure, L"Backup file cannot be read", native});
        }
        offset += read;
    }
    CloseHandle(file);
    return Result<std::string>::success(std::move(bytes));
}

void add_identity(FileSnapshot& snapshot, const std::filesystem::path& target) {
    HANDLE file = CreateFileW(target.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(file, &information)) {
        snapshot.volume_serial = information.dwVolumeSerialNumber;
        snapshot.file_index =
            (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
            information.nFileIndexLow;
    }
    CloseHandle(file);
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
    if ((text.size() % 2) != 0) return std::nullopt;
    auto nibble = [](char value) -> int {
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
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path path_from_bytes(const std::string& bytes) {
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()}};
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(delimiter, start);
        parts.emplace_back(text.substr(start, end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

bool valid_hash(std::string_view hash) {
    return hash.size() == 64 &&
           hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

bool allowed_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path() ||
        !path.parent_path().empty()) return false;
    const auto name = path.filename().wstring();
    return name == L"KFEngine.ini" || name == L"KFGame.ini" ||
           name == L"KFSystemSettings.ini";
}

Result<bool> remove_safe_regular_file(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Backup file cannot be opened for pruning",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(file, &information) &&
                      (information.dwFileAttributes &
                       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
                      information.nNumberOfLinks == 1;
    if (!safe) {
        CloseHandle(file);
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Backup pruning rejected an unsafe file identity", 0});
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(
            file, FileDispositionInfo, &disposition, sizeof(disposition))) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Backup file cannot be pruned", native});
    }
    CloseHandle(file);
    return Result<bool>::success(true);
}

std::string identity_material(const std::filesystem::path& root,
                              const std::vector<FileSnapshot>& snapshots) {
    std::string material;
    for (const auto& snapshot : snapshots) {
        material += hex_encode(path_bytes(root)) + "\n" +
                    hex_encode(path_bytes(snapshot.relative_path)) + "\n" +
                    snapshot.sha256 + "\n" + snapshot.desired_sha256 + "\n";
    }
    return material;
}

}  // namespace

BackupStore::BackupStore(std::filesystem::path state_root)
    : state_root_{std::move(state_root)} {}

Result<BackupSet> BackupStore::create(const config::ConfigPreview& preview) {
    if (preview.config_root.empty() || !preview.config_root.is_absolute() ||
        preview.files.empty() || preview.files.size() > max_snapshot_count) {
        return Result<BackupSet>::failure(
            {ErrorCode::invalid_argument, L"Backup preview scope is invalid", 0});
    }
    std::set<std::filesystem::path> unique_paths;
    for (const auto& file : preview.files) {
        if (!allowed_relative_path(file.relative_path) ||
            !unique_paths.insert(file.relative_path).second ||
            file.original_bytes.size() > max_object_bytes ||
            file.proposed_bytes.size() > max_object_bytes) {
            return Result<BackupSet>::failure(
                {ErrorCode::access_denied,
                 L"Backup preview contains an unsafe file record", 0});
        }
    }
    std::error_code error;
    const auto objects = state_root_ / L"backups/objects";
    const auto manifests = state_root_ / L"backups/manifests";
    const auto journals = state_root_ / L"backups/journals";
    std::filesystem::create_directories(objects, error);
    if (!error) std::filesystem::create_directories(manifests, error);
    if (!error) std::filesystem::create_directories(journals, error);
    if (error) {
        return Result<BackupSet>::failure(
            {ErrorCode::io_failure, L"Backup directories cannot be created",
             static_cast<std::uint32_t>(error.value())});
    }

    BackupSet backup;
    backup.config_root = preview.config_root;
    for (const auto& file : preview.files) {
        auto digest = security::sha256_hex(file.original_bytes);
        if (!digest.has_value()) return Result<BackupSet>::failure(digest.error());
        FileSnapshot snapshot;
        snapshot.relative_path = file.relative_path;
        snapshot.size = file.original_bytes.size();
        snapshot.sha256 = digest.value();
        snapshot.desired_size = file.proposed_bytes.size();
        auto desired_digest = security::sha256_hex(file.proposed_bytes);
        if (!desired_digest.has_value()) {
            return Result<BackupSet>::failure(desired_digest.error());
        }
        snapshot.desired_sha256 = desired_digest.value();
        snapshot.object_path = objects / (std::wstring{digest.value().begin(), digest.value().end()} + L".blob");
        add_identity(snapshot, preview.config_root / file.relative_path);
        if (std::filesystem::exists(snapshot.object_path)) {
            const auto existing_bytes = read_bounded_regular_file(
                snapshot.object_path, max_object_bytes);
            if (!existing_bytes.has_value()) {
                return Result<BackupSet>::failure(existing_bytes.error());
            }
            const auto existing = security::sha256_hex(existing_bytes.value());
            if (!existing.has_value() || existing_bytes.value().size() != snapshot.size ||
                existing.value() != snapshot.sha256) {
                return Result<BackupSet>::failure(
                    {ErrorCode::io_failure, L"Existing backup object failed verification", 0});
            }
        } else {
            auto written = platform::windows::atomic_replace_utf8(
                snapshot.object_path, file.original_bytes);
            if (!written.has_value()) return Result<BackupSet>::failure(written.error());
        }
        backup.snapshots.push_back(std::move(snapshot));
    }
    auto backup_id = security::sha256_hex(
        identity_material(backup.config_root, backup.snapshots));
    if (!backup_id.has_value()) return Result<BackupSet>::failure(backup_id.error());
    backup.id = backup_id.value();
    const std::wstring wide_id{backup.id.begin(), backup.id.end()};
    backup.manifest_path = manifests / (wide_id + L".manifest");
    backup.journal_path = journals / (wide_id + L".journal");
    std::ostringstream manifest;
    manifest << "version=2\nid=" << backup.id << '\n'
             << "root=" << hex_encode(path_bytes(backup.config_root)) << '\n';
    for (const auto& snapshot : backup.snapshots) {
        manifest << "file=" << hex_encode(path_bytes(snapshot.relative_path)) << '|'
                 << snapshot.size << '|' << snapshot.sha256 << '|'
                 << snapshot.desired_size << '|' << snapshot.desired_sha256 << '\n';
    }
    auto manifest_written = platform::windows::atomic_replace_utf8(
        backup.manifest_path, manifest.str());
    if (!manifest_written.has_value()) {
        return Result<BackupSet>::failure(manifest_written.error());
    }
    auto journal_written = platform::windows::atomic_replace_utf8(
        backup.journal_path, "version=1\nstate=backup_complete\nid=" + backup.id + "\n");
    if (!journal_written.has_value()) {
        return Result<BackupSet>::failure(journal_written.error());
    }
    return Result<BackupSet>::success(std::move(backup));
}

Result<BackupSet> BackupStore::create_standalone(
    const config::ConfigPreview& preview) {
    auto backup = create(preview);
    if (!backup.has_value()) return backup;
    auto verified = verify(backup.value());
    if (!verified.has_value()) {
        return Result<BackupSet>::failure(verified.error());
    }
    const auto completed = platform::windows::atomic_replace_utf8(
        backup.value().journal_path,
        "version=1\nstate=complete\nid=" + backup.value().id + "\n");
    if (!completed.has_value()) {
        return Result<BackupSet>::failure(completed.error());
    }
    return backup;
}

Result<bool> BackupStore::verify(const BackupSet& backup) const {
    auto loaded = load_backup(backup.id);
    if (!loaded.has_value()) return Result<bool>::failure(loaded.error());
    if (path_bytes(loaded.value().config_root) != path_bytes(backup.config_root) ||
        loaded.value().snapshots.size() != backup.snapshots.size()) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Backup manifest identity changed", 0});
    }
    for (std::size_t index = 0; index < backup.snapshots.size(); ++index) {
        const auto& expected = backup.snapshots[index];
        const auto& snapshot = loaded.value().snapshots[index];
        if (path_bytes(snapshot.relative_path) != path_bytes(expected.relative_path) ||
            snapshot.size != expected.size || snapshot.sha256 != expected.sha256 ||
            snapshot.desired_size != expected.desired_size ||
            snapshot.desired_sha256 != expected.desired_sha256) {
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"Backup manifest record changed", 0});
        }
        auto bytes = read_bounded_regular_file(snapshot.object_path, max_object_bytes);
        if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
        auto digest = security::sha256_hex(bytes.value());
        if (!digest.has_value()) return Result<bool>::failure(digest.error());
        if (bytes.value().size() != snapshot.size || digest.value() != snapshot.sha256) {
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"Backup object verification failed", 0});
        }
    }
    return Result<bool>::success(true);
}

Result<BackupSet> BackupStore::load_backup(std::string_view id) const {
    if (id.size() != 64 || id.find_first_not_of("0123456789abcdef") != std::string_view::npos) {
        return Result<BackupSet>::failure(
            {ErrorCode::invalid_argument, L"Backup identifier is invalid", 0});
    }
    BackupSet backup;
    backup.id = id;
    const std::wstring wide_id{id.begin(), id.end()};
    backup.manifest_path = state_root_ / L"backups/manifests" / (wide_id + L".manifest");
    backup.journal_path = state_root_ / L"backups/journals" / (wide_id + L".journal");
    const auto manifest = read_bounded_regular_file(
        backup.manifest_path, max_manifest_bytes);
    if (!manifest.has_value()) return Result<BackupSet>::failure(manifest.error());
    if (manifest.value().empty()) return Result<BackupSet>::failure(
        {ErrorCode::io_failure, L"Backup manifest is empty", 0});
    std::istringstream lines{manifest.value()};
    std::string line;
    bool version_ok = false, id_ok = false, root_ok = false;
    std::set<std::filesystem::path> unique_paths;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "version=2" && !version_ok) version_ok = true;
        else if (line == "id=" + backup.id && !id_ok) id_ok = true;
        else if (line.starts_with("root=")) {
            if (root_ok) return Result<BackupSet>::failure(
                {ErrorCode::io_failure, L"Backup root record is duplicated", 0});
            const auto decoded = hex_decode(std::string_view{line}.substr(5));
            if (!decoded || decoded->empty() || decoded->size() > 65534 ||
                decoded->find('\0') != std::string::npos) {
                return Result<BackupSet>::failure(
                {ErrorCode::io_failure, L"Backup root encoding is corrupt", 0});
            }
            backup.config_root = path_from_bytes(*decoded);
            if (!backup.config_root.is_absolute()) return Result<BackupSet>::failure(
                {ErrorCode::access_denied, L"Backup root is not absolute", 0});
            root_ok = true;
        } else if (line.starts_with("file=")) {
            if (backup.snapshots.size() >= max_snapshot_count) {
                return Result<BackupSet>::failure(
                    {ErrorCode::access_denied, L"Backup file count exceeds the allowlist", 0});
            }
            const auto fields = split(std::string_view{line}.substr(5), '|');
            if (fields.size() != 5) return Result<BackupSet>::failure(
                {ErrorCode::io_failure, L"Backup file record is corrupt", 0});
            const auto decoded = hex_decode(fields[0]);
            if (!decoded || decoded->empty() || decoded->find('\0') != std::string::npos) {
                return Result<BackupSet>::failure(
                {ErrorCode::io_failure, L"Backup path encoding is corrupt", 0});
            }
            FileSnapshot snapshot;
            snapshot.relative_path = path_from_bytes(*decoded);
            if (!allowed_relative_path(snapshot.relative_path) ||
                !unique_paths.insert(snapshot.relative_path).second) {
                return Result<BackupSet>::failure(
                    {ErrorCode::access_denied,
                     L"Backup file path is outside the strict allowlist", 0});
            }
            try {
                snapshot.size = std::stoull(fields[1]);
                snapshot.desired_size = std::stoull(fields[3]);
            } catch (...) {
                return Result<BackupSet>::failure(
                    {ErrorCode::io_failure, L"Backup size record is corrupt", 0});
            }
            snapshot.sha256 = fields[2];
            snapshot.desired_sha256 = fields[4];
            if (snapshot.size > max_object_bytes ||
                snapshot.desired_size > max_object_bytes ||
                !valid_hash(snapshot.sha256) ||
                !valid_hash(snapshot.desired_sha256)) {
                return Result<BackupSet>::failure(
                    {ErrorCode::io_failure, L"Backup hash or size record is corrupt", 0});
            }
            snapshot.object_path = state_root_ / L"backups/objects" /
                (std::wstring{snapshot.sha256.begin(), snapshot.sha256.end()} + L".blob");
            backup.snapshots.push_back(std::move(snapshot));
        } else if (!line.empty()) {
            return Result<BackupSet>::failure(
                {ErrorCode::io_failure, L"Backup manifest contains an unknown record", 0});
        }
    }
    if (!version_ok || !id_ok || !root_ok || backup.snapshots.empty()) {
        return Result<BackupSet>::failure(
            {ErrorCode::io_failure, L"Backup manifest is incomplete", 0});
    }
    auto recomputed = security::sha256_hex(
        identity_material(backup.config_root, backup.snapshots));
    if (!recomputed.has_value()) return Result<BackupSet>::failure(recomputed.error());
    if (recomputed.value() != backup.id) {
        return Result<BackupSet>::failure(
            {ErrorCode::io_failure, L"Backup manifest identity does not match its content", 0});
    }
    return Result<BackupSet>::success(std::move(backup));
}

Result<std::vector<BackupSet>> BackupStore::list_backups() const {
    std::vector<BackupSet> backups;
    std::error_code error;
    const auto manifests = state_root_ / L"backups/manifests";
    if (!std::filesystem::exists(manifests, error)) {
        if (!error) return Result<std::vector<BackupSet>>::success({});
        return Result<std::vector<BackupSet>>::failure(
            {ErrorCode::io_failure, L"Backup list cannot be read",
             static_cast<std::uint32_t>(error.value())});
    }
    for (const auto& entry : std::filesystem::directory_iterator(manifests, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != L".manifest") continue;
        auto loaded = load_backup(entry.path().stem().string());
        if (!loaded.has_value()) return Result<std::vector<BackupSet>>::failure(loaded.error());
        backups.push_back(std::move(loaded.value()));
    }
    if (error) return Result<std::vector<BackupSet>>::failure(
        {ErrorCode::io_failure, L"Backup list enumeration failed",
         static_cast<std::uint32_t>(error.value())});
    std::sort(backups.begin(), backups.end(), [](const auto& left, const auto& right) {
        std::error_code left_error, right_error;
        return std::filesystem::last_write_time(left.manifest_path, left_error) >
               std::filesystem::last_write_time(right.manifest_path, right_error);
    });
    return Result<std::vector<BackupSet>>::success(std::move(backups));
}

Result<std::size_t> BackupStore::prune_verified(RetentionPolicy policy) {
    if (policy.keep_latest == 0) policy.keep_latest = 1;
    auto listed = list_backups();
    if (!listed.has_value()) return Result<std::size_t>::failure(listed.error());
    std::size_t removed = 0;
    for (std::size_t index = policy.keep_latest; index < listed.value().size(); ++index) {
        const auto verified = verify(listed.value()[index]);
        if (!verified.has_value()) continue;
        auto journal_removed = remove_safe_regular_file(
            listed.value()[index].journal_path);
        if (!journal_removed.has_value()) {
            return Result<std::size_t>::failure(journal_removed.error());
        }
        auto manifest_removed = remove_safe_regular_file(
            listed.value()[index].manifest_path);
        if (!manifest_removed.has_value()) {
            return Result<std::size_t>::failure(manifest_removed.error());
        }
        ++removed;
    }

    auto retained = list_backups();
    if (!retained.has_value()) {
        return Result<std::size_t>::failure(retained.error());
    }
    std::set<std::string> referenced_objects;
    for (const auto& backup : retained.value()) {
        for (const auto& snapshot : backup.snapshots) {
            referenced_objects.insert(snapshot.sha256);
        }
    }
    const auto objects = state_root_ / L"backups/objects";
    std::error_code error;
    if (std::filesystem::exists(objects, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(objects, error)) {
            if (error) break;
            const auto stem = entry.path().stem().string();
            if (entry.path().extension() != L".blob" || !valid_hash(stem) ||
                referenced_objects.contains(stem)) {
                continue;
            }
            auto object_removed = remove_safe_regular_file(entry.path());
            if (!object_removed.has_value()) {
                return Result<std::size_t>::failure(object_removed.error());
            }
        }
        if (error) {
            return Result<std::size_t>::failure(
                {ErrorCode::io_failure, L"Backup object cleanup failed",
                 static_cast<std::uint32_t>(error.value())});
        }
    } else if (error) {
        return Result<std::size_t>::failure(
            {ErrorCode::io_failure, L"Backup object directory cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<std::size_t>::success(removed);
}

const std::filesystem::path& BackupStore::state_root() const noexcept {
    return state_root_;
}

}  // namespace kf2::backup
