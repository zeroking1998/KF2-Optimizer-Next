#include "kf2/update/update_helper.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <thread>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/update/semantic_version.hpp"
#include "kf2/update/update_transaction.hpp"

namespace kf2::update {
namespace {

struct HelperRequest {
    std::uint32_t parent_process_id{};
    std::filesystem::path target_root;
    std::filesystem::path staged_root;
    std::filesystem::path backup_root;
    std::filesystem::path receipt_path;
    std::string expected_version;
    std::string token;
};

struct DirectoryIdentity {
    DWORD volume_serial{};
    DWORD file_index_high{};
    DWORD file_index_low{};

    bool operator==(const DirectoryIdentity&) const = default;
};

UpdateTransactionRequest transaction_request(
    const HelperRequest& request) {
    return {
        .target_root = request.target_root,
        .staged_root = request.staged_root,
        .backup_root = request.backup_root,
        .expected_new_version = request.expected_version,
    };
}

std::optional<std::string> utf8_from_wide(std::wstring_view value) {
    if (value.empty()) return std::string{};
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), result.data(),
                             size, nullptr, nullptr)) return std::nullopt;
    return result;
}

std::optional<std::wstring> wide_from_utf8(std::string_view value) {
    if (value.empty()) return std::wstring{};
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), result.data(),
                             size)) return std::nullopt;
    return result;
}

bool safe_token(std::string_view value) {
    return value.size() >= 16U && value.size() <= 64U &&
        std::ranges::all_of(value, [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

bool normal_directory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool normal_file(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

std::filesystem::path normalized(const std::filesystem::path& path) {
    std::error_code error;
    const auto result = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::path{} : result;
}

bool child_of(const std::filesystem::path& child,
              const std::filesystem::path& parent) {
    const auto child_value = normalized(child).native();
    auto parent_value = normalized(parent).native();
    if (child_value.empty() || parent_value.empty()) return false;
    if (!parent_value.ends_with(L'\\')) parent_value.push_back(L'\\');
    return child_value.size() > parent_value.size() &&
        _wcsnicmp(child_value.c_str(), parent_value.c_str(),
                  parent_value.size()) == 0;
}

bool same_path(const std::filesystem::path& left,
               const std::filesystem::path& right) {
    const auto left_value = normalized(left).native();
    const auto right_value = normalized(right).native();
    return !left_value.empty() && left_value.size() == right_value.size() &&
        _wcsnicmp(left_value.c_str(), right_value.c_str(),
                  left_value.size()) == 0;
}

bool has_dot_component(const std::filesystem::path& path) {
    return std::ranges::any_of(path, [](const auto& component) {
        return component == L"." || component == L"..";
    });
}

std::optional<std::pair<std::uint32_t, std::uint64_t>> work_identity(
    const std::filesystem::path& work) {
    const auto name = work.filename().wstring();
    const auto separator = name.find(L'-');
    if (separator == std::wstring::npos || separator == 0 ||
        separator + 1U >= name.size() ||
        name.find(L'-', separator + 1U) != std::wstring::npos) return std::nullopt;
    const auto parse_decimal = [](std::wstring_view value)
        -> std::optional<std::uint64_t> {
        std::uint64_t result = 0;
        for (const wchar_t character : value) {
            if (character < L'0' || character > L'9') return std::nullopt;
            const auto digit = static_cast<std::uint64_t>(character - L'0');
            if (result > (UINT64_MAX - digit) / 10U) return std::nullopt;
            result = result * 10U + digit;
        }
        return result == 0 ? std::nullopt
                           : std::optional<std::uint64_t>{result};
    };
    const auto process = parse_decimal(
        std::wstring_view{name}.substr(0, separator));
    const auto nonce = parse_decimal(
        std::wstring_view{name}.substr(separator + 1U));
    if (!process || *process > UINT32_MAX || !nonce) return std::nullopt;
    return std::pair{static_cast<std::uint32_t>(*process), *nonce};
}

bool canonical_update_work_root(const std::filesystem::path& work,
                                std::uint32_t expected_process_id) {
    const auto temporary = platform::windows::temporary_directory();
    if (!temporary.has_value() || !work.is_absolute() ||
        has_dot_component(work) || !normal_directory(work)) return false;
    const auto parent = temporary.value() / L"KF2OptimizerNext-Update";
    const auto identity = work_identity(work);
    return identity && identity->first == expected_process_id &&
        normal_directory(parent) && same_path(work.parent_path(), parent);
}

std::optional<DirectoryIdentity> directory_identity(
    const std::filesystem::path& path) {
    HANDLE directory = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool valid = GetFileInformationByHandle(directory, &information) != FALSE &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    CloseHandle(directory);
    if (!valid) return std::nullopt;
    return DirectoryIdentity{
        information.dwVolumeSerialNumber,
        information.nFileIndexHigh,
        information.nFileIndexLow};
}

std::string make_token() {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(),
                        static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        token.push_back(hex[byte >> 4U]);
        token.push_back(hex[byte & 0x0FU]);
    }
    return token;
}

std::wstring quote(std::wstring_view value) {
    std::wstring result{L"\""};
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            result.append(slashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            slashes = 0;
            result.push_back(character);
        }
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

Result<PROCESS_INFORMATION> start_process(
    const std::filesystem::path& executable, std::wstring arguments) {
    if (!normal_file(executable)) return Result<PROCESS_INFORMATION>::failure(
        {ErrorCode::not_found, L"Update restart executable is unavailable", 0});
    std::wstring command = quote(executable.wstring());
    if (!arguments.empty()) command += L" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr,
                        executable.parent_path().c_str(), &startup, &process)) {
        return Result<PROCESS_INFORMATION>::failure(
            {ErrorCode::platform_failure, L"Updated application could not start",
             GetLastError()});
    }
    return Result<PROCESS_INFORMATION>::success(process);
}

Result<std::string> read_small_file(const std::filesystem::path& path) {
    if (!normal_file(path)) return Result<std::string>::failure(
        {ErrorCode::not_found, L"Update helper request is unavailable", 0});
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 64U * 1024U || error) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"Update helper request is too large", 0});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"Update helper request cannot be read", 0});
    return Result<std::string>::success(
        {std::istreambuf_iterator<char>{input},
         std::istreambuf_iterator<char>{}});
}

