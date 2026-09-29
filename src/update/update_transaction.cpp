#include "kf2/update/update_transaction.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/update/semantic_version.hpp"

namespace kf2::update {
namespace {

constexpr std::uintmax_t kMaximumManagedFileBytes = 32U * 1024U * 1024U;
constexpr std::uintmax_t kMaximumJournalBytes = 8U * 1024U;
constexpr std::wstring_view kJournalName{L"update-transaction.ini"};
constexpr std::array<std::string_view, 2> kManifestPaths{
    "Data/package-integrity.ini", "Data/package-manifest.json"};

#if defined(KF2_UPDATE_TRANSACTION_TESTING)
ManagedReadHook managed_read_hook{};
#endif

struct ManagedPackageSnapshot {
    std::string source_identity;
    std::string version;
    std::vector<std::string> files;
};

enum class JournalPhase {
    backup_ready,
    replacement_in_progress,
    update_verified,
    rollback_in_progress,
    rollback_verified,
    rollback_failed,
    handoff_ready,
};

struct UpdateJournal {
    JournalPhase phase{JournalPhase::backup_ready};
    std::string target_path_hash;
    std::string staged_path_hash;
    std::string backup_path_hash;
    std::string previous_identity;
    std::string previous_version;
    std::string new_identity;
    std::string new_version;
    std::size_t replaced_files{};
    std::uint32_t owner_process_id{};
    std::uint64_t owner_process_start_id{};
};

std::vector<std::string_view> managed_paths();
bool equal_hash(std::string_view left, std::string_view right) noexcept;

std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(value.data()), value.size()}};
}

std::filesystem::path journal_path(
    const std::filesystem::path& backup_root) {
    return backup_root.parent_path() / kJournalName;
}

Result<std::string> path_hash(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error || absolute.empty()) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Update transaction path cannot be normalized", 0});
    }
    auto wide = absolute.wstring();
    std::ranges::transform(wide, wide.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return security::sha256_hex(std::string_view{
        reinterpret_cast<const char*>(wide.data()),
        wide.size() * sizeof(wchar_t)});
}

std::string_view phase_name(JournalPhase phase) noexcept {
    switch (phase) {
        case JournalPhase::backup_ready: return "backup_ready";
        case JournalPhase::replacement_in_progress:
            return "replacement_in_progress";
        case JournalPhase::update_verified: return "update_verified";
        case JournalPhase::rollback_in_progress:
            return "rollback_in_progress";
        case JournalPhase::rollback_verified: return "rollback_verified";
        case JournalPhase::rollback_failed: return "rollback_failed";
        case JournalPhase::handoff_ready: return "handoff_ready";
    }
    return {};
}

std::optional<JournalPhase> parse_phase(std::string_view value) noexcept {
    if (value == "backup_ready") return JournalPhase::backup_ready;
    if (value == "replacement_in_progress") {
        return JournalPhase::replacement_in_progress;
    }
    if (value == "update_verified") return JournalPhase::update_verified;
    if (value == "rollback_in_progress") {
        return JournalPhase::rollback_in_progress;
    }
    if (value == "rollback_verified") return JournalPhase::rollback_verified;
    if (value == "rollback_failed") return JournalPhase::rollback_failed;
    if (value == "handoff_ready") return JournalPhase::handoff_ready;
    return std::nullopt;
}

std::uint64_t process_start_id(HANDLE process) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32U) |
           creation.dwLowDateTime;
}

bool safe_identity(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 128U &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '.' ||
                   character == '_' || character == '-';
        });
}

