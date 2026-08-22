#include "kf2/update/update_package.hpp"

#include <Windows.h>
#include <Shldisp.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <ranges>
#include <string_view>
#include <thread>

#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"
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

struct VariantOwner {
    VARIANT value{};
    VariantOwner() { VariantInit(&value); }
    ~VariantOwner() { VariantClear(&value); }
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
    const DWORD attributes = GetFileAttributesW(path.c_str());
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
    std::error_code error;
    if (std::filesystem::exists(root, error) || error ||
        !std::filesystem::create_directories(root, error) || error ||
        !normal_directory(root)) {
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
        destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
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

Result<bool> extract(const std::filesystem::path& archive,
                     const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error || !normal_directory(destination)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update extraction directory is invalid",
         static_cast<std::uint32_t>(error.value())});
    ComApartment apartment;
    if (FAILED(apartment.result)) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Windows ZIP support is unavailable",
         static_cast<std::uint32_t>(apartment.result)});
    IShellDispatch* raw = nullptr;
    const HRESULT created = CoCreateInstance(
        CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch,
        reinterpret_cast<void**>(&raw));
    if (FAILED(created) || raw == nullptr) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Windows ZIP support could not start",
         static_cast<std::uint32_t>(created)});
    Microsoft::WRL::ComPtr<IShellDispatch> shell;
    shell.Attach(raw);
    VariantOwner archive_value;
    archive_value.value.vt = VT_BSTR;
    archive_value.value.bstrVal = SysAllocString(archive.c_str());
    VariantOwner destination_value;
    destination_value.value.vt = VT_BSTR;
    destination_value.value.bstrVal = SysAllocString(destination.c_str());
    Folder* source_raw = nullptr;
    Folder* target_raw = nullptr;
    if (!archive_value.value.bstrVal || !destination_value.value.bstrVal ||
        FAILED(shell->NameSpace(archive_value.value, &source_raw)) ||
        !source_raw ||
        FAILED(shell->NameSpace(destination_value.value, &target_raw)) ||
        !target_raw) {
        if (source_raw) source_raw->Release();
        if (target_raw) target_raw->Release();
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Update package is not a valid ZIP", 0});
    }
    Microsoft::WRL::ComPtr<Folder> source;
    Microsoft::WRL::ComPtr<Folder> target;
    source.Attach(source_raw);
    target.Attach(target_raw);
    FolderItems* items_raw = nullptr;
    if (FAILED(source->Items(&items_raw)) || !items_raw) return Result<bool>::failure(
        {ErrorCode::access_denied, L"Update ZIP has no readable files", 0});
    Microsoft::WRL::ComPtr<FolderItems> items;
    items.Attach(items_raw);
    IDispatch* dispatch = nullptr;
    if (FAILED(items->QueryInterface(
            IID_IDispatch, reinterpret_cast<void**>(&dispatch))) || !dispatch) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure, L"Update ZIP cannot be enumerated", 0});
    }
    VariantOwner item_value;
    item_value.value.vt = VT_DISPATCH;
    item_value.value.pdispVal = dispatch;
    VariantOwner options;
    options.value.vt = VT_I4;
    options.value.lVal = 4 | 16 | 512 | 1024;
    const HRESULT copied = target->CopyHere(item_value.value, options.value);
    if (FAILED(copied)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update ZIP extraction failed",
         static_cast<std::uint32_t>(copied)});

    const auto expected = destination / L"KF2OptimizerNext" /
        L"Data" / L"package-integrity.ini";
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::is_regular_file(expected, error) && !error) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            return Result<bool>::success(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure, L"Update ZIP extraction timed out", WAIT_TIMEOUT});
}

}  // namespace

Result<bool> verify_update_archive(const std::filesystem::path& archive,
                                   const ReleaseAsset& asset) {
    if (archive.empty() || !archive.is_absolute() ||
        asset.size_bytes == 0 || asset.size_bytes > kMaximumArchiveBytes ||
        !safe_hex_digest(asset.sha256)) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Update verification input is invalid", 0});
    const DWORD attributes = GetFileAttributesW(archive.c_str());
    std::error_code error;
    const auto size = std::filesystem::file_size(archive, error);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        error || size != asset.size_bytes) return Result<bool>::failure(
        {ErrorCode::access_denied,
         L"Update package size does not match the release", 0});
    const auto hash = security::sha256_file_hex(archive);
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
        {.download = download, .extract = extract});
}

Result<PreparedUpdatePackage> prepare_update_package_with_operations(
    const ReleaseInfo& release, const std::filesystem::path& new_work_root,
    const UpdatePackageOperations& operations) {
    const auto identity = validate_release_identity(release);
    if (!identity.has_value()) return Result<PreparedUpdatePackage>::failure(
        identity.error());
    const auto created = create_new_work_root(new_work_root);
    if (!created.has_value()) return Result<PreparedUpdatePackage>::failure(
        created.error());
    WorkRootCleanup cleanup{new_work_root};
    PreparedUpdatePackage result{
        .work_root = new_work_root,
        .archive_path = new_work_root / L"update.zip",
        .staged_root = new_work_root / L"extracted" / L"KF2OptimizerNext"};
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
        result.archive_path, new_work_root / L"extracted");
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
