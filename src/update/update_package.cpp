#include "kf2/update/update_package.hpp"

#include <Windows.h>
#include <Shellapi.h>
#include <ShlObj.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <string_view>

#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/update/github_release_client.hpp"
#include "kf2/update/update_transaction.hpp"

namespace kf2::update {
namespace {

constexpr std::uint64_t kMaximumArchiveBytes = 64ULL * 1024ULL * 1024ULL;

struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value != nullptr) WinHttpCloseHandle(value); }
};

struct FileHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct ComApartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

struct WorkRootCleanup {
    std::filesystem::path path;
    bool keep{false};
    ~WorkRootCleanup() {
        if (keep || path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

bool normal_directory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(
        platform::windows::extended_length_path(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool safe_hex_digest(std::string_view value) {
    return value.size() == 64U && std::ranges::all_of(value, [](char value) {
        return (value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F');
    });
}

std::wstring widen_ascii(std::string_view value) {
    return {value.begin(), value.end()};
}

bool allowed_final_url(std::wstring_view url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    if (!url.starts_with(L"https://") ||
        !WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0,
                         &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring_view host{components.lpszHostName,
                                 components.dwHostNameLength};
    return host == L"github.com" ||
        host == L"release-assets.githubusercontent.com" ||
        host == L"objects.githubusercontent.com";
}

Result<bool> validate_release_identity(const ReleaseInfo& release) {
    if (!release.asset || release.repository != official_release_repository() ||
        release.tag != "v" + release.version) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Update does not identify an official release package", 0});
    }
    const std::string expected_name =
        "KF2OptimizerNext-v" + release.version + "-win64.zip";
    const std::string expected_url = release.repository + "/releases/download/" +
        release.tag + "/" + expected_name;
    if (release.asset->file_name != expected_name ||
        release.asset->download_url != expected_url ||
        release.asset->size_bytes == 0 ||
        release.asset->size_bytes > kMaximumArchiveBytes ||
        !safe_hex_digest(release.asset->sha256)) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Update release asset identity is invalid", 0});
    }
    return Result<bool>::success(true);
}

Result<bool> create_new_work_root(const std::filesystem::path& root) {
    if (root.empty() || !root.is_absolute()) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Update working directory is invalid", 0});
    const auto native_root = platform::windows::extended_length_path(root);
    std::error_code error;
    if (std::filesystem::exists(native_root, error) || error ||
        !std::filesystem::create_directories(native_root, error) || error ||
        !normal_directory(native_root)) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Update working directory must be new and safe",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(true);
}

Result<std::wstring> final_url(HINTERNET request) {
    DWORD bytes = 0;
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &bytes) ==
            FALSE && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return Result<std::wstring>::failure(
            {ErrorCode::platform_failure,
             L"Update download address could not be verified", GetLastError()});
    }
    std::wstring url(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, url.data(), &bytes)) {
        return Result<std::wstring>::failure(
            {ErrorCode::platform_failure,
             L"Update download address could not be verified", GetLastError()});
    }
    while (!url.empty() && url.back() == L'\0') url.pop_back();
    if (!allowed_final_url(url)) return Result<std::wstring>::failure(
        {ErrorCode::access_denied,
         L"Update rejected an unexpected download redirect", 0});
    return Result<std::wstring>::success(std::move(url));
}

Result<bool> download(const ReleaseAsset& asset,
                      const std::filesystem::path& destination) {
    constexpr std::wstring_view prefix{L"https://github.com"};
    const auto url = widen_ascii(asset.download_url);
    if (!url.starts_with(prefix)) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Update download address is invalid", 0});
    InternetHandle session{WinHttpOpen(
        L"KF2OptimizerNext-Update/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return Result<bool>::failure(
        {ErrorCode::platform_failure,
         L"Update could not initialize the HTTPS client", GetLastError()});
    WinHttpSetTimeouts(session.value, 10'000, 10'000, 15'000, 30'000);
    InternetHandle connection{WinHttpConnect(
        session.value, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Update could not connect to GitHub",
         GetLastError()});
    const auto object = url.substr(prefix.size());
    InternetHandle request{WinHttpOpenRequest(
        connection.value, L"GET", std::wstring{object}.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH)};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    if (!request.value ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirect, sizeof(redirect)) ||
        !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"Update download failed", GetLastError()});
    }
    DWORD status = 0;
    DWORD status_bytes = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_bytes,
            WINHTTP_NO_HEADER_INDEX) || status != 200) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"GitHub did not return the update package",
             status});
    }
    const auto verified_url = final_url(request.value);
    if (!verified_url.has_value()) return Result<bool>::failure(
        verified_url.error());

    FileHandle file{CreateFileW(
        platform::windows::extended_length_path(destination).c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update temporary file could not be created",
         GetLastError()});
    std::array<std::byte, 64U * 1024U> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"Update download was interrupted",
                 GetLastError()});
        }
        if (read == 0) break;
        total += read;
        if (total > asset.size_bytes || total > kMaximumArchiveBytes) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Update download size does not match the release", 0});
        }
        DWORD written = 0;
        if (!WriteFile(file.value, buffer.data(), read, &written, nullptr) ||
            written != read) {
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"Update download could not be saved",
                 GetLastError()});
        }
    }
    if (total != asset.size_bytes || !FlushFileBuffers(file.value)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Update download is incomplete", GetLastError()});
    }
    return Result<bool>::success(true);
}

}  // namespace