bool valid_hash(std::string_view value) noexcept {
    return value.size() == 64U &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

Result<bool> write_journal(const std::filesystem::path& path,
                           const UpdateJournal& journal) {
    const std::string bytes =
        "schema_version=1\nstate=" + std::string{phase_name(journal.phase)} +
        "\ntarget_path_hash=" + journal.target_path_hash +
        "\nstaged_path_hash=" + journal.staged_path_hash +
        "\nbackup_path_hash=" + journal.backup_path_hash +
        "\nprevious_identity=" + journal.previous_identity +
        "\nprevious_version=" + journal.previous_version +
        "\nnew_identity=" + journal.new_identity +
        "\nnew_version=" + journal.new_version +
        "\nreplaced_files=" + std::to_string(journal.replaced_files) +
        "\nowner_process_id=" + std::to_string(journal.owner_process_id) +
        "\nowner_process_start_id=" +
        std::to_string(journal.owner_process_start_id) + "\n";
    return platform::windows::atomic_replace_utf8(path, bytes);
}

template <typename Integer>
bool parse_integer(std::string_view value, Integer& output) noexcept {
    if (value.empty()) return false;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

Result<UpdateJournal> read_journal(const std::filesystem::path& path) {
    const auto bytes = platform::windows::read_bounded_verified_file(
        path, kMaximumJournalBytes);
    if (!bytes.has_value()) {
        return Result<UpdateJournal>::failure(bytes.error());
    }
    std::map<std::string, std::string> values;
    std::size_t offset = 0;
    while (offset < bytes.value().size()) {
        const auto end = bytes.value().find('\n', offset);
        auto line = bytes.value().substr(
            offset, end == std::string::npos ? std::string::npos : end - offset);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        offset = end == std::string::npos ? bytes.value().size() : end + 1U;
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            !values.emplace(line.substr(0, separator),
                            line.substr(separator + 1U)).second) {
            return Result<UpdateJournal>::failure(
                {ErrorCode::invalid_argument,
                 L"Update transaction journal is malformed", 0});
        }
    }
    constexpr std::array<std::string_view, 12> keys{
        "schema_version", "state", "target_path_hash", "staged_path_hash",
        "backup_path_hash", "previous_identity", "previous_version",
        "new_identity", "new_version", "replaced_files",
        "owner_process_id", "owner_process_start_id"};
    if (values.size() != keys.size() || values["schema_version"] != "1") {
        return Result<UpdateJournal>::failure(
            {ErrorCode::invalid_argument,
             L"Update transaction journal schema is invalid", 0});
    }
    for (const auto key : keys) {
        if (!values.contains(std::string{key})) {
            return Result<UpdateJournal>::failure(
                {ErrorCode::invalid_argument,
                 L"Update transaction journal is incomplete", 0});
        }
    }
    const auto phase = parse_phase(values["state"]);
    UpdateJournal journal;
    if (!phase || !parse_integer(values["replaced_files"],
                                 journal.replaced_files) ||
        !parse_integer(values["owner_process_id"],
                       journal.owner_process_id) ||
        !parse_integer(values["owner_process_start_id"],
                       journal.owner_process_start_id) ||
        journal.replaced_files > managed_paths().size() ||
        journal.owner_process_id == 0 || journal.owner_process_start_id == 0 ||
        !valid_hash(values["target_path_hash"]) ||
        !valid_hash(values["staged_path_hash"]) ||
        !valid_hash(values["backup_path_hash"]) ||
        !safe_identity(values["previous_identity"]) ||
        !safe_identity(values["new_identity"]) ||
        !parse_semantic_version(values["previous_version"]).has_value() ||
        !parse_semantic_version(values["new_version"]).has_value()) {
        return Result<UpdateJournal>::failure(
            {ErrorCode::invalid_argument,
             L"Update transaction journal values are invalid", 0});
    }
    journal.phase = *phase;
    journal.target_path_hash = values["target_path_hash"];
    journal.staged_path_hash = values["staged_path_hash"];
    journal.backup_path_hash = values["backup_path_hash"];
    journal.previous_identity = values["previous_identity"];
    journal.previous_version = values["previous_version"];
    journal.new_identity = values["new_identity"];
    journal.new_version = values["new_version"];
    return Result<UpdateJournal>::success(std::move(journal));
}

Result<UpdateJournal> read_bound_journal(
    const UpdateTransactionRequest& request) {
    auto journal = read_journal(journal_path(request.backup_root));
    if (!journal.has_value()) return journal;
    const auto target = path_hash(request.target_root);
    const auto staged = path_hash(request.staged_root);
    const auto backup = path_hash(request.backup_root);
    if (!target.has_value() || !staged.has_value() || !backup.has_value() ||
        !equal_hash(journal.value().target_path_hash, target.value()) ||
        !equal_hash(journal.value().staged_path_hash, staged.value()) ||
        !equal_hash(journal.value().backup_path_hash, backup.value()) ||
        journal.value().new_version != request.expected_new_version) {
        return Result<UpdateJournal>::failure(
            {ErrorCode::access_denied,
             L"Update transaction journal does not match its request", 0});
    }
    return journal;
}

bool owner_is_active(const UpdateJournal& journal) noexcept {
    if (journal.owner_process_id == GetCurrentProcessId() &&
        journal.owner_process_start_id == process_start_id(GetCurrentProcess())) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, journal.owner_process_id);
    if (!process) return false;
    DWORD exit_code = 0;
    const bool active = GetExitCodeProcess(process, &exit_code) &&
        exit_code == STILL_ACTIVE &&
        process_start_id(process) == journal.owner_process_start_id;
    CloseHandle(process);
    return active;
}

std::vector<std::string_view> managed_paths() {
    std::vector<std::string_view> result;
    for (const auto path : security::managed_package_payload_paths()) {
        result.push_back(path);
    }
    result.insert(result.end(), kManifestPaths.begin(), kManifestPaths.end());
    return result;
}

