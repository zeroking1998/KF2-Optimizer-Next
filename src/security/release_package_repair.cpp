#include "kf2/security/release_package_repair.hpp"

#include <Windows.h>
#include <Shldisp.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>

#ifndef KF2_RELEASE_REPOSITORY
#define KF2_RELEASE_REPOSITORY ""
#endif

namespace kf2::security {
namespace {

constexpr std::uint64_t kMaximumArchiveBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::array<std::wstring_view, 15> kExtractedFiles{
    L"KF2Optimizer.exe",
    L"Data/package-integrity.ini",
    L"Data/Lab/flexRelease_x64.forwarder-lab.dll",
    L"Data/Lab/KF2OptimizerTelemetry.u",
    L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md",
    L"Data/Documentation/README.md",
    L"Data/Documentation/USER_GUIDE.md",
    L"Data/Documentation/UPDATES.md",
    L"Data/Documentation/FEATURE_REFERENCE.md",
    L"Data/Documentation/SAFETY.md",
    L"Data/Documentation/SUPPORT.md",
    L"Data/Documentation/LICENSE",
    L"Data/Documentation/THIRD_PARTY_NOTICES.md",
    L"Data/Documentation/issue72-feature-inventory.json",
    L"Data/Documentation/PresentMon-LICENSE.txt",
};

std::atomic<unsigned long> temporary_sequence{0};

struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() {
        if (value != nullptr) WinHttpCloseHandle(value);
    }
};

struct FileHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FileHandle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

struct VariantOwner {
    VARIANT value{};
    VariantOwner() { VariantInit(&value); }
    ~VariantOwner() { VariantClear(&value); }
};

struct ComApartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    ~ComApartment() {
        if (SUCCEEDED(result)) CoUninitialize();
    }
};

struct TemporaryRepairFiles {
    std::filesystem::path root;
    std::filesystem::path archive;
    std::filesystem::path extraction;
    ~TemporaryRepairFiles() {
        std::error_code ignored;
        if (!archive.empty()) std::filesystem::remove(archive, ignored);
        if (!extraction.empty()) {
            std::filesystem::remove_all(extraction, ignored);
        }
        if (!root.empty()) std::filesystem::remove(root, ignored);
    }
};

bool safe_version(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 64 &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '.' ||
                character == '-';
        });
}

bool safe_repository(std::string_view value) noexcept {
    constexpr std::string_view prefix{"https://github.com/"};
    if (!value.starts_with(prefix) || value.size() > 240 ||
        value.find_first_of("?#\\") != std::string_view::npos ||
        value.find("..") != std::string_view::npos) {
        return false;
    }
    const auto repository = value.substr(prefix.size());
    const auto separator = repository.find('/');
    return separator != std::string_view::npos && separator != 0 &&
        separator + 1 < repository.size() &&
        repository.find('/', separator + 1) == std::string_view::npos;
}

std::wstring widen_ascii(std::string_view value) {
    return {value.begin(), value.end()};
}

Result<bool> ensure_normal_directory(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Auto Repair working directory is invalid", 0});
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Auto Repair working directory could not be created",
             static_cast<std::uint32_t>(error.value())});
    }
    HANDLE directory = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair working directory cannot be inspected",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(directory, &information) &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const DWORD native = safe ? ERROR_SUCCESS : GetLastError();
    CloseHandle(directory);
    if (!safe) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair working directory has an unsafe identity", native});
    }
    return Result<bool>::success(true);
}

Result<std::filesystem::path> unique_path(
    const std::filesystem::path& root, std::wstring_view prefix,
    std::wstring_view suffix, bool directory) {
    for (unsigned int attempt = 0; attempt != 64; ++attempt) {
        const auto sequence = temporary_sequence.fetch_add(1) + 1;
        const auto candidate = root /
            (std::wstring{prefix} + std::to_wstring(GetCurrentProcessId()) +
             L"-" + std::to_wstring(sequence) + std::wstring{suffix});
        if (directory) {
            if (CreateDirectoryW(candidate.c_str(), nullptr) != FALSE) {
                return Result<std::filesystem::path>::success(candidate);
            }
        } else {
            HANDLE file = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0,
                                      nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_TEMPORARY |
                                          FILE_FLAG_WRITE_THROUGH,
                                      nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                CloseHandle(file);
                return Result<std::filesystem::path>::success(candidate);
            }
        }
        const DWORD native = GetLastError();
        if (native != ERROR_FILE_EXISTS && native != ERROR_ALREADY_EXISTS) {
            return Result<std::filesystem::path>::failure(
                {ErrorCode::io_failure,
                 L"Auto Repair temporary path could not be created", native});
        }
    }
    return Result<std::filesystem::path>::failure(
        {ErrorCode::io_failure,
         L"Auto Repair could not allocate a unique temporary path",
         ERROR_FILE_EXISTS});
}

