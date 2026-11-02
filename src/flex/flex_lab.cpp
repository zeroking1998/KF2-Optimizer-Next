#include "kf2/flex/flex_lab.hpp"

#include <Windows.h>

#include <array>
#include <initializer_list>
#include <string_view>
#include <system_error>

#include "kf2/flex/binary_delta.hpp"
#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::flex {
namespace {

constexpr wchar_t active_core_name[] = L"flexRelease_x64.dll";
constexpr wchar_t relayed_core_name[] = L"flexRelease_original.dll";
constexpr wchar_t extension_name[] = L"flexExtRelease_x64.dll";
constexpr wchar_t helper_name[] = L"kf2_p4_overlap.dll";
constexpr wchar_t core_backup_name[] = L"flexRelease_x64.pre-lab.dll";
constexpr wchar_t extension_backup_name[] = L"flexExtRelease_x64.pre-lab.dll";
constexpr wchar_t helper_backup_name[] = L"kf2_p4_overlap.pre-lab.dll";
constexpr wchar_t marker_name[] = L"flex-lab-transaction.marker";
constexpr std::uint64_t maximum_runtime_bytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t maximum_marker_bytes = 8ULL * 1024ULL;

#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
RuntimeInstallTestHook install_test_hook{};
RuntimeStatusHook status_test_hook{};
void run_install_test_hook(RuntimeInstallTestCheckpoint checkpoint) {
    if (install_test_hook != nullptr) install_test_hook(checkpoint);
}
#endif

Result<std::string> hash_file(const std::filesystem::path& path) {
    return security::sha256_file_hex(path, maximum_runtime_bytes);
}

Result<bool> inspected_exists(const std::filesystem::path& path) {
    std::error_code error;
#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
    const bool exists = status_test_hook != nullptr
        ? status_test_hook(path, error)
        : std::filesystem::exists(path, error);
#else
    const bool exists = std::filesystem::exists(path, error);
#endif
    if (error) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX path status cannot be inspected",
         static_cast<std::uint32_t>(error.value())});
    return Result<bool>::success(exists);
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
    if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX staging copy failed", GetLastError()});
    }
    const auto cleanup = [&] {
        std::error_code error;
        std::filesystem::remove(temporary, error);
    };
    const auto staged_hash = hash_file(temporary);
    if (!staged_hash.has_value() || staged_hash.value() != expected_hash) {
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::stale_data, L"FleX source changed during staging", 0});
    }
    HANDLE staged = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (staged == INVALID_HANDLE_VALUE) {
        const auto native = GetLastError();
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX staging file could not be opened", native});
    }
    const BOOL flushed = FlushFileBuffers(staged);
    const auto flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(staged);
    if (!flushed) {
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX staging file could not be flushed", flush_error});
    }
    DWORD native = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto installed_hash = hash_file(target);
            if (installed_hash.has_value() && installed_hash.value() == expected_hash)
                return Result<bool>::success(true);
            return Result<bool>::failure(
                {ErrorCode::io_failure, L"FleX replacement verification failed", 0});
        }
        native = GetLastError();
        if (native != ERROR_SHARING_VIOLATION &&
            native != ERROR_LOCK_VIOLATION && native != ERROR_ACCESS_DENIED) break;
        Sleep(10U << attempt);
    }
    cleanup();
    return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX atomic replacement failed", native});
}

Result<bool> preflight(const std::filesystem::path& game,
                       const std::filesystem::path& state, bool running) {
    if (running) return Result<bool>::failure(
        {ErrorCode::access_denied, L"KF2 must be completely stopped", 0});
    std::error_code error;
    if (!std::filesystem::is_directory(game, error) || error) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"KF2 FleX directory is invalid", 0});
    }
    std::filesystem::create_directories(state, error);
    if (error) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX state directory could not be created",
         static_cast<std::uint32_t>(error.value())});
    return Result<bool>::success(true);
}

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
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"FleX installation identity is unsafe", native});
    }
    return security::sha256_hex(std::to_string(info.dwVolumeSerialNumber) +
        ":" + std::to_string(index));
}

struct LabMarker {
    std::string state;
    std::string stock_core;
    std::string stock_extension;
    std::string forwarder;
    std::string optimized_core;
    std::string optimized_extension;
    std::string optimized_helper;
    std::string original_helper;
    std::string owner;
};