bool normal_directory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(
        platform::windows::extended_length_path(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

Result<std::string> read_managed_file(const std::filesystem::path& path) {
    const auto native_path = platform::windows::extended_length_path(path);
    HANDLE file = CreateFileW(
        native_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"A managed update file cannot be opened", GetLastError()});
    }
    struct CloseFile {
        HANDLE value;
        ~CloseFile() { CloseHandle(value); }
    } close_file{file};
    BY_HANDLE_FILE_INFORMATION before{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &before) ||
        !GetFileSizeEx(file, &size)) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"A managed update file cannot be inspected", GetLastError()});
    }
    if (size.QuadPart < 0 ||
        static_cast<std::uintmax_t>(size.QuadPart) >
            kMaximumManagedFileBytes ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        before.nNumberOfLinks != 1) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"A managed update file has an unsafe identity or size", 0});
    }
#if defined(KF2_UPDATE_TRANSACTION_TESTING)
    if (managed_read_hook) managed_read_hook(path);
#endif
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + total, requested, &read, nullptr) ||
            read == 0) {
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"A managed update file cannot be read completely",
                 GetLastError()});
        }
        total += read;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    LARGE_INTEGER final_size{};
    if (!GetFileInformationByHandle(file, &after) ||
        !GetFileSizeEx(file, &final_size) ||
        after.dwVolumeSerialNumber != before.dwVolumeSerialNumber ||
        after.nFileIndexHigh != before.nFileIndexHigh ||
        after.nFileIndexLow != before.nFileIndexLow ||
        final_size.QuadPart != size.QuadPart ||
        after.ftLastWriteTime.dwHighDateTime !=
            before.ftLastWriteTime.dwHighDateTime ||
        after.ftLastWriteTime.dwLowDateTime !=
            before.ftLastWriteTime.dwLowDateTime) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"A managed update file changed while being read", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

Result<std::string> parse_package_version(std::string_view bytes) {
    if (bytes.size() > 256U * 1024U) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package version manifest is invalid", 0});
    }
    constexpr std::string_view key{"\"package_version\""};
    const auto first = bytes.find(key);
    if (first == std::string_view::npos ||
        bytes.find(key, first + key.size()) != std::string_view::npos) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package version is missing or duplicated", 0});
    }
    auto offset = first + key.size();
    while (offset < bytes.size() && std::isspace(
               static_cast<unsigned char>(bytes[offset]))) ++offset;
    if (offset >= bytes.size() || bytes[offset++] != ':') {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    }
    while (offset < bytes.size() && std::isspace(
               static_cast<unsigned char>(bytes[offset]))) ++offset;
    if (offset >= bytes.size() || bytes[offset++] != '"') {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    }
    const auto end = bytes.find('"', offset);
    if (end == std::string_view::npos) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    }
    const std::string version{bytes.substr(offset, end - offset)};
    if (!parse_semantic_version(version).has_value()) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is invalid", 0});
    }
    return Result<std::string>::success(version);
}

bool equal_hash(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

Result<bool> ensure_parent(const std::filesystem::path& path) {
    const auto native_path = platform::windows::extended_length_path(path);
    std::error_code error;
    std::filesystem::create_directories(native_path, error);
    if (error || !normal_directory(native_path)) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"An update directory has an unsafe identity",
         static_cast<std::uint32_t>(error.value())});
    return Result<bool>::success(true);
}