Result<HelperRequest> parse_request(const std::filesystem::path& path) {
    const auto bytes = read_small_file(path);
    if (!bytes.has_value()) return Result<HelperRequest>::failure(bytes.error());
    std::map<std::string, std::string> values;
    std::size_t offset = 0;
    while (offset < bytes.value().size()) {
        const auto end = bytes.value().find('\n', offset);
        auto line = bytes.value().substr(
            offset, end == std::string::npos ? std::string::npos : end - offset);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        offset = end == std::string::npos ? bytes.value().size() : end + 1U;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            !values.emplace(line.substr(0, separator),
                            line.substr(separator + 1U)).second) {
            return Result<HelperRequest>::failure(
                {ErrorCode::invalid_argument, L"Update helper request is invalid", 0});
        }
    }
    constexpr std::array<std::string_view, 8> keys{
        "schema_version", "parent_process_id", "target_root", "staged_root",
        "backup_root", "receipt_path", "expected_version", "token"};
    if (values.size() != keys.size() || values["schema_version"] != "1") {
        return Result<HelperRequest>::failure(
            {ErrorCode::invalid_argument, L"Update helper request schema is invalid", 0});
    }
    for (const auto key : keys) if (!values.contains(std::string{key})) {
        return Result<HelperRequest>::failure(
            {ErrorCode::invalid_argument, L"Update helper request is incomplete", 0});
    }
    std::uint64_t parent = 0;
    const auto* begin = values["parent_process_id"].data();
    const auto* end = begin + values["parent_process_id"].size();
    const auto parsed = std::from_chars(begin, end, parent);
    const auto target = wide_from_utf8(values["target_root"]);
    const auto staged = wide_from_utf8(values["staged_root"]);
    const auto backup = wide_from_utf8(values["backup_root"]);
    const auto receipt = wide_from_utf8(values["receipt_path"]);
    if (parsed.ec != std::errc{} || parsed.ptr != end || parent == 0 ||
        parent > UINT32_MAX || !target || !staged || !backup || !receipt ||
        !safe_token(values["token"]) ||
        !parse_semantic_version(values["expected_version"]).has_value()) {
        return Result<HelperRequest>::failure(
            {ErrorCode::invalid_argument, L"Update helper request values are invalid", 0});
    }
    HelperRequest request{
        .parent_process_id = static_cast<std::uint32_t>(parent),
        .target_root = *target,
        .staged_root = *staged,
        .backup_root = *backup,
        .receipt_path = *receipt,
        .expected_version = values["expected_version"],
        .token = values["token"]};
    const auto work = path.parent_path();
    const auto marker = read_small_file(work / L"update.marker");
    if (!path.is_absolute() ||
        !canonical_update_work_root(work, request.parent_process_id) ||
        !normal_directory(request.target_root) ||
        !normal_directory(request.staged_root) ||
        !child_of(request.staged_root, work) ||
        !child_of(request.backup_root, work) ||
        !child_of(request.receipt_path, work) ||
        child_of(request.target_root, work) || !marker.has_value() ||
        marker.value() != request.token) {
        return Result<HelperRequest>::failure(
            {ErrorCode::access_denied, L"Update helper path validation failed", 0});
    }
    return Result<HelperRequest>::success(std::move(request));
}

