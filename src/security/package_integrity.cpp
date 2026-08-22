#include "kf2/security/package_integrity.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <set>
#include <string>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::security {
namespace {

constexpr std::array<std::string_view, 14> kPayloadPaths{
    "KF2Optimizer.exe",
    "Data/Lab/flexRelease_x64.forwarder-lab.dll",
    "Data/Lab/KF2OptimizerTelemetry.u",
    "Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md",
    "Data/Documentation/README.md",
    "Data/Documentation/USER_GUIDE.md",
    "Data/Documentation/UPDATES.md",
    "Data/Documentation/FEATURE_REFERENCE.md",
    "Data/Documentation/SAFETY.md",
    "Data/Documentation/SUPPORT.md",
    "Data/Documentation/LICENSE",
    "Data/Documentation/THIRD_PARTY_NOTICES.md",
    "Data/Documentation/issue72-feature-inventory.json",
    "Data/Documentation/PresentMon-LICENSE.txt",
};

bool safe_identity(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 128 &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '.' ||
                character == '_' || character == '-';
        });
}

bool valid_hex(std::string_view value) noexcept {
    return value.size() == 64 &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

bool equal_ascii_case_insensitive(std::string_view left,
                                  std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

Result<std::string> read_manifest(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"Package integrity document cannot be opened", GetLastError()});
    }
    struct CloseFile {
        HANDLE value;
        ~CloseFile() { CloseHandle(value); }
    } close_file{file};
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 || information.nFileSizeHigh != 0 ||
        information.nFileSizeLow > 64 * 1024) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Package integrity document has an unsafe file identity", 0});
    }
    std::string bytes(information.nFileSizeLow, '\0');
    DWORD read = 0;
    if (!bytes.empty() &&
        (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                   &read, nullptr) || read != bytes.size())) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure,
             L"Package integrity document could not be read completely",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(file, &after) ||
        after.nFileIndexHigh != information.nFileIndexHigh ||
        after.nFileIndexLow != information.nFileIndexLow ||
        after.nFileSizeHigh != information.nFileSizeHigh ||
        after.nFileSizeLow != information.nFileSizeLow ||
        after.ftLastWriteTime.dwHighDateTime !=
            information.ftLastWriteTime.dwHighDateTime ||
        after.ftLastWriteTime.dwLowDateTime !=
            information.ftLastWriteTime.dwLowDateTime) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Package integrity document changed while being read", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

std::vector<std::string_view> split_lines(std::string_view document) {
    std::vector<std::string_view> result;
    std::size_t offset = 0;
    while (offset < document.size()) {
        const auto newline = document.find('\n', offset);
        auto line = document.substr(
            offset, newline == std::string_view::npos
                ? document.size() - offset : newline - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        result.push_back(line);
        if (newline == std::string_view::npos) break;
        offset = newline + 1;
    }
    return result;
}

struct ParsedManifest {
    std::string document;
    std::string source_identity;
    std::array<std::string, kPayloadPaths.size()> hashes;
};

Result<ParsedManifest> parse_manifest(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity) {
    const auto manifest =
        executable_directory / L"Data" / L"package-integrity.ini";
    const auto document = read_manifest(manifest);
    if (!document.has_value()) {
        return Result<ParsedManifest>::failure(document.error());
    }
    const auto lines = split_lines(document.value());
    const std::string expected_file_count =
        "file_count=" + std::to_string(kPayloadPaths.size());
    if (lines.size() != kPayloadPaths.size() + 4 ||
        lines[0] != "schema_version=1" ||
        lines[1] != "product=KF2OptimizerNext" ||
        !lines[2].starts_with("source_identity=") ||
        lines[3] != expected_file_count) {
        return Result<ParsedManifest>::failure(
            {ErrorCode::access_denied,
             L"Package integrity document has an incompatible schema", 0});
    }
    const auto source_identity = lines[2].substr(
        std::string_view{"source_identity="}.size());
    if (!safe_identity(source_identity) ||
        source_identity != expected_source_identity) {
        return Result<ParsedManifest>::failure(
            {ErrorCode::access_denied,
             L"Package files and executable have different build identities", 0});
    }

    ParsedManifest parsed{
        .document = document.value(),
        .source_identity = std::string{source_identity}};
    std::set<std::string> seen;
    for (std::size_t index = 4; index < lines.size(); ++index) {
        if (!lines[index].starts_with("file=")) {
            return Result<ParsedManifest>::failure(
                {ErrorCode::access_denied,
                 L"Package integrity entry is malformed", 0});
        }
        const auto entry = lines[index].substr(5);
        const auto separator = entry.find('|');
        if (separator == std::string_view::npos ||
            entry.find('|', separator + 1) != std::string_view::npos) {
            return Result<ParsedManifest>::failure(
                {ErrorCode::access_denied,
                 L"Package integrity entry is malformed", 0});
        }
        const auto relative = entry.substr(0, separator);
        const auto expected_hash = entry.substr(separator + 1);
        const auto known = std::ranges::find(kPayloadPaths, relative);
        if (known == kPayloadPaths.end() || !valid_hex(expected_hash) ||
            !seen.emplace(relative).second) {
            return Result<ParsedManifest>::failure(
                {ErrorCode::access_denied,
                 L"Package integrity entry is unknown or duplicated", 0});
        }
        parsed.hashes[static_cast<std::size_t>(
            std::distance(kPayloadPaths.begin(), known))] = expected_hash;
    }
    if (seen.size() != kPayloadPaths.size()) {
        return Result<ParsedManifest>::failure(
            {ErrorCode::access_denied,
             L"Package integrity document is incomplete", 0});
    }
    return Result<ParsedManifest>::success(std::move(parsed));
}

Result<std::string> read_repair_source(
    const std::filesystem::path& path) {
    constexpr std::uint64_t kMaximumBytes = 64ULL * 1024ULL * 1024ULL;
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found,
             L"A required file is missing from the selected package",
             GetLastError()});
    }
    struct CloseFile {
        HANDLE value;
        ~CloseFile() { CloseHandle(value); }
    } close_file{file};
    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileInformationByHandle(file, &before) ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        before.nNumberOfLinks != 1) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"A selected package file has an unsafe identity", 0});
    }
    const std::uint64_t size =
        (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32U) |
        before.nFileSizeLow;
    if (size > kMaximumBytes || size >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"A selected package file exceeds the repair size limit", 0});
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + total, requested, &read, nullptr) ||
            read == 0) {
            return Result<std::string>::failure(
                {ErrorCode::io_failure,
                 L"A selected package file could not be read completely",
                 GetLastError()});
        }
        total += read;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(file, &after) ||
        after.nFileIndexHigh != before.nFileIndexHigh ||
        after.nFileIndexLow != before.nFileIndexLow ||
        after.nFileSizeHigh != before.nFileSizeHigh ||
        after.nFileSizeLow != before.nFileSizeLow ||
        after.ftLastWriteTime.dwHighDateTime !=
            before.ftLastWriteTime.dwHighDateTime ||
        after.ftLastWriteTime.dwLowDateTime !=
            before.ftLastWriteTime.dwLowDateTime) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"A selected package file changed while being imported", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

