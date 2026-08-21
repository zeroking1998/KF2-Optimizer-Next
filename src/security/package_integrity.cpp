#include "kf2/security/package_integrity.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <set>
#include <string>
#include <vector>

#include "kf2/security/sha256.hpp"

namespace kf2::security {
namespace {

constexpr std::array<std::string_view, 6> kPayloadPaths{
    "KF2Optimizer.exe",
    "Data/Lab/flexRelease_x64.forwarder-lab.dll",
    "Data/Lab/KF2OptimizerTelemetry.u",
    "Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md",
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

PackageIntegrityAudit invalid(std::wstring message) {
    return {.managed_package = true, .verified = false,
            .message = std::move(message)};
}

}  // namespace

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

    const auto document = read_manifest(manifest);
    if (!document.has_value()) {
        return Result<PackageIntegrityAudit>::success(
            invalid(document.error().message));
    }
    const auto lines = split_lines(document.value());
    if (lines.size() != 10 || lines[0] != "schema_version=1" ||
        lines[1] != "product=KF2OptimizerNext" ||
        !lines[2].starts_with("source_identity=") ||
        lines[3] != "file_count=6") {
        return Result<PackageIntegrityAudit>::success(
            invalid(L"Package integrity document has an incompatible schema"));
    }
    const auto source_identity = lines[2].substr(
        std::string_view{"source_identity="}.size());
    if (!safe_identity(source_identity) ||
        source_identity != expected_source_identity) {
        return Result<PackageIntegrityAudit>::success(
            invalid(L"Package files and executable have different build identities"));
    }

    std::set<std::string> seen;
    PackageIntegrityAudit audit{
        .managed_package = true,
        .verified = false,
        .source_identity = std::string{source_identity}};
    for (std::size_t index = 4; index < lines.size(); ++index) {
        if (!lines[index].starts_with("file=")) {
            return Result<PackageIntegrityAudit>::success(
                invalid(L"Package integrity entry is malformed"));
        }
        const auto entry = lines[index].substr(5);
        const auto separator = entry.find('|');
        if (separator == std::string_view::npos ||
            entry.find('|', separator + 1) != std::string_view::npos) {
            return Result<PackageIntegrityAudit>::success(
                invalid(L"Package integrity entry is malformed"));
        }
        const auto relative = entry.substr(0, separator);
        const auto expected_hash = entry.substr(separator + 1);
        if (std::ranges::find(kPayloadPaths, relative) == kPayloadPaths.end() ||
            !valid_hex(expected_hash) || !seen.emplace(relative).second) {
            return Result<PackageIntegrityAudit>::success(
                invalid(L"Package integrity entry is unknown or duplicated"));
        }
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
    if (seen.size() != kPayloadPaths.size()) {
        return Result<PackageIntegrityAudit>::success(
            invalid(L"Package integrity document is incomplete"));
    }
    audit.verified = true;
    audit.message = L"All managed package files match their SHA-256 values";
    return Result<PackageIntegrityAudit>::success(std::move(audit));
}

}  // namespace kf2::security