bool allowed_final_url(std::wstring_view url) {
    if (!url.starts_with(L"https://")) return false;
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    if (WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0,
                        &components) == FALSE ||
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    const std::wstring_view host{components.lpszHostName,
                                 components.dwHostNameLength};
    return host == L"github.com" ||
        host == L"release-assets.githubusercontent.com" ||
        host == L"objects.githubusercontent.com";
}

Result<std::wstring> final_request_url(HINTERNET request) {
    DWORD bytes = 0;
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &bytes) ==
            FALSE &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return Result<std::wstring>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not verify the final download address",
             GetLastError()});
    }
    std::wstring url(bytes / sizeof(wchar_t), L'\0');
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, url.data(), &bytes) ==
        FALSE) {
        return Result<std::wstring>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not verify the final download address",
             GetLastError()});
    }
    while (!url.empty() && url.back() == L'\0') url.pop_back();
    if (!allowed_final_url(url)) {
        return Result<std::wstring>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair rejected an unexpected download redirect", 0});
    }
    return Result<std::wstring>::success(std::move(url));
}

Result<bool> download_archive(const ReleaseRepairPlan& plan,
                              const std::filesystem::path& destination) {
    InternetHandle session{WinHttpOpen(
        L"KF2OptimizerNext-AutoRepair/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.value == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not initialize the HTTPS client",
             GetLastError()});
    }
    WinHttpSetTimeouts(session.value, 10'000, 10'000, 15'000, 30'000);
    InternetHandle connection{
        WinHttpConnect(session.value, L"github.com",
                       INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (connection.value == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not connect to GitHub", GetLastError()});
    }
    const std::wstring prefix = L"https://github.com";
    if (!plan.url.starts_with(prefix)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Auto Repair release address is invalid", 0});
    }
    const auto object_name = plan.url.substr(prefix.size());
    InternetHandle request{WinHttpOpenRequest(
        connection.value, L"GET", object_name.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH)};
    if (request.value == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not create the GitHub request",
             GetLastError()});
    }
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    if (WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect_policy, sizeof(redirect_policy)) == FALSE ||
        WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
        WinHttpReceiveResponse(request.value, nullptr) == FALSE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Auto Repair could not download the matching GitHub release",
             GetLastError()});
    }
    DWORD status = 0;
    DWORD status_bytes = sizeof(status);
    if (WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_bytes,
            WINHTTP_NO_HEADER_INDEX) == FALSE || status != 200) {
        return Result<bool>::failure(
            {status == 404 ? ErrorCode::not_found : ErrorCode::io_failure,
             status == 404
                 ? L"GitHub does not provide a Windows package for this exact installed version"
                 : L"GitHub did not return the matching release package",
             status});
    }
    const auto final_url = final_request_url(request.value);
    if (!final_url.has_value()) {
        return Result<bool>::failure(final_url.error());
    }

    FileHandle file{CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                                TRUNCATE_EXISTING,
                                FILE_ATTRIBUTE_TEMPORARY |
                                    FILE_FLAG_WRITE_THROUGH,
                                nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Auto Repair temporary download could not be opened",
             GetLastError()});
    }
    std::array<std::byte, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD read = 0;
        if (WinHttpReadData(request.value, buffer.data(),
                            static_cast<DWORD>(buffer.size()), &read) == FALSE) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Auto Repair release download was interrupted",
                 GetLastError()});
        }
        if (read == 0) break;
        total += read;
        if (total > kMaximumArchiveBytes) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Auto Repair release archive exceeds the size limit", 0});
        }
        DWORD written = 0;
        if (WriteFile(file.value, buffer.data(), read, &written, nullptr) ==
                FALSE ||
            written != read) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Auto Repair temporary download could not be written",
                 GetLastError()});
        }
    }
    if (total == 0 || FlushFileBuffers(file.value) == FALSE) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Auto Repair downloaded an empty or incomplete archive",
             GetLastError()});
    }
    return Result<bool>::success(true);
}