Result<bool> extract_update_archive(const std::filesystem::path& archive,
                                   const std::filesystem::path& destination) {
    if (!archive.is_absolute() || !destination.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Update extraction paths are invalid", 0});
    }
    auto native_destination = destination.wstring().size() >= MAX_PATH
        ? platform::windows::extended_length_path(destination)
        : destination;
    auto native_archive = archive.wstring().size() >= MAX_PATH
        ? platform::windows::extended_length_path(archive)
        : archive;
    native_destination.make_preferred();
    native_archive.make_preferred();
    const DWORD attributes = GetFileAttributesW(native_archive.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Update archive is not a normal file", 0});
    }
    std::error_code error;
    if (!std::filesystem::create_directory(native_destination, error) ||
        error || !normal_directory(native_destination)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update extraction directory is invalid",
         static_cast<std::uint32_t>(error.value())});
    ComApartment apartment;
    if (FAILED(apartment.result)) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Windows ZIP support is unavailable",
         static_cast<std::uint32_t>(apartment.result)});
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    HRESULT result = CoCreateInstance(
        CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&operation));
    if (FAILED(result)) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Windows ZIP support could not start",
         static_cast<std::uint32_t>(result)});
    result = operation->SetOperationFlags(
        FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR |
        FOF_NOERRORUI | FOFX_EARLYFAILURE);
    Microsoft::WRL::ComPtr<IShellItem> source;
    Microsoft::WRL::ComPtr<IShellItem> target;
    if (FAILED(result) ||
        FAILED(result = SHCreateItemFromParsingName(native_archive.c_str(), nullptr,
                                                   IID_PPV_ARGS(&source))) ||
        FAILED(result = SHCreateItemFromParsingName(native_destination.c_str(), nullptr,
                                                   IID_PPV_ARGS(&target)))) {
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Update package is not a valid ZIP",
             static_cast<std::uint32_t>(result)});
    }
    Microsoft::WRL::ComPtr<IEnumShellItems> items;
    if (FAILED(source->BindToHandler(nullptr, BHID_EnumItems,
                                    IID_PPV_ARGS(&items)))) return Result<bool>::failure(
        {ErrorCode::access_denied, L"Update ZIP has no readable files", 0});
    bool queued = false;
    for (;;) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        result = items->Next(1, &item, nullptr);
        if (result == S_FALSE) break;
        if (FAILED(result) ||
            FAILED(result = operation->CopyItem(item.Get(), target.Get(),
                                                nullptr, nullptr))) {
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"Update ZIP cannot be queued",
                 static_cast<std::uint32_t>(result)});
        }
        queued = true;
    }
    if (!queued) return Result<bool>::failure(
        {ErrorCode::access_denied, L"Update ZIP has no readable files", 0});

    // PerformOperations is the completion boundary, including failure. Unlike
    // CopyHere, it leaves no background copy racing validation or cleanup.
    result = operation->PerformOperations();
    BOOL aborted = TRUE;
    const HRESULT abort_result = operation->GetAnyOperationsAborted(&aborted);
    if (SUCCEEDED(result) && FAILED(abort_result)) result = abort_result;
    if (SUCCEEDED(result) && aborted) result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (FAILED(result)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update ZIP extraction failed",
         static_cast<std::uint32_t>(result)});
    return Result<bool>::success(true);
}