Result<bool> ensure_repair_parent(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"A required package directory could not be created",
             static_cast<std::uint32_t>(error.value())});
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"A required package directory has an unsafe identity",
             GetLastError()});
    }
    return Result<bool>::success(true);
}

PackageIntegrityAudit invalid(std::wstring message) {
    return {.managed_package = true, .verified = false,
            .message = std::move(message)};
}

}  // namespace

std::span<const std::string_view> managed_package_payload_paths() noexcept {
    return kPayloadPaths;
}

Result<std::string> package_source_identity(
    const std::filesystem::path& executable_directory) {
    if (executable_directory.empty() || !executable_directory.is_absolute()) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Package root is invalid", 0});
    }
    const auto document = read_manifest(
        executable_directory / L"Data" / L"package-integrity.ini");
    if (!document.has_value()) return Result<std::string>::failure(document.error());
    constexpr std::string_view key{"source_identity="};
    const auto begin = document.value().find(key);
    if (begin == std::string::npos ||
        (begin != 0 && document.value()[begin - 1] != '\n')) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package source identity is missing", 0});
    }
    const auto value_begin = begin + key.size();
    const auto end = document.value().find('\n', value_begin);
    const std::string identity = document.value().substr(
        value_begin, end == std::string::npos
                         ? std::string::npos : end - value_begin);
    if (!safe_identity(identity)) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Package source identity is invalid", 0});
    }
    return Result<std::string>::success(identity);
}

Result<PackageIntegrityAudit> audit_package_integrity(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity) {
    if (executable_directory.empty() ||
        !safe_identity(expected_source_identity)) {
        return Result<PackageIntegrityAudit>::failure(
            {ErrorCode::invalid_argument,
             L"Package integrity audit request is invalid", 0});
    }
    const auto manifest =
        executable_directory / L"Data" / L"package-integrity.ini";
    const DWORD attributes = GetFileAttributesW(manifest.c_str());
    const DWORD attribute_error = attributes == INVALID_FILE_ATTRIBUTES
        ? GetLastError() : ERROR_SUCCESS;
    if (attributes == INVALID_FILE_ATTRIBUTES &&
        (attribute_error == ERROR_FILE_NOT_FOUND ||
         attribute_error == ERROR_PATH_NOT_FOUND)) {
        return Result<PackageIntegrityAudit>::success({
            .managed_package = false,
            .verified = true,
            .message = L"Development build without a package integrity document"});
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return Result<PackageIntegrityAudit>::success(
            invalid(L"Package integrity document cannot be inspected"));
    }

    const auto parsed = parse_manifest(
        executable_directory, expected_source_identity);
    if (!parsed.has_value()) {
        return Result<PackageIntegrityAudit>::success(
            invalid(parsed.error().message));
    }
    PackageIntegrityAudit audit{
        .managed_package = true,
        .verified = false,
        .source_identity = parsed.value().source_identity};
    for (std::size_t index = 0; index < kPayloadPaths.size(); ++index) {
        const auto relative = kPayloadPaths[index];
        const auto& expected_hash = parsed.value().hashes[index];
        const std::filesystem::path path = executable_directory /
            std::filesystem::path{std::u8string{
                reinterpret_cast<const char8_t*>(relative.data()),
                relative.size()}};
        const auto actual_hash = sha256_file_hex(path);
        if (!actual_hash.has_value() ||
            !equal_ascii_case_insensitive(actual_hash.value(), expected_hash)) {
            return Result<PackageIntegrityAudit>::success(
                invalid(L"A managed package file is missing, unsafe or damaged"));
        }
        ++audit.verified_files;
    }
    audit.verified = true;
    audit.message = L"All managed package files match their SHA-256 values";
    return Result<PackageIntegrityAudit>::success(std::move(audit));
}