Result<bool> extract_archive(const std::filesystem::path& archive,
                             const std::filesystem::path& destination) {
    ComApartment apartment;
    if (FAILED(apartment.result)) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not initialize Windows ZIP support",
             static_cast<std::uint32_t>(apartment.result)});
    }
    IShellDispatch* shell_raw = nullptr;
    const HRESULT created = CoCreateInstance(
        CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch,
        reinterpret_cast<void**>(&shell_raw));
    if (FAILED(created) || shell_raw == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not start Windows ZIP support",
             static_cast<std::uint32_t>(created)});
    }
    Microsoft::WRL::ComPtr<IShellDispatch> shell;
    shell.Attach(shell_raw);

    VariantOwner archive_variant;
    archive_variant.value.vt = VT_BSTR;
    archive_variant.value.bstrVal = SysAllocString(archive.c_str());
    VariantOwner destination_variant;
    destination_variant.value.vt = VT_BSTR;
    destination_variant.value.bstrVal = SysAllocString(destination.c_str());
    if (archive_variant.value.bstrVal == nullptr ||
        destination_variant.value.bstrVal == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::internal_failure,
             L"Auto Repair could not allocate ZIP paths", 0});
    }

    Folder* source_raw = nullptr;
    Folder* target_raw = nullptr;
    if (FAILED(shell->NameSpace(archive_variant.value, &source_raw)) ||
        source_raw == nullptr ||
        FAILED(shell->NameSpace(destination_variant.value, &target_raw)) ||
        target_raw == nullptr) {
        if (source_raw != nullptr) source_raw->Release();
        if (target_raw != nullptr) target_raw->Release();
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair downloaded an invalid ZIP package", 0});
    }
    Microsoft::WRL::ComPtr<Folder> source;
    Microsoft::WRL::ComPtr<Folder> target;
    source.Attach(source_raw);
    target.Attach(target_raw);
    FolderItems* items_raw = nullptr;
    if (FAILED(source->Items(&items_raw)) || items_raw == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair ZIP package contains no readable files", 0});
    }
    Microsoft::WRL::ComPtr<FolderItems> items;
    items.Attach(items_raw);
    IDispatch* items_dispatch = nullptr;
    if (FAILED(items->QueryInterface(IID_IDispatch,
                                     reinterpret_cast<void**>(&items_dispatch))) ||
        items_dispatch == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"Auto Repair could not enumerate the ZIP package", 0});
    }
    VariantOwner item_variant;
    item_variant.value.vt = VT_DISPATCH;
    item_variant.value.pdispVal = items_dispatch;
    VariantOwner option_variant;
    option_variant.value.vt = VT_I4;
    option_variant.value.lVal = 4 | 16 | 512 | 1024;
    const HRESULT copied = target->CopyHere(item_variant.value,
                                             option_variant.value);
    if (FAILED(copied)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Auto Repair could not extract the release package",
             static_cast<std::uint32_t>(copied)});
    }

    const auto package = destination / L"KF2OptimizerNext";
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
        bool complete = true;
        for (const auto relative : kExtractedFiles) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(package / relative, error) ||
                error) {
                complete = false;
                break;
            }
        }
        if (complete) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            return Result<bool>::success(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return Result<bool>::failure(
        {ErrorCode::io_failure,
         L"Auto Repair timed out while extracting the release package",
         WAIT_TIMEOUT});
}

}  // namespace

Result<ReleaseRepairPlan> exact_release_repair_plan(
    std::string_view installed_version) {
    constexpr std::string_view repository{KF2_RELEASE_REPOSITORY};
    if (!safe_version(installed_version) || !safe_repository(repository)) {
        return Result<ReleaseRepairPlan>::failure(
            {ErrorCode::invalid_argument,
             L"Installed build cannot be mapped to an exact official GitHub release",
             0});
    }
    const auto version = widen_ascii(installed_version);
    ReleaseRepairPlan plan;
    plan.tag = L"v" + version;
    plan.asset_name =
        L"KF2OptimizerNext-v" + version + L"-win64.zip";
    plan.url = widen_ascii(repository) + L"/releases/download/" +
        plan.tag + L"/" + plan.asset_name;
    return Result<ReleaseRepairPlan>::success(std::move(plan));
}

Result<PackageRepairResult> download_and_repair_release_package(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity) {
    const auto plan = exact_release_repair_plan(installed_version);
    if (!plan.has_value()) {
        return Result<PackageRepairResult>::failure(plan.error());
    }
    const auto prepared = ensure_normal_directory(working_directory);
    if (!prepared.has_value()) {
        return Result<PackageRepairResult>::failure(prepared.error());
    }
    TemporaryRepairFiles temporary;
    temporary.root = working_directory;
    const auto archive = unique_path(working_directory, L"release-", L".zip",
                                     false);
    if (!archive.has_value()) {
        return Result<PackageRepairResult>::failure(archive.error());
    }
    temporary.archive = archive.value();
    const auto extraction = unique_path(working_directory, L"extract-", L"",
                                        true);
    if (!extraction.has_value()) {
        return Result<PackageRepairResult>::failure(extraction.error());
    }
    temporary.extraction = extraction.value();
    const auto downloaded = download_archive(plan.value(), temporary.archive);
    if (!downloaded.has_value()) {
        return Result<PackageRepairResult>::failure(downloaded.error());
    }
    const auto extracted = extract_archive(temporary.archive,
                                           temporary.extraction);
    if (!extracted.has_value()) {
        return Result<PackageRepairResult>::failure(extracted.error());
    }
    return repair_package_from_directory(
        executable_directory,
        temporary.extraction / L"KF2OptimizerNext",
        expected_source_identity);
}

}  // namespace kf2::security
