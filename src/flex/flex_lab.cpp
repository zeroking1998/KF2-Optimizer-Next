#include "kf2/flex/flex_lab.hpp"

#include <Windows.h>

#include <string_view>
#include <system_error>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::flex {
namespace {

constexpr wchar_t active_name[] = L"flexRelease_x64.dll";
constexpr wchar_t original_name[] = L"flexRelease_original.dll";
constexpr wchar_t backup_name[] = L"flexRelease_x64.pre-lab.dll";
constexpr wchar_t marker_name[] = L"flex-lab-transaction.marker";
constexpr std::uint64_t maximum_runtime_bytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t maximum_marker_bytes = 4ULL * 1024ULL;

#if defined(KF2_FLEX_LAB_TEST_HOOKS)
LabInstallTestHook install_test_hook{};
LabStatusHook status_test_hook{};

void run_install_test_hook(LabInstallTestCheckpoint checkpoint) {
    if (install_test_hook != nullptr) install_test_hook(checkpoint);
}
#endif

Result<std::string> hash_file(const std::filesystem::path& path) {
    return security::sha256_file_hex(path, maximum_runtime_bytes);
}

Result<bool> copy_verified(const std::filesystem::path& source,
                           const std::filesystem::path& target,
                           std::string_view expected_hash) {
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX laboratory copy failed", GetLastError()});
    const auto copied_hash = hash_file(target);
    if (!copied_hash.has_value()) return Result<bool>::failure(copied_hash.error());
    if (copied_hash.value() != expected_hash) return Result<bool>::failure(
        {ErrorCode::stale_data,
         L"FleX laboratory source changed before the verified copy completed", 0});
    return Result<bool>::success(true);
}

Result<bool> replace_verified(const std::filesystem::path& source,
                              const std::filesystem::path& target,
                              std::string_view expected_hash) {
    const auto temporary = target.parent_path() /
        (target.filename().wstring() + L".kf2lab." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(GetCurrentThreadId()) + L".tmp");
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE))
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX laboratory staging copy failed", GetLastError()});

    const auto cleanup = [&] {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
    };
    const auto staged_hash = hash_file(temporary);
    if (!staged_hash.has_value() || staged_hash.value() != expected_hash) {
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"FleX laboratory source changed before staging completed", 0});
    }

    HANDLE staged = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (staged == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX laboratory staging file could not be opened", error});
    }
    const BOOL flushed = FlushFileBuffers(staged);
    const auto flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(staged);
    if (!flushed) {
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX laboratory staging file could not be flushed", flush_error});
    }

    DWORD last_error = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto installed_hash = hash_file(target);
            if (installed_hash.has_value() &&
                installed_hash.value() == expected_hash)
                return Result<bool>::success(true);
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"FleX laboratory replacement hash mismatch", 0});
        }
        last_error = GetLastError();
        if (last_error != ERROR_SHARING_VIOLATION &&
            last_error != ERROR_LOCK_VIOLATION && last_error != ERROR_ACCESS_DENIED)
            break;
        Sleep(10U << attempt);
    }
    cleanup();
    return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX laboratory atomic replacement failed", last_error});
}

Result<bool> preflight(const std::filesystem::path& game,
                       const std::filesystem::path& state, bool running) {
    if (running) return Result<bool>::failure(
        {ErrorCode::access_denied, L"KF2 must be completely stopped", 0});
    std::error_code ec;
    if (!std::filesystem::is_directory(game, ec) || ec) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"KF2 FleX directory is invalid", 0});
    std::filesystem::create_directories(state, ec);
    if (ec) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX laboratory state directory failed", static_cast<std::uint32_t>(ec.value())});
    return Result<bool>::success(true);
}

Result<bool> inspected_exists(const std::filesystem::path& path) {
    std::error_code error;
#if defined(KF2_FLEX_LAB_TEST_HOOKS)
    const bool exists = status_test_hook != nullptr
        ? status_test_hook(path, error)
        : std::filesystem::exists(path, error);
#else
    const bool exists = std::filesystem::exists(path, error);
#endif
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"FleX laboratory path status cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<bool>::success(exists);
}

struct LabMarker {
    std::string state;
    std::string original_hash;
    std::string forwarder_hash;
    std::string owner_hash;
};