struct LegacyLabMarker {
    std::string state;
    std::string original;
    std::string forwarder;
    std::string owner;
};

bool valid_hash(std::string_view hash) {
    return hash.size() == 64 &&
        hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::string marker_text(const LabMarker& marker) {
    return "schema=4\nstate=" + marker.state +
        "\nstock_core_sha256=" + marker.stock_core +
        "\nstock_extension_sha256=" + marker.stock_extension +
        "\nforwarder_sha256=" + marker.forwarder +
        "\noptimized_core_sha256=" + marker.optimized_core +
        "\noptimized_extension_sha256=" + marker.optimized_extension +
        "\noptimized_helper_sha256=" + marker.optimized_helper +
        "\noriginal_helper_sha256=" + marker.original_helper +
        "\nowner_sha256=" + marker.owner + "\n";
}

Result<LabMarker> parse_marker(const std::filesystem::path& path) {
    const auto read = platform::windows::read_bounded_verified_file(
        path, maximum_marker_bytes);
    if (!read.has_value()) return Result<LabMarker>::failure(read.error());
    const auto& bytes = read.value();
    constexpr std::string_view prefix = "schema=4\n";
    if (!bytes.starts_with(prefix)) return Result<LabMarker>::failure(
        {ErrorCode::invalid_argument,
         L"FleX recovery evidence is unrecognized or belongs to an older runtime", 0});
    const auto field = [&](std::string_view key) -> std::string {
        const auto start = bytes.find(key);
        if (start == std::string::npos) return {};
        const auto value = start + key.size();
        const auto end = bytes.find('\n', value);
        return end == std::string::npos ? std::string{} :
            bytes.substr(value, end - value);
    };
    LabMarker marker{
        field("state="), field("stock_core_sha256="),
        field("stock_extension_sha256="), field("forwarder_sha256="),
        field("optimized_core_sha256="), field("optimized_extension_sha256="),
        field("optimized_helper_sha256="), field("original_helper_sha256="),
        field("owner_sha256=")};
    if ((marker.state != "installing" && marker.state != "installed") ||
        !valid_hash(marker.stock_core) || !valid_hash(marker.stock_extension) ||
        !valid_hash(marker.forwarder) || !valid_hash(marker.optimized_core) ||
        !valid_hash(marker.optimized_extension) ||
        !valid_hash(marker.optimized_helper) ||
        (marker.original_helper != "absent" &&
         !valid_hash(marker.original_helper)) || !valid_hash(marker.owner) ||
        bytes != marker_text(marker)) {
        return Result<LabMarker>::failure(
            {ErrorCode::invalid_argument, L"FleX transaction marker is invalid", 0});
    }
    return Result<LabMarker>::success(std::move(marker));
}

Result<LegacyLabMarker> parse_legacy_marker(
    const std::filesystem::path& path) {
    const auto read = platform::windows::read_bounded_verified_file(
        path, maximum_marker_bytes);
    if (!read.has_value()) {
        return Result<LegacyLabMarker>::failure(read.error());
    }
    const auto& bytes = read.value();
    constexpr std::string_view prefix = "schema=3\n";
    if (!bytes.starts_with(prefix)) {
        return Result<LegacyLabMarker>::failure(
            {ErrorCode::invalid_argument,
             L"FleX recovery evidence is unrecognized", 0});
    }
    const auto field = [&](std::string_view key) -> std::string {
        const auto start = bytes.find(key);
        if (start == std::string::npos) return {};
        const auto value = start + key.size();
        const auto end = bytes.find('\n', value);
        return end == std::string::npos ? std::string{} :
            bytes.substr(value, end - value);
    };
    LegacyLabMarker marker{field("state="), field("original_sha256="),
        field("forwarder_sha256="), field("owner_sha256=")};
    const auto exact = "schema=3\nstate=" + marker.state +
        "\noriginal_sha256=" + marker.original +
        "\nforwarder_sha256=" + marker.forwarder +
        "\nowner_sha256=" + marker.owner + "\n";
    if ((marker.state != "installing" && marker.state != "installed") ||
        !valid_hash(marker.original) || !valid_hash(marker.forwarder) ||
        !valid_hash(marker.owner) || bytes != exact) {
        return Result<LegacyLabMarker>::failure(
            {ErrorCode::invalid_argument,
             L"Legacy FleX transaction marker is invalid", 0});
    }
    return Result<LegacyLabMarker>::success(std::move(marker));
}

Result<bool> is_legacy_marker(const std::filesystem::path& path) {
    const auto read = platform::windows::read_bounded_verified_file(
        path, maximum_marker_bytes);
    if (!read.has_value()) return Result<bool>::failure(read.error());
    return Result<bool>::success(read.value().starts_with("schema=3\n"));
}

Result<LabMarker> owned_marker(const std::filesystem::path& game,
                              const std::filesystem::path& marker_path) {
    auto marker = parse_marker(marker_path);
    if (!marker.has_value()) return marker;
    const auto owner = installation_identity(game);
    if (!owner.has_value()) return Result<LabMarker>::failure(owner.error());
    if (marker.value().owner != owner.value()) return Result<LabMarker>::failure(
        {ErrorCode::stale_data,
         L"FleX recovery belongs to a different KF2 installation", 0});
    return marker;
}

Result<bool> verified_source(const std::filesystem::path& path,
                             std::string_view expected) {
    const auto exists = inspected_exists(path);
    if (!exists.has_value()) return exists;
    if (!exists.value()) return Result<bool>::failure(
        {ErrorCode::not_found, L"A FleX recovery copy is missing", 0});
    const auto hash = hash_file(path);
    if (!hash.has_value()) return Result<bool>::failure(hash.error());
    if (hash.value() != expected) return Result<bool>::failure(
        {ErrorCode::stale_data, L"A FleX recovery copy is not verified", 0});
    return Result<bool>::success(true);
}

Result<bool> require_known_current(
    const std::filesystem::path& path,
    std::initializer_list<std::string_view> expected_hashes,
    bool allow_missing) {
    const auto exists = inspected_exists(path);
    if (!exists.has_value()) return exists;
    if (!exists.value()) {
        return allow_missing ? Result<bool>::success(true) : Result<bool>::failure(
            {ErrorCode::not_found,
             L"A managed FleX runtime component is missing", 0});
    }
    const auto hash = hash_file(path);
    if (!hash.has_value()) return Result<bool>::failure(hash.error());
    for (const auto expected : expected_hashes) {
        if (hash.value() == expected) return Result<bool>::success(true);
    }
    return Result<bool>::failure(
        {ErrorCode::stale_data,
         L"A managed FleX runtime component changed outside the transaction", 0});
}

Result<bool> remove_known_file(const std::filesystem::path& path,
                               std::string_view expected_hash) {
    const auto exists = inspected_exists(path);
    if (!exists.has_value() || !exists.value()) return exists;
    const auto hash = hash_file(path);
    if (!hash.has_value()) return Result<bool>::failure(hash.error());
    if (hash.value() != expected_hash) return Result<bool>::failure(
        {ErrorCode::stale_data,
         L"An unknown file occupies the optimized FleX helper path", 0});
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) return Result<bool>::failure(
        {ErrorCode::io_failure, L"Optimized FleX helper removal failed",
         static_cast<std::uint32_t>(error.value())});
    return Result<bool>::success(true);
}

