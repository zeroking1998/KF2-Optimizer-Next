#include "kf2/update/update_transaction.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
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

std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(value.data()), value.size()}};
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

Result<UpdateTransactionResult> fail_with_rollback(
    const UpdateTransactionRequest& request,
    const ManagedPackageSnapshot& backup, UpdateTransactionResult result,
    Error error) {
    const auto rolled_back = write_snapshot(request.target_root, backup);
    if (!rolled_back.has_value()) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Update failed and rollback could not be completed: " +
                 rolled_back.error().message,
             rolled_back.error().native_code});
    }
    result.rolled_back = true;
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
    UpdateTransactionResult result{
        .previous_version = previous_version.value(),
        .installed_version = new_version.value()};
    const auto paths = managed_paths();
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto replaced = platform::windows::atomic_replace_utf8(
            request.target_root / path_from_utf8(paths[index]),
            staged.value().files[index]);
        if (!replaced.has_value()) return fail_with_rollback(
            request, previous.value(), result, replaced.error());
        ++result.replaced_files;
        if (request.fault == UpdateFaultInjection::after_first_replacement &&
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
    return write_snapshot(target_root, snapshot.value());
}

}  // namespace kf2::update