Result<std::string> installation_identity(const std::filesystem::path& game) {
    HANDLE directory = CreateFileW(game.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) return Result<std::string>::failure(
        {ErrorCode::access_denied, L"FleX installation identity cannot be opened",
         GetLastError()});
    BY_HANDLE_FILE_INFORMATION info{};
    const bool inspected = GetFileInformationByHandle(directory, &info) != FALSE;
    const auto native = inspected ? ERROR_INVALID_DATA : GetLastError();
    CloseHandle(directory);
    const auto index = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) |
        info.nFileIndexLow;
    if (!game.is_absolute() || !inspected || index == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"FleX installation identity is unsafe", native});
    return security::sha256_hex(std::to_string(info.dwVolumeSerialNumber) +
        ":" + std::to_string(index));
}

Result<LabMarker> parse_marker(const std::filesystem::path& marker) {
    const auto marker_bytes = platform::windows::read_bounded_verified_file(
        marker, maximum_marker_bytes);
    if (!marker_bytes.has_value()) {
        return Result<LabMarker>::failure(marker_bytes.error());
    }
    const std::string& bytes = marker_bytes.value();
    constexpr std::string_view prefix = "schema=3\nstate=";
    if (!bytes.starts_with(prefix)) return Result<LabMarker>::failure(
        {ErrorCode::invalid_argument,
         L"FleX recovery requires an installation-bound transaction; unrecognized or legacy evidence was retained", 0});
    const auto state_end = bytes.find('\n', prefix.size());
    const auto original_prefix = bytes.find("original_sha256=", state_end);
    const auto forwarder_prefix = bytes.find("forwarder_sha256=", state_end);
    const auto owner_prefix = bytes.find("owner_sha256=", state_end);
    if (state_end == std::string::npos || original_prefix == std::string::npos ||
        forwarder_prefix == std::string::npos || owner_prefix == std::string::npos)
        return Result<LabMarker>::failure(
            {ErrorCode::invalid_argument, L"FleX laboratory marker fields are missing", 0});
    LabMarker parsed;
    parsed.state = bytes.substr(prefix.size(), state_end - prefix.size());
    const auto original_start = original_prefix + std::string_view{"original_sha256="}.size();
    const auto original_end = bytes.find('\n', original_start);
    const auto forwarder_start = forwarder_prefix + std::string_view{"forwarder_sha256="}.size();
    const auto forwarder_end = bytes.find('\n', forwarder_start);
    parsed.original_hash = bytes.substr(original_start, original_end - original_start);
    parsed.forwarder_hash = bytes.substr(forwarder_start, forwarder_end - forwarder_start);
    const auto owner_start = owner_prefix + std::string_view{"owner_sha256="}.size();
    parsed.owner_hash = bytes.substr(owner_start, bytes.find('\n', owner_start) - owner_start);
    const auto valid_hash = [](const std::string& hash) {
        return hash.size() == 64 &&
            hash.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    if ((parsed.state != "installing" && parsed.state != "installed") ||
        !valid_hash(parsed.original_hash) || !valid_hash(parsed.forwarder_hash) ||
        !valid_hash(parsed.owner_hash) || bytes != "schema=3\nstate=" + parsed.state +
            "\noriginal_sha256=" + parsed.original_hash +
            "\nforwarder_sha256=" + parsed.forwarder_hash +
            "\nowner_sha256=" + parsed.owner_hash + "\n")
        return Result<LabMarker>::failure(
            {ErrorCode::invalid_argument, L"FleX laboratory marker hash is invalid", 0});
    return Result<LabMarker>::success(std::move(parsed));
}

Result<LabMarker> owned_marker(const std::filesystem::path& game,
                              const std::filesystem::path& marker) {
    auto parsed = parse_marker(marker);
    if (!parsed.has_value()) return parsed;
    const auto identity = installation_identity(game);
    if (!identity.has_value()) return Result<LabMarker>::failure(identity.error());
    if (parsed.value().owner_hash != identity.value()) return Result<LabMarker>::failure(
        {ErrorCode::stale_data,
         L"FleX recovery belongs to a different installation; select its original KF2 folder", 0});
    return parsed;
}

}  // namespace

#if defined(KF2_FLEX_LAB_TEST_HOOKS)
void set_lab_install_test_hook(LabInstallTestHook hook) noexcept {
    install_test_hook = hook;
}

void set_lab_status_hook_for_testing(LabStatusHook hook) noexcept {
    status_test_hook = hook;
}
#endif