Result<bool> restore_legacy_lab(const std::filesystem::path& game,
                                const std::filesystem::path& state,
                                std::wstring* recovery_details) {
    const auto marker_path = state / marker_name;
    const auto marker = parse_legacy_marker(marker_path);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());
    const auto owner = installation_identity(game);
    if (!owner.has_value()) return Result<bool>::failure(owner.error());
    if (marker.value().owner != owner.value()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Legacy FleX recovery belongs to a different KF2 installation", 0});
    }

    const auto active_core = game / active_core_name;
    const auto state_backup = state / core_backup_name;
    const auto in_game_copy = game / relayed_core_name;
    auto known = require_known_current(active_core,
        {marker.value().original, marker.value().forwarder}, true);
    if (!known.has_value()) return known;
    known = require_known_current(state_backup, {marker.value().original}, true);
    if (!known.has_value()) return known;
    known = require_known_current(in_game_copy, {marker.value().original}, true);
    if (!known.has_value()) return known;

    const auto backup_valid = verified_source(
        state_backup, marker.value().original);
    const auto in_game_valid = verified_source(
        in_game_copy, marker.value().original);
    const std::filesystem::path* source = nullptr;
    if (backup_valid.has_value()) source = &state_backup;
    else if (in_game_valid.has_value()) source = &in_game_copy;
    if (source == nullptr) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"No verified legacy FleX recovery copy is available", 0});
    }
    auto restored = replace_verified(
        *source, active_core, marker.value().original);
    if (!restored.has_value()) return restored;
    const auto relay_exists = inspected_exists(in_game_copy);
    if (!relay_exists.has_value()) return relay_exists;
    if (relay_exists.value()) {
        restored = remove_known_file(in_game_copy, marker.value().original);
        if (!restored.has_value()) return restored;
    }
    std::error_code error;
    std::filesystem::remove(marker_path, error);
    if (error) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Legacy FleX transaction marker removal failed",
             static_cast<std::uint32_t>(error.value())});
    }
    if (recovery_details != nullptr) {
        *recovery_details =
            L"The verified legacy FleX relay was removed before the optimized runtime was prepared.";
    }
    return Result<bool>::success(true);
}

}  // namespace