Result<ManagedPackageSnapshot> capture_managed_package(
    const std::filesystem::path& root, std::string_view source_identity,
    std::string_view expected_version) {
    auto manifest = security::load_package_integrity_manifest(
        root, source_identity);
    if (!manifest.has_value()) {
        return Result<ManagedPackageSnapshot>::failure(manifest.error());
    }
    ManagedPackageSnapshot snapshot{
        .source_identity = std::string{source_identity},
        .version = std::string{expected_version}};
    const auto payloads = security::managed_package_payload_paths();
    snapshot.files.reserve(payloads.size() + kManifestPaths.size());
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        if (index >= manifest.value().files.size() ||
            manifest.value().files[index].relative_path != payloads[index]) {
            return Result<ManagedPackageSnapshot>::failure(
                {ErrorCode::access_denied,
                 L"Package integrity records are out of order", 0});
        }
        auto bytes = read_managed_file(
            root / path_from_utf8(payloads[index]));
        if (!bytes.has_value()) {
            return Result<ManagedPackageSnapshot>::failure(bytes.error());
        }
        auto digest = security::sha256_hex(bytes.value());
        if (!digest.has_value()) {
            return Result<ManagedPackageSnapshot>::failure(digest.error());
        }
        if (!equal_hash(digest.value(),
                        manifest.value().files[index].sha256)) {
            return Result<ManagedPackageSnapshot>::failure(
                {ErrorCode::stale_data,
                 L"A managed update file no longer matches its verified hash",
                 0});
        }
        snapshot.files.push_back(std::move(bytes.value()));
    }
    auto integrity = read_managed_file(
        root / path_from_utf8(kManifestPaths[0]));
    if (!integrity.has_value()) {
        return Result<ManagedPackageSnapshot>::failure(integrity.error());
    }
    if (integrity.value() != manifest.value().document) {
        return Result<ManagedPackageSnapshot>::failure(
            {ErrorCode::stale_data,
             L"Package integrity document changed after verification", 0});
    }
    snapshot.files.push_back(std::move(integrity.value()));
    auto version_manifest = read_managed_file(
        root / path_from_utf8(kManifestPaths[1]));
    if (!version_manifest.has_value()) {
        return Result<ManagedPackageSnapshot>::failure(
            version_manifest.error());
    }
    auto version = parse_package_version(version_manifest.value());
    if (!version.has_value() || version.value() != expected_version) {
        return Result<ManagedPackageSnapshot>::failure(
            {ErrorCode::stale_data,
             L"Package version changed after verification", 0});
    }
    snapshot.files.push_back(std::move(version_manifest.value()));
    return Result<ManagedPackageSnapshot>::success(std::move(snapshot));
}

Result<bool> write_snapshot(const std::filesystem::path& target_root,
                            const ManagedPackageSnapshot& snapshot) {
    const auto paths = managed_paths();
    if (snapshot.files.size() != paths.size()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Managed package snapshot is incomplete", 0});
    }
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto target = target_root / path_from_utf8(paths[index]);
        const auto parent = ensure_parent(target.parent_path());
        if (!parent.has_value()) return parent;
        const auto restored = platform::windows::atomic_replace_utf8(
            target, snapshot.files[index]);
        if (!restored.has_value()) return restored;
    }
    auto verified = capture_managed_package(
        target_root, snapshot.source_identity, snapshot.version);
    if (!verified.has_value()) {
        return Result<bool>::failure(verified.error());
    }
    if (verified.value().files != snapshot.files) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"The managed package could not be written completely", 0});
    }
    return Result<bool>::success(true);
}

Result<bool> target_is_verified_pre_update_package(
    const UpdateTransactionRequest& request) {
    const auto target_version = package_version(request.target_root);
    const auto staged_version = package_version(request.staged_root);
    if (!target_version.has_value() || !staged_version.has_value() ||
        staged_version.value() != request.expected_new_version) {
        return Result<bool>::success(false);
    }
    const auto target_semver = parse_semantic_version(target_version.value());
    const auto staged_semver = parse_semantic_version(staged_version.value());
    if (!target_semver.has_value() || !staged_semver.has_value() ||
        compare_semantic_versions(staged_semver.value(),
                                  target_semver.value()) <= 0) {
        return Result<bool>::success(false);
    }
    const auto target_identity =
        security::package_source_identity(request.target_root);
    const auto staged_identity =
        security::package_source_identity(request.staged_root);
    if (!target_identity.has_value() || !staged_identity.has_value()) {
        return Result<bool>::success(false);
    }
    const auto target = capture_managed_package(
        request.target_root, target_identity.value(), target_version.value());
    const auto staged = capture_managed_package(
        request.staged_root, staged_identity.value(), staged_version.value());
    return Result<bool>::success(target.has_value() && staged.has_value());
}

Result<UpdateTransactionResult> fail_with_rollback(
    const UpdateTransactionRequest& request,
    const ManagedPackageSnapshot& backup, UpdateTransactionResult result,
    Error error) {
    auto journal = read_bound_journal(request);
    if (!journal.has_value()) {
        const auto restored_without_journal = write_snapshot(
            request.target_root, backup);
        if (!restored_without_journal.has_value()) {
            return Result<UpdateTransactionResult>::failure(
                {ErrorCode::io_failure,
                 L"Update failed, its recovery journal is unavailable, and "
                 L"rollback could not be completed: " +
                     restored_without_journal.error().message,
                 restored_without_journal.error().native_code});
        }
        return Result<UpdateTransactionResult>::failure(std::move(error));
    }
    journal.value().phase = JournalPhase::rollback_in_progress;
    auto journal_written = write_journal(
        journal_path(request.backup_root), journal.value());
    if (request.fault == UpdateFaultInjection::rollback_failure) {
        journal.value().phase = JournalPhase::rollback_failed;
        static_cast<void>(write_journal(
            journal_path(request.backup_root), journal.value()));
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Injected update rollback failure", 0});
    }
    const auto rolled_back = write_snapshot(request.target_root, backup);
    if (!rolled_back.has_value()) {
        journal.value().phase = JournalPhase::rollback_failed;
        static_cast<void>(write_journal(
            journal_path(request.backup_root), journal.value()));
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Update failed and rollback could not be completed: " +
                 rolled_back.error().message,
             rolled_back.error().native_code});
    }
    result.rolled_back = true;
    journal.value().phase = JournalPhase::rollback_verified;
    journal_written = write_journal(
        journal_path(request.backup_root), journal.value());
    if (!journal_written.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            journal_written.error());
    }
    return Result<UpdateTransactionResult>::failure(std::move(error));
}

}  // namespace