Result<LabTransactionResult> install_offline_lab(const LabTransactionOptions& o) {
    const auto checked = preflight(o.game_directory, o.state_directory, o.game_running);
    if (!checked.has_value()) return Result<LabTransactionResult>::failure(checked.error());
    if (!o.exact_runtime_verified || !o.offline_confirmed)
        return Result<LabTransactionResult>::failure({ErrorCode::access_denied,
            L"Exact runtime identity and explicit offline confirmation are required", 0});
    if (o.forwarder_dll.filename() != L"flexRelease_x64.forwarder-lab.dll")
        return Result<LabTransactionResult>::failure({ErrorCode::invalid_argument,
            L"Unexpected FleX laboratory forwarder", 0});
    const auto active = o.game_directory / active_name;
    const auto original = o.game_directory / original_name;
    const auto backup = o.state_directory / backup_name;
    const auto marker = o.state_directory / marker_name;
    const auto marker_exists = inspected_exists(marker);
    const auto original_exists = inspected_exists(original);
    if (!marker_exists.has_value()) {
        return Result<LabTransactionResult>::failure(marker_exists.error());
    }
    if (!original_exists.has_value()) {
        return Result<LabTransactionResult>::failure(original_exists.error());
    }
    if (marker_exists.value() || original_exists.value())
        return Result<LabTransactionResult>::failure({ErrorCode::stale_data,
            L"An unfinished FleX laboratory transaction requires recovery", 0});
    const auto original_hash = hash_file(active);
    const auto forwarder_hash = hash_file(o.forwarder_dll);
    const auto owner_hash = installation_identity(o.game_directory);
    if (!original_hash.has_value()) return Result<LabTransactionResult>::failure(original_hash.error());
    if (!forwarder_hash.has_value()) return Result<LabTransactionResult>::failure(forwarder_hash.error());
    if (!owner_hash.has_value()) return Result<LabTransactionResult>::failure(owner_hash.error());
#if defined(KF2_FLEX_LAB_TEST_HOOKS)
    run_install_test_hook(LabInstallTestCheckpoint::sources_hashed);
#endif
    auto copied = copy_verified(active, backup, original_hash.value());
    if (!copied.has_value()) return Result<LabTransactionResult>::failure(copied.error());
    const auto marker_text = [&](std::string_view state) {
        return "schema=3\nstate=" + std::string{state} +
            "\noriginal_sha256=" + original_hash.value() +
            "\nforwarder_sha256=" + forwarder_hash.value() +
            "\nowner_sha256=" + owner_hash.value() + "\n";
    };
    const auto marked = platform::windows::atomic_replace_utf8(
        marker, marker_text("installing"));
    if (!marked.has_value()) return Result<LabTransactionResult>::failure(marked.error());
    // Persist ownership before creating any in-game recovery residue.
    copied = copy_verified(active, original, original_hash.value());
    if (!copied.has_value()) return Result<LabTransactionResult>::failure(copied.error());
#if defined(KF2_FLEX_LAB_TEST_HOOKS)
    run_install_test_hook(LabInstallTestCheckpoint::marker_written);
#endif
    copied = replace_verified(o.forwarder_dll, active, forwarder_hash.value());
    if (!copied.has_value() || o.simulate_failure_after_install) {
        const auto restored = restore_offline_lab(o.game_directory, o.state_directory, false);
        if (!restored.has_value()) return Result<LabTransactionResult>::failure(restored.error());
        return Result<LabTransactionResult>::failure({ErrorCode::io_failure,
            L"Simulated or real FleX installation failure was rolled back", 0});
    }
    const auto committed = platform::windows::atomic_replace_utf8(
        marker, marker_text("installed"));
    if (!committed.has_value()) {
        const auto restored = restore_offline_lab(o.game_directory, o.state_directory, false);
        if (!restored.has_value()) return Result<LabTransactionResult>::failure(restored.error());
        return Result<LabTransactionResult>::failure(committed.error());
    }
    return Result<LabTransactionResult>::success(
        {original_hash.value(), forwarder_hash.value(), true});
}