#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
void set_runtime_install_test_hook(RuntimeInstallTestHook hook) noexcept {
    install_test_hook = hook;
}
void set_runtime_status_hook_for_testing(RuntimeStatusHook hook) noexcept {
    status_test_hook = hook;
}
#endif

Result<OptimizedRuntimeTransactionResult> install_optimized_runtime_transaction(
    const OptimizedRuntimeTransactionOptions& o) {
    const auto checked = preflight(o.game_directory, o.state_directory, o.game_running);
    if (!checked.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(checked.error());
    if (!o.exact_runtime_verified) {
        return Result<OptimizedRuntimeTransactionResult>::failure(
            {ErrorCode::access_denied,
             L"The exact stock runtime identity is required", 0});
    }
    if (o.forwarder_dll.filename() != L"flexRelease_x64.forwarder-lab.dll" ||
        o.optimized_core_dll.filename() != L"flexRelease_x64.optimized.dll" ||
        o.optimized_extension_dll.filename() != L"flexExtRelease_x64.optimized.dll" ||
        o.optimized_helper_dll.filename() != helper_name) {
        return Result<OptimizedRuntimeTransactionResult>::failure(
            {ErrorCode::invalid_argument, L"Unexpected optimized FleX component", 0});
    }
    const auto active_core = o.game_directory / active_core_name;
    const auto relayed_core = o.game_directory / relayed_core_name;
    const auto active_extension = o.game_directory / extension_name;
    const auto active_helper = o.game_directory / helper_name;
    const auto marker_path = o.state_directory / marker_name;
    const auto marker_exists = inspected_exists(marker_path);
    const auto relay_exists = inspected_exists(relayed_core);
    if (!marker_exists.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(marker_exists.error());
    if (!relay_exists.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(relay_exists.error());
    if (marker_exists.value() || relay_exists.value()) return Result<OptimizedRuntimeTransactionResult>::failure(
        {ErrorCode::stale_data, L"An unfinished FleX transaction requires recovery", 0});

    const auto stock_core = hash_file(active_core);
    const auto stock_extension = hash_file(active_extension);
    const auto forwarder = hash_file(o.forwarder_dll);
    const auto optimized_core = hash_file(o.optimized_core_dll);
    const auto optimized_extension = hash_file(o.optimized_extension_dll);
    const auto optimized_helper = hash_file(o.optimized_helper_dll);
    const auto owner = installation_identity(o.game_directory);
    if (!stock_core.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(stock_core.error());
    if (!stock_extension.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(stock_extension.error());
    if (!forwarder.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(forwarder.error());
    if (!optimized_core.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(optimized_core.error());
    if (!optimized_extension.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(optimized_extension.error());
    if (!optimized_helper.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(optimized_helper.error());
    if (!owner.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(owner.error());
#if !defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
    if (stock_core.value() != optimized_core_recipe().original_sha256 ||
        stock_extension.value() != optimized_extension_recipe().original_sha256 ||
        optimized_core.value() != optimized_core_recipe().target_sha256 ||
        optimized_extension.value() != optimized_extension_recipe().target_sha256) {
        return Result<OptimizedRuntimeTransactionResult>::failure(
            {ErrorCode::stale_data, L"The FleX Core/Extension pair identity is unsupported", 0});
    }
#endif
#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
    run_install_test_hook(RuntimeInstallTestCheckpoint::sources_hashed);
#endif

    const auto helper_exists = inspected_exists(active_helper);
    if (!helper_exists.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(helper_exists.error());
    std::string original_helper = "absent";
    if (helper_exists.value()) {
        const auto hash = hash_file(active_helper);
        if (!hash.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(hash.error());
        original_helper = hash.value();
    }
    LabMarker marker{"installing", stock_core.value(), stock_extension.value(),
        forwarder.value(), optimized_core.value(), optimized_extension.value(),
        optimized_helper.value(), original_helper, owner.value()};
    auto copied = replace_verified(active_core,
        o.state_directory / core_backup_name, marker.stock_core);
    if (!copied.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(copied.error());
    copied = replace_verified(active_extension,
        o.state_directory / extension_backup_name, marker.stock_extension);
    if (!copied.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(copied.error());
    if (helper_exists.value()) {
        copied = replace_verified(active_helper,
            o.state_directory / helper_backup_name, marker.original_helper);
        if (!copied.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(copied.error());
    }
    const auto marked = platform::windows::atomic_replace_utf8(
        marker_path, marker_text(marker));
    if (!marked.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(marked.error());
#if defined(KF2_FLEX_RUNTIME_TEST_HOOKS)
    run_install_test_hook(RuntimeInstallTestCheckpoint::marker_written);
#endif
    const auto install = [&](const std::filesystem::path& source,
                             const std::filesystem::path& target,
                             std::string_view expected) {
        return replace_verified(source, target, expected);
    };
    copied = install(o.optimized_core_dll, relayed_core, marker.optimized_core);
    if (copied.has_value()) copied = install(o.optimized_extension_dll,
        active_extension, marker.optimized_extension);
    if (copied.has_value()) copied = install(o.optimized_helper_dll,
        active_helper, marker.optimized_helper);
    if (copied.has_value()) copied = install(o.forwarder_dll,
        active_core, marker.forwarder);
    if (!copied.has_value() || o.simulate_failure_after_install) {
        const auto restored = restore_optimized_runtime_transaction(
            o.game_directory, o.state_directory, false);
        if (!restored.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(restored.error());
        return Result<OptimizedRuntimeTransactionResult>::failure(
            {ErrorCode::io_failure, L"FleX installation failed and was rolled back", 0});
    }
    marker.state = "installed";
    const auto committed = platform::windows::atomic_replace_utf8(
        marker_path, marker_text(marker));
    if (!committed.has_value()) {
        const auto restored = restore_optimized_runtime_transaction(
            o.game_directory, o.state_directory, false);
        if (!restored.has_value()) return Result<OptimizedRuntimeTransactionResult>::failure(restored.error());
        return Result<OptimizedRuntimeTransactionResult>::failure(committed.error());
    }
    return Result<OptimizedRuntimeTransactionResult>::success({marker.stock_core,
        marker.stock_extension, marker.forwarder, marker.optimized_core,
        marker.optimized_extension, marker.optimized_helper, true});
}

Result<bool> restore_optimized_runtime_transaction(const std::filesystem::path& game,
                                 const std::filesystem::path& state,
                                 bool running,
                                 std::wstring* recovery_details) {
    if (recovery_details != nullptr) recovery_details->clear();
    const auto checked = preflight(game, state, running);
    if (!checked.has_value()) return checked;
    const auto marker_path = state / marker_name;
    const auto exists = inspected_exists(marker_path);
    if (!exists.has_value()) return exists;
    if (!exists.value()) return Result<bool>::failure(
        {ErrorCode::not_found, L"FleX recovery marker is missing", 0});
    const auto legacy = is_legacy_marker(marker_path);
    if (!legacy.has_value()) return legacy;
    if (legacy.value()) {
        return restore_legacy_lab(game, state, recovery_details);
    }
    const auto marker = owned_marker(game, marker_path);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());

    const auto core_backup = state / core_backup_name;
    const auto extension_backup = state / extension_backup_name;
    auto verified = verified_source(core_backup, marker.value().stock_core);
    if (!verified.has_value()) return verified;
    verified = verified_source(extension_backup, marker.value().stock_extension);
    if (!verified.has_value()) return verified;
    const auto helper_backup = state / helper_backup_name;
    if (marker.value().original_helper != "absent") {
        verified = verified_source(helper_backup, marker.value().original_helper);
        if (!verified.has_value()) return verified;
    }

    const auto active_core = game / active_core_name;
    const auto active_extension = game / extension_name;
    const auto active_helper = game / helper_name;
    const auto relayed_core = game / relayed_core_name;
    verified = require_known_current(active_core,
        {marker.value().stock_core, marker.value().forwarder}, false);
    if (!verified.has_value()) return verified;
    verified = require_known_current(active_extension,
        {marker.value().stock_extension, marker.value().optimized_extension}, false);
    if (!verified.has_value()) return verified;
    if (marker.value().original_helper == "absent") {
        verified = require_known_current(active_helper,
            {marker.value().optimized_helper}, true);
    } else {
        verified = require_known_current(active_helper,
            {marker.value().original_helper, marker.value().optimized_helper}, false);
    }
    if (!verified.has_value()) return verified;
    verified = require_known_current(relayed_core,
        {marker.value().optimized_core}, true);
    if (!verified.has_value()) return verified;

    auto restored = replace_verified(
        core_backup, active_core, marker.value().stock_core);
    if (!restored.has_value()) return restored;
    restored = replace_verified(extension_backup,
        active_extension, marker.value().stock_extension);
    if (!restored.has_value()) return restored;
    if (marker.value().original_helper == "absent") {
        restored = remove_known_file(
            active_helper, marker.value().optimized_helper);
    } else {
        restored = replace_verified(helper_backup,
            active_helper, marker.value().original_helper);
    }
    if (!restored.has_value()) return restored;

    const auto relayed_exists = inspected_exists(relayed_core);
    if (!relayed_exists.has_value()) return relayed_exists;
    if (relayed_exists.value()) {
        restored = remove_known_file(relayed_core, marker.value().optimized_core);
        if (!restored.has_value()) return restored;
    }
    std::error_code error;
    std::filesystem::remove(marker_path, error);
    if (error) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX transaction marker removal failed",
         static_cast<std::uint32_t>(error.value())});
    if (recovery_details != nullptr) {
        *recovery_details = L"Verified FleX Core, Extension and helper were restored.";
    }
    return Result<bool>::success(true);
}

Result<bool> recover_optimized_runtime_transaction(const std::filesystem::path& game,
                                 const std::filesystem::path& state,
                                 bool running,
                                 std::wstring* recovery_details) {
    if (recovery_details != nullptr) recovery_details->clear();
    const auto marker_path = state / marker_name;
    const auto exists = inspected_exists(marker_path);
    if (!exists.has_value()) return exists;
    if (!exists.value()) {
        const auto relay = inspected_exists(game / relayed_core_name);
        if (!relay.has_value()) return relay;
        return relay.value()
            ? Result<bool>::failure({ErrorCode::stale_data,
                L"A markerless FleX relay was retained for manual recovery", 0})
            : Result<bool>::success(false);
    }
    const auto legacy = is_legacy_marker(marker_path);
    if (!legacy.has_value()) return legacy;
    if (legacy.value()) {
        return restore_optimized_runtime_transaction(
            game, state, running, recovery_details);
    }
    const auto marker = owned_marker(game, marker_path);
    if (!marker.has_value()) return Result<bool>::failure(marker.error());
    if (marker.value().state == "installed") {
        const std::array checks{
            std::pair{game / active_core_name,
                      std::string_view{marker.value().forwarder}},
            std::pair{game / relayed_core_name,
                      std::string_view{marker.value().optimized_core}},
            std::pair{game / extension_name,
                      std::string_view{marker.value().optimized_extension}},
            std::pair{game / helper_name,
                      std::string_view{marker.value().optimized_helper}}};
        bool current = true;
        for (const auto& [path, expected] : checks) {
            const auto hash = hash_file(path);
            current = current && hash.has_value() && hash.value() == expected;
        }
        const std::array recovery_checks{
            std::pair{state / core_backup_name,
                      std::string_view{marker.value().stock_core}},
            std::pair{state / extension_backup_name,
                      std::string_view{marker.value().stock_extension}}};
        for (const auto& [path, expected] : recovery_checks) {
            const auto verified = verified_source(path, expected);
            current = current && verified.has_value();
        }
        if (marker.value().original_helper != "absent") {
            const auto helper_backup = verified_source(
                state / helper_backup_name, marker.value().original_helper);
            current = current && helper_backup.has_value();
        }
        if (current) return Result<bool>::success(false);
    }
    return restore_optimized_runtime_transaction(
        game, state, running, recovery_details);
}

}  // namespace kf2::flex