#if defined(KF2_UPDATE_TRANSACTION_TESTING)
void set_managed_read_hook_for_testing(ManagedReadHook hook) noexcept {
    managed_read_hook = hook;
}
#endif

Result<std::string> package_version(
    const std::filesystem::path& package_root) {
    const auto bytes = read_managed_file(
        package_root / L"Data" / L"package-manifest.json");
    if (!bytes.has_value()) return Result<std::string>::failure(bytes.error());
    return parse_package_version(bytes.value());
}

Result<UpdateTransactionResult> apply_update_transaction(
    const UpdateTransactionRequest& request) {
    if (request.target_root.empty() || request.staged_root.empty() ||
        request.backup_root.empty() || !request.target_root.is_absolute() ||
        !request.staged_root.is_absolute() || !request.backup_root.is_absolute() ||
        request.target_root == request.staged_root ||
        request.target_root == request.backup_root ||
        !normal_directory(request.target_root) ||
        !normal_directory(request.staged_root)) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::invalid_argument,
             L"Update transaction paths are invalid", 0});
    }
    std::error_code error;
    if (std::filesystem::exists(request.backup_root, error) || error) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::access_denied,
             L"Update backup directory must be new", 0});
    }

    const auto previous_version = package_version(request.target_root);
    const auto new_version = package_version(request.staged_root);
    if (!previous_version.has_value() || !new_version.has_value() ||
        new_version.value() != request.expected_new_version) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::access_denied,
             L"Staged package version does not match the approved update", 0});
    }
    const auto old_semver = parse_semantic_version(previous_version.value());
    const auto new_semver = parse_semantic_version(new_version.value());
    if (!old_semver.has_value() || !new_semver.has_value() ||
        compare_semantic_versions(new_semver.value(), old_semver.value()) <= 0) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::access_denied,
             L"Staged package is not newer than the installed package", 0});
    }

    const auto old_identity = security::package_source_identity(request.target_root);
    const auto new_identity = security::package_source_identity(request.staged_root);
    if (!old_identity.has_value() || !new_identity.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::access_denied, L"Package build identity is invalid", 0});
    }
    // Bind both packages to stable, complete bytes before any backup or target
    // write can begin.
    auto previous = capture_managed_package(
        request.target_root, old_identity.value(), previous_version.value());
    if (!previous.has_value()) {
        return Result<UpdateTransactionResult>::failure(previous.error());
    }
    auto staged = capture_managed_package(
        request.staged_root, new_identity.value(), new_version.value());
    if (!staged.has_value()) {
        return Result<UpdateTransactionResult>::failure(staged.error());
    }
    auto backup_written = write_snapshot(
        request.backup_root, previous.value());
    if (!backup_written.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            backup_written.error());
    }
    const auto target_hash = path_hash(request.target_root);
    const auto staged_hash = path_hash(request.staged_root);
    const auto backup_hash = path_hash(request.backup_root);
    const auto owner_start = process_start_id(GetCurrentProcess());
    if (!target_hash.has_value() || !staged_hash.has_value() ||
        !backup_hash.has_value() || owner_start == 0) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::platform_failure,
             L"Update recovery identity could not be created", GetLastError()});
    }
    UpdateJournal journal{
        .phase = JournalPhase::backup_ready,
        .target_path_hash = target_hash.value(),
        .staged_path_hash = staged_hash.value(),
        .backup_path_hash = backup_hash.value(),
        .previous_identity = old_identity.value(),
        .previous_version = previous_version.value(),
        .new_identity = new_identity.value(),
        .new_version = new_version.value(),
        .owner_process_id = GetCurrentProcessId(),
        .owner_process_start_id = owner_start,
    };
    auto journal_written = write_journal(
        journal_path(request.backup_root), journal);
    if (!journal_written.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            journal_written.error());
    }
    UpdateTransactionResult result{
        .previous_version = previous_version.value(),
        .installed_version = new_version.value()};
    const auto paths = managed_paths();
    journal.phase = JournalPhase::replacement_in_progress;
    journal_written = write_journal(journal_path(request.backup_root), journal);
    if (!journal_written.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            journal_written.error());
    }
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto replaced = platform::windows::atomic_replace_utf8(
            request.target_root / path_from_utf8(paths[index]),
            staged.value().files[index]);
        if (!replaced.has_value()) return fail_with_rollback(
            request, previous.value(), result, replaced.error());
        ++result.replaced_files;
        journal.replaced_files = result.replaced_files;
        journal_written = write_journal(
            journal_path(request.backup_root), journal);
        if (!journal_written.has_value()) return fail_with_rollback(
            request, previous.value(), result, journal_written.error());
        if (request.fault ==
                UpdateFaultInjection::interrupt_after_replacement &&
            request.fault_after_replacements == result.replaced_files) {
            return Result<UpdateTransactionResult>::failure(
                {ErrorCode::io_failure,
                 L"Injected abrupt update interruption", 0});
        }
        if ((request.fault == UpdateFaultInjection::after_first_replacement ||
             request.fault == UpdateFaultInjection::rollback_failure) &&
            result.replaced_files == 1) {
            return fail_with_rollback(
                request, previous.value(), result,
                {ErrorCode::io_failure,
                 L"Injected update replacement failure", 0});
        }
    }
    auto installed = capture_managed_package(
        request.target_root, new_identity.value(), new_version.value());
    if (!installed.has_value()) {
        return fail_with_rollback(
            request, previous.value(), result, installed.error());
    }
    if (installed.value().files != staged.value().files) {
        return fail_with_rollback(
            request, previous.value(), result,
            {ErrorCode::io_failure,
             L"Updated package failed final integrity verification", 0});
    }
    journal.phase = JournalPhase::update_verified;
    journal_written = write_journal(journal_path(request.backup_root), journal);
    if (!journal_written.has_value()) return fail_with_rollback(
        request, previous.value(), result, journal_written.error());
    if (request.fault == UpdateFaultInjection::interrupt_after_verification) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Injected interruption after update verification", 0});
    }
    return Result<UpdateTransactionResult>::success(std::move(result));
}