std::optional<DirectoryIdentity> validated_cleanup_identity(
    const std::filesystem::path& work, std::string_view token) {
    const auto request = parse_request(work / L"update-request.ini");
    const auto marker = read_small_file(work / L"update.marker");
    if (!request.has_value() || request.value().token != token ||
        !canonical_update_work_root(work, request.value().parent_process_id) ||
        !marker.has_value() || marker.value() != token) return std::nullopt;
    const auto cleanup_allowed = update_transaction_allows_cleanup(
        transaction_request(request.value()));
    if (!cleanup_allowed.has_value() || !cleanup_allowed.value()) {
        return std::nullopt;
    }
    return directory_identity(work);
}

Result<bool> write_request(const std::filesystem::path& path,
                           const HelperRequest& request) {
    const auto target = utf8_from_wide(request.target_root.wstring());
    const auto staged = utf8_from_wide(request.staged_root.wstring());
    const auto backup = utf8_from_wide(request.backup_root.wstring());
    const auto receipt = utf8_from_wide(request.receipt_path.wstring());
    if (!target || !staged || !backup || !receipt ||
        target->find_first_of("\r\n") != std::string::npos ||
        staged->find_first_of("\r\n") != std::string::npos ||
        backup->find_first_of("\r\n") != std::string::npos ||
        receipt->find_first_of("\r\n") != std::string::npos) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Update helper paths are invalid", 0});
    }
    const std::string bytes =
        "schema_version=1\nparent_process_id=" +
        std::to_string(request.parent_process_id) + "\ntarget_root=" + *target +
        "\nstaged_root=" + *staged + "\nbackup_root=" + *backup +
        "\nreceipt_path=" + *receipt + "\nexpected_version=" +
        request.expected_version + "\ntoken=" + request.token + "\n";
    return platform::windows::atomic_replace_utf8(path, bytes);
}

bool wait_for_parent(std::uint32_t process_id) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (parent) {
        const DWORD waited = WaitForSingleObject(parent, 30'000);
        CloseHandle(parent);
        return waited == WAIT_OBJECT_0;
    }
    return GetLastError() == ERROR_INVALID_PARAMETER;
}

bool receipt_ready(const std::filesystem::path& receipt,
                   std::string_view token) {
    const auto bytes = read_small_file(receipt);
    return bytes.has_value() && bytes.value() == token;
}

bool marker_matches(const std::filesystem::path& work,
                    std::string_view token) {
    const auto marker = read_small_file(work / L"update.marker");
    return marker.has_value() && marker.value() == token;
}

Result<bool> launch_cleanup_instance(const HelperRequest& request) {
    const auto executable = request.target_root / L"KF2Optimizer.exe";
    auto process = start_process(
        executable, L"--portable-update-cleanup " +
            std::to_wstring(GetCurrentProcessId()) + L" " +
            quote(request.backup_root.parent_path().wstring()) + L" " +
            quote(std::wstring{request.token.begin(), request.token.end()}));
    if (process.has_value()) {
        CloseHandle(process.value().hThread);
        CloseHandle(process.value().hProcess);
        return Result<bool>::success(true);
    }
    return Result<bool>::failure(process.error());
}

}  // namespace