Result<PackageRepairResult> repair_package_from_directory(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& source_directory,
    std::string_view expected_source_identity) {
    if (executable_directory.empty() || source_directory.empty() ||
        !executable_directory.is_absolute() || !source_directory.is_absolute() ||
        !safe_identity(expected_source_identity)) {
        return Result<PackageRepairResult>::failure(
            {ErrorCode::invalid_argument,
             L"Package repair request is invalid", 0});
    }
    const auto source = parse_manifest(
        source_directory, expected_source_identity);
    if (!source.has_value()) {
        return Result<PackageRepairResult>::failure({
            source.error().code,
            L"The selected folder is not a complete matching package: " +
                source.error().message,
            source.error().native_code});
    }

    PackageRepairResult result;
    std::array<std::string, kPayloadPaths.size()> source_files;
    for (std::size_t index = 0; index < kPayloadPaths.size(); ++index) {
        const auto relative = kPayloadPaths[index];
        const auto relative_path = std::filesystem::path{std::u8string{
            reinterpret_cast<const char8_t*>(relative.data()),
            relative.size()}};
        const auto source_bytes = read_repair_source(
            source_directory / relative_path);
        if (!source_bytes.has_value()) {
            return Result<PackageRepairResult>::failure(source_bytes.error());
        }
        const auto source_hash = sha256_hex(source_bytes.value());
        if (!source_hash.has_value() ||
            !equal_ascii_case_insensitive(
                source_hash.value(), source.value().hashes[index])) {
            return Result<PackageRepairResult>::failure(
                {ErrorCode::access_denied,
                 L"A selected package file does not match its SHA-256 value",
                 0});
        }
        source_files[index] = source_bytes.value();
    }

    for (std::size_t index = 0; index < kPayloadPaths.size(); ++index) {
        const auto relative = kPayloadPaths[index];
        const auto relative_path = std::filesystem::path{std::u8string{
            reinterpret_cast<const char8_t*>(relative.data()),
            relative.size()}};
        const auto target = executable_directory / relative_path;
        const auto current_hash = sha256_file_hex(target);
        if (current_hash.has_value() && equal_ascii_case_insensitive(
                current_hash.value(), source.value().hashes[index])) {
            ++result.already_valid_files;
            continue;
        }
        if (relative == std::string_view{"KF2Optimizer.exe"}) {
            return Result<PackageRepairResult>::failure(
                {ErrorCode::access_denied,
                 L"The running executable does not match the selected package. "
                 L"Extract the complete package into a new folder instead.",
                 0});
        }
        const auto parent = ensure_repair_parent(target.parent_path());
        if (!parent.has_value()) {
            return Result<PackageRepairResult>::failure(parent.error());
        }
        const auto written = platform::windows::atomic_replace_utf8(
            target, source_files[index]);
        if (!written.has_value()) {
            return Result<PackageRepairResult>::failure(written.error());
        }
        ++result.repaired_files;
    }

    const auto target_manifest = executable_directory / L"Data" /
        L"package-integrity.ini";
    const auto current_manifest = read_manifest(target_manifest);
    if (!current_manifest.has_value() ||
        current_manifest.value() != source.value().document) {
        const auto parent = ensure_repair_parent(target_manifest.parent_path());
        if (!parent.has_value()) {
            return Result<PackageRepairResult>::failure(parent.error());
        }
        const auto written = platform::windows::atomic_replace_utf8(
            target_manifest, source.value().document);
        if (!written.has_value()) {
            return Result<PackageRepairResult>::failure(written.error());
        }
        ++result.repaired_files;
    }

    const auto verified = audit_package_integrity(
        executable_directory, expected_source_identity);
    if (!verified.has_value() || !verified.value().managed_package ||
        !verified.value().verified) {
        return Result<PackageRepairResult>::failure(
            {ErrorCode::io_failure,
             L"The repaired package did not pass final integrity verification",
             0});
    }
    result.restart_required = result.repaired_files != 0;
    return Result<PackageRepairResult>::success(result);
}

}  // namespace kf2::security