Result<bool> rollback_update_transaction(
    const std::filesystem::path& target_root,
    const std::filesystem::path& backup_root) {
    if (!target_root.is_absolute() || !backup_root.is_absolute() ||
        !normal_directory(target_root) || !normal_directory(backup_root)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Update rollback paths are invalid", 0});
    }
    const auto identity = security::package_source_identity(backup_root);
    const auto version = package_version(backup_root);
    if (!identity.has_value() || !version.has_value()) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Update rollback package identity is invalid", 0});
    }
    auto snapshot = capture_managed_package(
        backup_root, identity.value(), version.value());
    if (!snapshot.has_value()) return Result<bool>::failure(snapshot.error());
    const auto transaction_path = journal_path(backup_root);
    std::error_code exists_error;
    const bool has_journal = std::filesystem::exists(
        transaction_path, exists_error);
    if (exists_error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Update rollback journal cannot be inspected",
             static_cast<std::uint32_t>(exists_error.value())});
    }
    std::optional<UpdateJournal> journal;
    if (has_journal) {
        auto loaded = read_journal(transaction_path);
        const auto target_hash = path_hash(target_root);
        const auto backup_hash = path_hash(backup_root);
        if (!loaded.has_value() || !target_hash.has_value() ||
            !backup_hash.has_value() ||
            !equal_hash(loaded.value().target_path_hash,
                        target_hash.value()) ||
            !equal_hash(loaded.value().backup_path_hash,
                        backup_hash.value()) ||
            loaded.value().previous_identity != identity.value() ||
            loaded.value().previous_version != version.value()) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Update rollback journal identity is invalid", 0});
        }
        journal = std::move(loaded.value());
        journal->phase = JournalPhase::rollback_in_progress;
        static_cast<void>(write_journal(transaction_path, *journal));
    }
    const auto restored = write_snapshot(target_root, snapshot.value());
    if (!restored.has_value()) {
        if (journal) {
            journal->phase = JournalPhase::rollback_failed;
            static_cast<void>(write_journal(transaction_path, *journal));
        }
        return restored;
    }
    if (journal) {
        journal->phase = JournalPhase::rollback_verified;
        const auto written = write_journal(transaction_path, *journal);
        if (!written.has_value()) return written;
    }
    return Result<bool>::success(true);
}

