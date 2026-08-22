#include "kf2/update/update_transaction.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/package_integrity.hpp"
#include "kf2/update/semantic_version.hpp"

namespace kf2::update {
namespace {

constexpr std::uintmax_t kMaximumManagedFileBytes = 32U * 1024U * 1024U;
constexpr std::array<std::string_view, 2> kManifestPaths{
    "Data/package-integrity.ini", "Data/package-manifest.json"};

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
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

Result<std::string> read_managed_file(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"A managed update file has an unsafe identity", GetLastError()});
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumManagedFileBytes) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"A managed update file has an invalid size",
             static_cast<std::uint32_t>(error.value())});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"A managed update file cannot be read", 0});
    return Result<std::string>::success(
        {std::istreambuf_iterator<char>{input},
         std::istreambuf_iterator<char>{}});
}

Result<bool> ensure_parent(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error || !normal_directory(path)) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"An update directory has an unsafe identity",
         static_cast<std::uint32_t>(error.value())});
    return Result<bool>::success(true);
}

Result<bool> copy_to_backup(const std::filesystem::path& source,
                            const std::filesystem::path& destination) {
    const auto bytes = read_managed_file(source);
    if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
    const auto parent = ensure_parent(destination.parent_path());
    if (!parent.has_value()) return parent;
    return platform::windows::atomic_replace_utf8(destination, bytes.value());
}

Result<bool> restore_backup(const std::filesystem::path& target_root,
                            const std::filesystem::path& backup_root) {
    for (const auto relative : managed_paths()) {
        const auto source = backup_root / path_from_utf8(relative);
        const auto target = target_root / path_from_utf8(relative);
        const auto bytes = read_managed_file(source);
        if (!bytes.has_value()) return Result<bool>::failure(bytes.error());
        const auto parent = ensure_parent(target.parent_path());
        if (!parent.has_value()) return parent;
        const auto restored = platform::windows::atomic_replace_utf8(
            target, bytes.value());
        if (!restored.has_value()) return restored;
    }
    const auto identity = security::package_source_identity(backup_root);
    if (!identity.has_value()) return Result<bool>::failure(identity.error());
    const auto audit = security::audit_package_integrity(
        target_root, identity.value());
    if (!audit.has_value() || !audit.value().managed_package ||
        !audit.value().verified) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"The previous package could not be restored completely", 0});
    }
    return Result<bool>::success(true);
}

Result<UpdateTransactionResult> fail_with_rollback(
    const UpdateTransactionRequest& request, UpdateTransactionResult result,
    Error error) {
    const auto rolled_back = restore_backup(
        request.target_root, request.backup_root);
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

Result<std::string> package_version(
    const std::filesystem::path& package_root) {
    const auto bytes = read_managed_file(
        package_root / L"Data" / L"package-manifest.json");
    if (!bytes.has_value() || bytes.value().size() > 256U * 1024U) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package version manifest is invalid", 0});
    }
    constexpr std::string_view key{"\"package_version\""};
    const auto first = bytes.value().find(key);
    if (first == std::string::npos ||
        bytes.value().find(key, first + key.size()) != std::string::npos) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package version is missing or duplicated", 0});
    }
    auto offset = first + key.size();
    while (offset < bytes.value().size() && std::isspace(
               static_cast<unsigned char>(bytes.value()[offset]))) ++offset;
    if (offset >= bytes.value().size() || bytes.value()[offset++] != ':') {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    }
    while (offset < bytes.value().size() && std::isspace(
               static_cast<unsigned char>(bytes.value()[offset]))) ++offset;
    if (offset >= bytes.value().size() || bytes.value()[offset++] != '"') {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    }
    const auto end = bytes.value().find('"', offset);
    if (end == std::string::npos) return Result<std::string>::failure(
        {ErrorCode::invalid_argument, L"Package version is malformed", 0});
    const std::string version = bytes.value().substr(offset, end - offset);
    if (!parse_semantic_version(version).has_value()) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package version is invalid", 0});
    }
    return Result<std::string>::success(version);
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
    std::filesystem::create_directories(request.backup_root, error);
    if (error || !normal_directory(request.backup_root)) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Update backup directory could not be created",
             static_cast<std::uint32_t>(error.value())});
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
    const auto old_audit = security::audit_package_integrity(
        request.target_root, old_identity.value());
    const auto new_audit = security::audit_package_integrity(
        request.staged_root, new_identity.value());
    if (!old_audit.has_value() || !new_audit.has_value() ||
        !old_audit.value().managed_package || !old_audit.value().verified ||
        !new_audit.value().managed_package || !new_audit.value().verified) {
        return Result<UpdateTransactionResult>::failure(
            {ErrorCode::access_denied,
             L"Installed or staged package failed integrity verification", 0});
    }

    for (const auto relative : managed_paths()) {
        const auto backup = copy_to_backup(
            request.target_root / path_from_utf8(relative),
            request.backup_root / path_from_utf8(relative));
        if (!backup.has_value()) return Result<UpdateTransactionResult>::failure(
            {ErrorCode::io_failure,
             L"Current program version could not be backed up", 0});
    }

    UpdateTransactionResult result{
        .previous_version = previous_version.value(),
        .installed_version = new_version.value()};
    for (const auto relative : managed_paths()) {
        const auto source = read_managed_file(
            request.staged_root / path_from_utf8(relative));
        if (!source.has_value()) return fail_with_rollback(
            request, result, source.error());
        const auto replaced = platform::windows::atomic_replace_utf8(
            request.target_root / path_from_utf8(relative), source.value());
        if (!replaced.has_value()) return fail_with_rollback(
            request, result, replaced.error());
        ++result.replaced_files;
        if (request.fault == UpdateFaultInjection::after_first_replacement &&
            result.replaced_files == 1) {
            return fail_with_rollback(
                request, result,
                {ErrorCode::io_failure,
                 L"Injected update replacement failure", 0});
        }
    }
    const auto final_version = package_version(request.target_root);
    const auto final_audit = security::audit_package_integrity(
        request.target_root, new_identity.value());
    if (!final_audit.has_value() || !final_audit.value().managed_package ||
        !final_audit.value().verified ||
        !final_version.has_value() ||
        final_version.value() != new_version.value()) {
        return fail_with_rollback(
            request, result,
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
    return restore_backup(target_root, backup_root);
}

}  // namespace kf2::update