Result<bool> restore_offline_lab(const std::filesystem::path& game,
                                 const std::filesystem::path& state, bool running,
                                 std::wstring* recovery_details) {
    if (recovery_details != nullptr) recovery_details->clear();
    const auto checked = preflight(game, state, running);
    if (!checked.has_value()) return checked;
    const auto active = game / active_name;
    const auto original = game / original_name;
    const auto backup = state / backup_name;
    const auto marker = state / marker_name;
    const auto marker_exists = inspected_exists(marker);
    const auto original_exists = inspected_exists(original);
    const auto backup_exists = inspected_exists(backup);
    if (!marker_exists.has_value()) return marker_exists;
    if (!marker_exists.value()) return Result<bool>::failure(
        {ErrorCode::not_found,
         L"FleX recovery requires an installation-bound marker; recovery copies were retained", 0});
    const auto parsed = owned_marker(game, marker);
    if (!parsed.has_value()) return Result<bool>::failure(parsed.error());
    const auto verify_source = [&](const std::filesystem::path& path,
                                   const Result<bool>& exists) {
        if (!exists.has_value())
            return Result<std::string>::failure(exists.error());
        if (!exists.value()) return Result<std::string>::failure(
            {ErrorCode::not_found, L"Recovery copy is missing", 0});
        auto hash = hash_file(path);
        if (hash.has_value() && hash.value() != parsed.value().original_hash)
            return Result<std::string>::failure(
                {ErrorCode::stale_data,
                 L"Recovery copy does not match the transaction marker", 0});
        return hash;
    };
    const auto backup_hash = verify_source(backup, backup_exists);
    const auto original_hash = verify_source(original, original_exists);
    if (!backup_hash.has_value() && !original_hash.has_value()) {
        auto error = backup_hash.error().code == ErrorCode::not_found
            ? original_hash.error() : backup_hash.error();
        error.message = L"No verified FleX recovery copy is available. State backup: " +
            backup_hash.error().message + L"; in-game original: " +
            original_hash.error().message;
        return Result<bool>::failure(std::move(error));
    }
    const bool use_backup = backup_hash.has_value();
    const auto& source = use_backup ? backup : original;
    const auto& source_before = use_backup ? backup_hash : original_hash;
    auto copied = replace_verified(source, active, source_before.value());
    if (!copied.has_value()) return copied;
    const auto source_hash = hash_file(source); const auto active_hash = hash_file(active);
    if (!source_hash.has_value() || !active_hash.has_value() || source_hash.value() != active_hash.value())
        return Result<bool>::failure({ErrorCode::io_failure, L"FleX restore verification failed", 0});
    std::error_code ec;
    std::filesystem::remove(original, ec);
    if (ec) return Result<bool>::failure({ErrorCode::io_failure,
        L"FleX original residue removal failed", static_cast<std::uint32_t>(ec.value())});
    std::filesystem::remove(marker, ec);
    if (ec) return Result<bool>::failure({ErrorCode::io_failure,
        L"FleX transaction marker removal failed", static_cast<std::uint32_t>(ec.value())});
    if (recovery_details != nullptr) {
        *recovery_details = use_backup ? L"Recovery source: verified state backup."
                                      : L"Recovery source: verified in-game original.";
        const auto& other = use_backup ? original_hash : backup_hash;
        if (!other.has_value()) {
            *recovery_details += use_backup ? L" In-game original rejected: "
                                            : L" State backup rejected: ";
            *recovery_details += other.error().message;
        }
    }
    return Result<bool>::success(true);
}

Result<bool> recover_offline_lab(const std::filesystem::path& game,
                                 const std::filesystem::path& state, bool running,
                                 std::wstring* recovery_details) {
    if (recovery_details != nullptr) recovery_details->clear();
    const auto marker = state / marker_name;
    const auto marker_exists = inspected_exists(marker);
    const auto original_exists = inspected_exists(game / original_name);
    if (!marker_exists.has_value()) return marker_exists;
    if (!marker_exists.value()) {
        if (!original_exists.has_value()) return original_exists;
        if (!original_exists.value()) return Result<bool>::success(false);
    }
    if (marker_exists.value()) {
        const auto parsed = owned_marker(game, marker);
        if (!parsed.has_value()) return Result<bool>::failure(parsed.error());
        if (parsed.value().state == "installed") {
            const auto active_hash = hash_file(game / active_name);
            const auto original_hash = hash_file(game / original_name);
            const auto backup_hash = hash_file(state / backup_name);
            if (active_hash.has_value() && original_hash.has_value() &&
                backup_hash.has_value() &&
                active_hash.value() == parsed.value().forwarder_hash &&
                original_hash.value() == parsed.value().original_hash &&
                backup_hash.value() == parsed.value().original_hash) {
                return Result<bool>::success(false);
            }
        }
    }
    return restore_offline_lab(game, state, running, recovery_details);
}

}  // namespace kf2::flex