Result<UpdateRecoveryResult> recover_update_transaction(
    const UpdateTransactionRequest& request) {
    if (request.target_root.empty() || request.staged_root.empty() ||
        request.backup_root.empty() ||
        !normal_directory(request.target_root) ||
        !normal_directory(request.staged_root)) {
        return Result<UpdateRecoveryResult>::failure(
            {ErrorCode::invalid_argument,
             L"Interrupted update recovery paths are invalid", 0});
    }
    auto journal = read_bound_journal(request);
    if (!journal.has_value()) {
        if (journal.error().code != ErrorCode::not_found) {
            return Result<UpdateRecoveryResult>::failure(journal.error());
        }
        std::error_code backup_error;
        const bool backup_exists = std::filesystem::exists(
            request.backup_root, backup_error);
        if (backup_error) {
            return Result<UpdateRecoveryResult>::failure(
                {ErrorCode::io_failure,
                 L"Update backup state cannot be inspected",
                 static_cast<std::uint32_t>(backup_error.value())});
        }
        if (!backup_exists) {
            return Result<UpdateRecoveryResult>::success(
                {UpdateRecoveryState::not_started, 0U});
        }
        if (!normal_directory(request.backup_root)) {
            return Result<UpdateRecoveryResult>::failure(
                {ErrorCode::access_denied,
                 L"Update backup has an unsafe identity", 0});
        }
        const auto previous_identity =
            security::package_source_identity(request.backup_root);
        const auto previous_version = package_version(request.backup_root);
        const auto new_identity =
            security::package_source_identity(request.staged_root);
        const auto new_version = package_version(request.staged_root);
        const auto target_hash = path_hash(request.target_root);
        const auto staged_hash = path_hash(request.staged_root);
        const auto backup_hash = path_hash(request.backup_root);
        const auto owner_start = process_start_id(GetCurrentProcess());
        if (!previous_identity.has_value() || !previous_version.has_value() ||
            !new_identity.has_value() || !new_version.has_value() ||
            new_version.value() != request.expected_new_version ||
            !target_hash.has_value() || !staged_hash.has_value() ||
            !backup_hash.has_value() || owner_start == 0) {
            const auto pre_update = target_is_verified_pre_update_package(
                request);
            if (pre_update.has_value() && pre_update.value()) {
                return Result<UpdateRecoveryResult>::success(
                    {UpdateRecoveryState::not_started, 0U});
            }
            return Result<UpdateRecoveryResult>::failure(
                {ErrorCode::stale_data,
                 L"Missing update journal cannot be reconstructed safely", 0});
        }
        UpdateJournal reconstructed{
            .phase = JournalPhase::rollback_in_progress,
            .target_path_hash = target_hash.value(),
            .staged_path_hash = staged_hash.value(),
            .backup_path_hash = backup_hash.value(),
            .previous_identity = previous_identity.value(),
            .previous_version = previous_version.value(),
            .new_identity = new_identity.value(),
            .new_version = new_version.value(),
            .owner_process_id = GetCurrentProcessId(),
            .owner_process_start_id = owner_start,
        };
        const auto reconstructed_written = write_journal(
            journal_path(request.backup_root), reconstructed);
        if (!reconstructed_written.has_value()) {
            return Result<UpdateRecoveryResult>::failure(
                reconstructed_written.error());
        }
        journal = Result<UpdateJournal>::success(std::move(reconstructed));
    }
    if (!normal_directory(request.backup_root)) {
        return Result<UpdateRecoveryResult>::failure(
            {ErrorCode::access_denied,
             L"Update backup has an unsafe identity", 0});
    }
    auto staged = capture_managed_package(
        request.staged_root, journal.value().new_identity,
        journal.value().new_version);
    auto backup = capture_managed_package(
        request.backup_root, journal.value().previous_identity,
        journal.value().previous_version);
    if (!staged.has_value()) {
        return Result<UpdateRecoveryResult>::failure(staged.error());
    }
    if (!backup.has_value()) {
        return Result<UpdateRecoveryResult>::failure(backup.error());
    }
    if (owner_is_active(journal.value())) {
        return Result<UpdateRecoveryResult>::success(
            {UpdateRecoveryState::owner_active,
             journal.value().replaced_files});
    }
    const auto verify_target = [&](std::string_view identity,
                                   std::string_view version)
        -> Result<ManagedPackageSnapshot> {
        return capture_managed_package(request.target_root, identity, version);
    };
    if (journal.value().phase == JournalPhase::handoff_ready ||
        journal.value().phase == JournalPhase::update_verified) {
        auto updated = verify_target(journal.value().new_identity,
                                     journal.value().new_version);
        if (updated.has_value() &&
            updated.value().files == staged.value().files) {
            return Result<UpdateRecoveryResult>::success(
                {UpdateRecoveryState::update_verified,
                 journal.value().replaced_files});
        }
    }
    if (journal.value().phase == JournalPhase::rollback_verified) {
        auto restored = verify_target(journal.value().previous_identity,
                                       journal.value().previous_version);
        if (restored.has_value() &&
            restored.value().files == backup.value().files) {
            return Result<UpdateRecoveryResult>::success(
                {UpdateRecoveryState::rollback_verified,
                 journal.value().replaced_files});
        }
    }
    auto already_restored = verify_target(journal.value().previous_identity,
                                          journal.value().previous_version);
    if (already_restored.has_value() &&
        already_restored.value().files == backup.value().files) {
        journal.value().phase = JournalPhase::rollback_verified;
        const auto written = write_journal(
            journal_path(request.backup_root), journal.value());
        if (!written.has_value()) {
            return Result<UpdateRecoveryResult>::failure(written.error());
        }
        return Result<UpdateRecoveryResult>::success(
            {UpdateRecoveryState::rollback_verified,
             journal.value().replaced_files});
    }
    journal.value().phase = JournalPhase::rollback_in_progress;
    auto written = write_journal(
        journal_path(request.backup_root), journal.value());
    const auto restored = write_snapshot(request.target_root, backup.value());
    if (!restored.has_value()) {
        journal.value().phase = JournalPhase::rollback_failed;
        static_cast<void>(write_journal(
            journal_path(request.backup_root), journal.value()));
        return Result<UpdateRecoveryResult>::failure(restored.error());
    }
    journal.value().phase = JournalPhase::rollback_verified;
    written = write_journal(journal_path(request.backup_root), journal.value());
    if (!written.has_value()) {
        return Result<UpdateRecoveryResult>::failure(written.error());
    }
    return Result<UpdateRecoveryResult>::success(
        {UpdateRecoveryState::rollback_verified,
         journal.value().replaced_files});
}