Result<bool> launch_update_helper(
    const PreparedUpdatePackage& package,
    const std::filesystem::path& target_root,
    std::string_view expected_version,
    std::uint32_t parent_process_id) {
    const auto native_target_root =
        platform::windows::extended_length_path(target_root);
    if (!normal_directory(package.work_root) ||
        !normal_directory(package.staged_root) ||
        !normal_directory(native_target_root) ||
        !parse_semantic_version(expected_version).has_value() ||
        parent_process_id == 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Update helper launch input is invalid", 0});
    }
    const std::string token = make_token();
    if (!safe_token(token)) return Result<bool>::failure(
        {ErrorCode::platform_failure, L"Update authorization token could not be created", 0});
    const auto module = platform::windows::executable_path();
    if (!module.has_value()) return Result<bool>::failure(module.error());
    const auto helper = package.work_root / L"KF2UpdateHelper.exe";
    std::error_code error;
    std::filesystem::copy_file(
        platform::windows::extended_length_path(module.value()),
        platform::windows::extended_length_path(helper),
                               std::filesystem::copy_options::none, error);
    if (error || !normal_file(helper)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Temporary update helper could not be created",
         static_cast<std::uint32_t>(error.value())});
    const auto marker = platform::windows::atomic_replace_utf8(
        package.work_root / L"update.marker", token);
    if (!marker.has_value()) return marker;
    HelperRequest request{
        .parent_process_id = parent_process_id,
        .target_root = native_target_root,
        .staged_root = package.staged_root,
        .backup_root = package.work_root / L"backup",
        .receipt_path = package.work_root / L"ready.receipt",
        .expected_version = std::string{expected_version},
        .token = token};
    const auto request_path = package.work_root / L"update-request.ini";
    const auto written = write_request(request_path, request);
    if (!written.has_value()) return written;
    auto process = start_process(helper,
        L"--portable-update-helper " + quote(request_path.wstring()));
    if (!process.has_value()) return Result<bool>::failure(process.error());
    CloseHandle(process.value().hThread);
    CloseHandle(process.value().hProcess);
    return Result<bool>::success(true);
}

int run_update_helper(const std::filesystem::path& request_path) noexcept {
    std::optional<HelperRequest> request;
    try {
        auto parsed = parse_request(request_path);
        if (!parsed.has_value()) return 20;
        request = std::move(parsed.value());
        if (!wait_for_parent(request->parent_process_id)) return 25;
        const auto applied = apply_update_transaction(
            transaction_request(*request));
        if (!applied.has_value()) {
            const auto recovered = recover_update_transaction(
                transaction_request(*request));
            if (!recovered.has_value() ||
                (recovered.value().state !=
                     UpdateRecoveryState::rollback_verified &&
                 recovered.value().state !=
                     UpdateRecoveryState::not_started)) {
                return 22;
            }
            static_cast<void>(launch_cleanup_instance(*request));
            return 21;
        }
        const auto executable = request->target_root /
            L"KF2Optimizer.exe";
        auto process = start_process(
            executable, L"--portable-update-ready " +
                quote(request->receipt_path.wstring()) + L" " +
                std::to_wstring(GetCurrentProcessId()) + L" " +
                quote(request->backup_root.parent_path().wstring()) + L" " +
                quote(std::wstring{request->token.begin(),
                                   request->token.end()}));
        bool ready = false;
        if (process.has_value()) {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{20};
            while (std::chrono::steady_clock::now() < deadline) {
                if (receipt_ready(request->receipt_path,
                                  request->token)) {
                    ready = true;
                    break;
                }
                if (WaitForSingleObject(process.value().hProcess, 0) ==
                    WAIT_OBJECT_0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }
        if (ready) {
            CloseHandle(process.value().hThread);
            CloseHandle(process.value().hProcess);
            return 0;
        }
        if (process.has_value()) {
            TerminateProcess(process.value().hProcess, 30);
            WaitForSingleObject(process.value().hProcess, 5'000);
            CloseHandle(process.value().hThread);
            CloseHandle(process.value().hProcess);
        }
        const auto rolled_back = rollback_update_transaction(
            request->target_root, request->backup_root);
        if (!rolled_back.has_value()) return 22;
        static_cast<void>(launch_cleanup_instance(*request));
        return 23;
    } catch (...) {
        if (request) {
            const auto recovered = recover_update_transaction(
                transaction_request(*request));
            if (recovered.has_value() &&
                (recovered.value().state ==
                     UpdateRecoveryState::rollback_verified ||
                 recovered.value().state ==
                     UpdateRecoveryState::not_started)) {
                static_cast<void>(launch_cleanup_instance(*request));
            }
        }
        return 24;
    }
}

Result<bool> recover_interrupted_updates_on_startup(
    const std::filesystem::path& target_root) {
    if (!target_root.is_absolute() || !normal_directory(target_root)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Interrupted update target is invalid", 0});
    }
    const auto temporary = platform::windows::temporary_directory();
    if (!temporary.has_value()) return Result<bool>::failure(temporary.error());
    const auto update_root = temporary.value() / L"KF2OptimizerNext-Update";
    std::error_code error;
    if (!std::filesystem::exists(update_root, error)) {
        if (error) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"Interrupted update directory cannot be inspected",
                 static_cast<std::uint32_t>(error.value())});
        }
        return Result<bool>::success(false);
    }
    if (!normal_directory(update_root)) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Interrupted update directory has an unsafe identity", 0});
    }
    const auto target_identity = directory_identity(target_root);
    if (!target_identity) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Interrupted update target identity is unavailable", 0});
    }
    std::size_t inspected = 0;
    std::filesystem::directory_iterator iterator{update_root, error};
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto work = iterator->path();
        iterator.increment(error);
        if (++inspected > 128U) {
            return Result<bool>::failure(
                {ErrorCode::access_denied,
                 L"Too many interrupted update directories were found", 0});
        }
        if (!normal_directory(work)) continue;
        auto request = parse_request(work / L"update-request.ini");
        const auto request_target_identity = request.has_value()
            ? directory_identity(request.value().target_root)
            : std::nullopt;
        if (!request.has_value() || !request_target_identity ||
            *request_target_identity != *target_identity) {
            continue;
        }
        auto recovered = recover_update_transaction(
            transaction_request(request.value()));
        if (!recovered.has_value()) {
            return Result<bool>::failure(recovered.error());
        }
        if (recovered.value().state == UpdateRecoveryState::owner_active) {
            return Result<bool>::success(true);
        }
        if (recovered.value().state ==
                UpdateRecoveryState::rollback_verified ||
            recovered.value().state == UpdateRecoveryState::not_started) {
            auto launched = launch_cleanup_instance(request.value());
            if (!launched.has_value()) return launched;
            return Result<bool>::success(true);
        }
        const auto executable = request.value().target_root /
            L"KF2Optimizer.exe";
        auto process = start_process(
            executable, L"--portable-update-ready " +
                quote(request.value().receipt_path.wstring()) + L" " +
                std::to_wstring(GetCurrentProcessId()) + L" " +
                quote(work.wstring()) + L" " +
                quote(std::wstring{request.value().token.begin(),
                                   request.value().token.end()}));
        if (!process.has_value()) {
            return Result<bool>::failure(process.error());
        }
        CloseHandle(process.value().hThread);
        CloseHandle(process.value().hProcess);
        return Result<bool>::success(true);
    }
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Interrupted update directory enumeration failed",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(false);
}