Result<bool> verify_update_archive(const std::filesystem::path& archive,
                                   const ReleaseAsset& asset) {
    if (archive.empty() || !archive.is_absolute() ||
        asset.size_bytes == 0 || asset.size_bytes > kMaximumArchiveBytes ||
        !safe_hex_digest(asset.sha256)) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Update verification input is invalid", 0});
    const auto native_archive =
        platform::windows::extended_length_path(archive);
    const DWORD attributes = GetFileAttributesW(native_archive.c_str());
    std::error_code error;
    const auto size = std::filesystem::file_size(native_archive, error);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        error || size != asset.size_bytes) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Update package size does not match the release", 0});
    const auto hash = security::sha256_file_hex(native_archive);
    if (!hash.has_value() || !std::ranges::equal(
            hash.value(), asset.sha256, [](char left, char right) {
                return std::tolower(static_cast<unsigned char>(left)) ==
                    std::tolower(static_cast<unsigned char>(right));
            })) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Update package SHA-256 does not match the release", 0});
    return Result<bool>::success(true);
}

Result<bool> validate_staged_update_package(
    const std::filesystem::path& staged_root, const ReleaseInfo& release) {
    const auto identity = validate_release_identity(release);
    if (!identity.has_value()) return identity;
    if (!staged_root.is_absolute() || staged_root.filename() != L"KF2OptimizerNext" ||
        !normal_directory(staged_root)) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Extracted update package has an invalid root", 0});
    const auto version = package_version(staged_root);
    const auto source = security::package_source_identity(staged_root);
    if (!version.has_value() || version.value() != release.version ||
        !source.has_value()) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Extracted update package version or build identity is invalid", 0});
    const auto audit = security::audit_package_integrity(
        staged_root, source.value());
    if (!audit.has_value() || !audit.value().managed_package ||
        !audit.value().verified) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Extracted update package failed integrity verification", 0});
    return Result<bool>::success(true);
}

Result<PreparedUpdatePackage> prepare_update_package(
    const ReleaseInfo& release, const std::filesystem::path& new_work_root) {
    return prepare_update_package_with_operations(
        release, new_work_root,
        {.download = download, .extract = extract_update_archive});
}

Result<PreparedUpdatePackage> prepare_update_package_with_operations(
    const ReleaseInfo& release, const std::filesystem::path& new_work_root,
    const UpdatePackageOperations& operations) {
    const auto identity = validate_release_identity(release);
    if (!identity.has_value()) return Result<PreparedUpdatePackage>::failure(
        identity.error());
    const auto work_root = new_work_root.wstring().size() >= MAX_PATH
        ? platform::windows::extended_length_path(new_work_root)
        : new_work_root;
    const auto created = create_new_work_root(work_root);
    if (!created.has_value()) return Result<PreparedUpdatePackage>::failure(
        created.error());
    WorkRootCleanup cleanup{work_root};
    PreparedUpdatePackage result{
        .work_root = work_root,
        .archive_path = work_root / L"update.zip",
        .staged_root = work_root / L"extracted" / L"KF2OptimizerNext"};
    if (!operations.download || !operations.extract) {
        return Result<PreparedUpdatePackage>::failure(
            {ErrorCode::invalid_argument,
             L"Update package operations are incomplete", 0});
    }
    const auto downloaded = operations.download(
        *release.asset, result.archive_path);
    if (!downloaded.has_value()) return Result<PreparedUpdatePackage>::failure(
        downloaded.error());
    const auto verified = verify_update_archive(result.archive_path,
                                                 *release.asset);
    if (!verified.has_value()) return Result<PreparedUpdatePackage>::failure(
        verified.error());
    const auto extracted = operations.extract(
        result.archive_path, work_root / L"extracted");
    if (!extracted.has_value()) return Result<PreparedUpdatePackage>::failure(
        extracted.error());
    const auto staged = validate_staged_update_package(result.staged_root,
                                                       release);
    if (!staged.has_value()) return Result<PreparedUpdatePackage>::failure(
        staged.error());
    cleanup.keep = true;
    return Result<PreparedUpdatePackage>::success(std::move(result));
}

}  // namespace kf2::update