Result<bool> mark_update_transaction_handoff_ready(
    const UpdateTransactionRequest& request) {
    auto journal = read_bound_journal(request);
    if (!journal.has_value()) return Result<bool>::failure(journal.error());
    if (journal.value().phase == JournalPhase::handoff_ready) {
        return Result<bool>::success(true);
    }
    if (journal.value().phase != JournalPhase::update_verified) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Update transaction is not ready for handoff", 0});
    }
    auto updated = capture_managed_package(
        request.target_root, journal.value().new_identity,
        journal.value().new_version);
    auto staged = capture_managed_package(
        request.staged_root, journal.value().new_identity,
        journal.value().new_version);
    if (!updated.has_value()) return Result<bool>::failure(updated.error());
    if (!staged.has_value()) return Result<bool>::failure(staged.error());
    if (updated.value().files != staged.value().files) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Verified update no longer matches its staged package", 0});
    }
    journal.value().phase = JournalPhase::handoff_ready;
    return write_journal(journal_path(request.backup_root), journal.value());
}

Result<bool> update_transaction_allows_cleanup(
    const UpdateTransactionRequest& request) {
    auto journal = read_bound_journal(request);
    if (!journal.has_value()) {
        if (journal.error().code != ErrorCode::not_found ||
            !normal_directory(request.target_root) ||
            !normal_directory(request.staged_root)) {
            return Result<bool>::failure(journal.error());
        }
        std::error_code backup_error;
        const bool backup_exists = std::filesystem::exists(
            request.backup_root, backup_error);
        if (backup_error) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Update backup state cannot be inspected",
                 static_cast<std::uint32_t>(backup_error.value())});
        }
        if (!backup_exists) return Result<bool>::success(true);
        if (!normal_directory(request.backup_root)) {
            return Result<bool>::success(false);
        }
        return target_is_verified_pre_update_package(request);
    }
    const bool updated = journal.value().phase == JournalPhase::handoff_ready;
    const bool restored =
        journal.value().phase == JournalPhase::rollback_verified;
    if (!updated && !restored) return Result<bool>::success(false);
    auto package = capture_managed_package(
        request.target_root,
        updated ? journal.value().new_identity
                : journal.value().previous_identity,
        updated ? journal.value().new_version
                : journal.value().previous_version);
    if (!package.has_value()) return Result<bool>::failure(package.error());
    auto source = capture_managed_package(
        updated ? request.staged_root : request.backup_root,
        updated ? journal.value().new_identity
                : journal.value().previous_identity,
        updated ? journal.value().new_version
                : journal.value().previous_version);
    if (!source.has_value()) return Result<bool>::failure(source.error());
    if (package.value().files != source.value().files) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Update cleanup package no longer matches its verified source", 0});
    }
    return Result<bool>::success(true);
}

Result<UpdateOwnerIdentity> update_transaction_owner_identity(
    const UpdateTransactionRequest& request) {
    const auto journal = read_bound_journal(request);
    if (!journal.has_value()) {
        return Result<UpdateOwnerIdentity>::failure(journal.error());
    }
    return Result<UpdateOwnerIdentity>::success({
        journal.value().owner_process_id,
        journal.value().owner_process_start_id});
}

}  // namespace kf2::update