Result<bool> signal_update_ready_and_schedule_cleanup(
    const UpdateReadyArguments& arguments) {
    const auto request = parse_request(
        arguments.work_root / L"update-request.ini");
    if (!safe_token(arguments.token) || arguments.helper_process_id == 0 ||
        !arguments.receipt_path.is_absolute() ||
        !normal_directory(arguments.work_root) ||
        !child_of(arguments.receipt_path, arguments.work_root) ||
        !marker_matches(arguments.work_root, arguments.token) ||
        !request.has_value() || request.value().token != arguments.token ||
        !same_path(request.value().receipt_path, arguments.receipt_path)) {
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Update restart handshake is invalid", 0});
    }
    const auto signaled = platform::windows::atomic_replace_utf8(
        arguments.receipt_path, arguments.token);
    if (!signaled.has_value()) return signaled;
    const auto handed_off = mark_update_transaction_handoff_ready(
        transaction_request(request.value()));
    if (!handed_off.has_value()) return handed_off;
    return schedule_update_cleanup(arguments.helper_process_id,
                                   arguments.work_root, arguments.token);
}

Result<bool> schedule_update_cleanup(
    std::uint32_t helper_process_id, const std::filesystem::path& work_root,
    std::string_view token) {
    const auto identity = safe_token(token) && helper_process_id != 0
        ? validated_cleanup_identity(work_root, token)
        : std::nullopt;
    if (!identity) {
        return Result<bool>::failure(
            {ErrorCode::access_denied, L"Update cleanup handshake is invalid", 0});
    }
    const auto work = normalized(work_root);
    const auto helper_id = helper_process_id;
    const std::string owned_token{token};
    std::thread([work, helper_id, owned_token, identity = *identity] {
        HANDLE helper = OpenProcess(SYNCHRONIZE, FALSE, helper_id);
        if (helper) {
            WaitForSingleObject(helper, 30'000);
            CloseHandle(helper);
        }
        const auto current_identity = validated_cleanup_identity(
            work, owned_token);
        if (!current_identity || *current_identity != identity) return;
        std::error_code ignored;
        std::filesystem::remove_all(work, ignored);
        const auto parent = work.parent_path();
        std::filesystem::remove(parent, ignored);
    }).detach();
    return Result<bool>::success(true);
}

}  // namespace kf2::update
