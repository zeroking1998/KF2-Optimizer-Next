#include "kf2/flex/flex_lab.hpp"

#include <Windows.h>

#include <fstream>
#include <iterator>
#include <optional>
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

Result<std::string> hash_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<std::string>::failure(
        {ErrorCode::not_found, L"FleX laboratory file is missing", 0});
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    if (input.bad()) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"FleX laboratory file could not be read", 0});
    return security::sha256_hex(bytes);
}

Result<bool> copy_verified(const std::filesystem::path& source,
                           const std::filesystem::path& target) {
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX laboratory copy failed", GetLastError()});
    const auto a = hash_file(source); const auto b = hash_file(target);
    if (!a.has_value()) return Result<bool>::failure(a.error());
    if (!b.has_value()) return Result<bool>::failure(b.error());
    if (a.value() != b.value()) return Result<bool>::failure(
        {ErrorCode::io_failure, L"FleX laboratory copy hash mismatch", 0});
    return Result<bool>::success(true);
}

Result<bool> replace_verified(const std::filesystem::path& source,
                              const std::filesystem::path& target) {
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
    const auto source_hash = hash_file(source);
    const auto staged_hash = hash_file(temporary);
    if (!source_hash.has_value() || !staged_hash.has_value() ||
        source_hash.value() != staged_hash.value()) {
        cleanup();
        return Result<bool>::failure(
            {ErrorCode::io_failure, L"FleX laboratory staged hash mismatch", 0});
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
                installed_hash.value() == source_hash.value())
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

struct LabMarker {
    std::string state;
    std::string original_hash;
    std::string forwarder_hash;
};

Result<LabMarker> parse_marker(const std::filesystem::path& marker) {
    std::ifstream input(marker, std::ios::binary);
    if (!input) return Result<LabMarker>::failure(
        {ErrorCode::not_found, L"FleX laboratory transaction marker is missing", 0});
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    constexpr std::string_view prefix = "schema=2\nstate=";
    if (!bytes.starts_with(prefix)) return Result<LabMarker>::failure(
        {ErrorCode::invalid_argument, L"FleX laboratory transaction marker is invalid", 0});
    const auto state_end = bytes.find('\n', prefix.size());
    const auto original_prefix = bytes.find("original_sha256=", state_end);
    const auto forwarder_prefix = bytes.find("forwarder_sha256=", state_end);
    if (state_end == std::string::npos || original_prefix == std::string::npos ||
        forwarder_prefix == std::string::npos) return Result<LabMarker>::failure(
            {ErrorCode::invalid_argument, L"FleX laboratory marker fields are missing", 0});
    LabMarker parsed;
    parsed.state = bytes.substr(prefix.size(), state_end - prefix.size());
    const auto original_start = original_prefix + std::string_view{"original_sha256="}.size();
    const auto original_end = bytes.find('\n', original_start);
    const auto forwarder_start = forwarder_prefix + std::string_view{"forwarder_sha256="}.size();
    const auto forwarder_end = bytes.find('\n', forwarder_start);
    parsed.original_hash = bytes.substr(original_start, original_end - original_start);
    parsed.forwarder_hash = bytes.substr(forwarder_start, forwarder_end - forwarder_start);
    const auto valid_hash = [](const std::string& hash) {
        return hash.size() == 64 &&
            hash.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    if ((parsed.state != "installing" && parsed.state != "installed") ||
        !valid_hash(parsed.original_hash) || !valid_hash(parsed.forwarder_hash))
        return Result<LabMarker>::failure(
            {ErrorCode::invalid_argument, L"FleX laboratory marker hash is invalid", 0});
    return Result<LabMarker>::success(std::move(parsed));
}

Result<std::string> parse_legacy_original_hash(
    const std::filesystem::path& marker) {
    std::ifstream input(marker, std::ios::binary);
    if (!input) return Result<std::string>::failure(
        {ErrorCode::not_found, L"Legacy FleX marker is missing", 0});
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    constexpr std::string_view prefix = "schema=1\noriginal_sha256=";
    if (!bytes.starts_with(prefix)) return Result<std::string>::failure(
        {ErrorCode::invalid_argument, L"Legacy FleX marker is invalid", 0});
    const auto end = bytes.find('\n', prefix.size());
    const auto hash = bytes.substr(prefix.size(), end - prefix.size());
    if (hash.size() != 64 ||
        hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"Legacy FleX marker hash is invalid", 0});
    return Result<std::string>::success(hash);
}

}  // namespace

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
    if (std::filesystem::exists(marker) || std::filesystem::exists(original))
        return Result<LabTransactionResult>::failure({ErrorCode::stale_data,
            L"An unfinished FleX laboratory transaction requires recovery", 0});
    const auto original_hash = hash_file(active);
    const auto forwarder_hash = hash_file(o.forwarder_dll);
    if (!original_hash.has_value()) return Result<LabTransactionResult>::failure(original_hash.error());
    if (!forwarder_hash.has_value()) return Result<LabTransactionResult>::failure(forwarder_hash.error());
    auto copied = copy_verified(active, backup);
    if (!copied.has_value()) return Result<LabTransactionResult>::failure(copied.error());
    copied = copy_verified(active, original);
    if (!copied.has_value()) return Result<LabTransactionResult>::failure(copied.error());
    const auto marker_text = [&](std::string_view state) {
        return "schema=2\nstate=" + std::string{state} +
            "\noriginal_sha256=" + original_hash.value() +
            "\nforwarder_sha256=" + forwarder_hash.value() + "\n";
    };
    const auto marked = platform::windows::atomic_replace_utf8(
        marker, marker_text("installing"));
    if (!marked.has_value()) return Result<LabTransactionResult>::failure(marked.error());
    copied = replace_verified(o.forwarder_dll, active);
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
                                 const std::filesystem::path& state, bool running) {
    const auto checked = preflight(game, state, running);
    if (!checked.has_value()) return checked;
    const auto active = game / active_name;
    const auto original = game / original_name;
    const auto backup = state / backup_name;
    const auto marker = state / marker_name;
    if (!std::filesystem::exists(marker) && !std::filesystem::exists(original))
        return Result<bool>::failure(
            {ErrorCode::not_found, L"No active FleX laboratory transaction exists", 0});
    const auto source = std::filesystem::exists(backup) ? backup : original;
    if (!std::filesystem::exists(source)) return Result<bool>::failure(
        {ErrorCode::not_found, L"Verified FleX laboratory backup is missing", 0});
    std::optional<std::string> expected_hash;
    if (std::filesystem::exists(marker)) {
        const auto parsed = parse_marker(marker);
        if (!parsed.has_value()) return Result<bool>::failure(parsed.error());
        expected_hash = parsed.value().original_hash;
    }
    const auto source_before = hash_file(source);
    if (!source_before.has_value()) return Result<bool>::failure(source_before.error());
    if (expected_hash && source_before.value() != *expected_hash)
        return Result<bool>::failure(
            {ErrorCode::stale_data, L"FleX backup does not match the transaction marker", 0});
    auto copied = replace_verified(source, active);
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
    return Result<bool>::success(true);
}

Result<bool> recover_offline_lab(const std::filesystem::path& game,
                                 const std::filesystem::path& state, bool running) {
    const auto marker = state / marker_name;
    if (!std::filesystem::exists(marker) &&
        !std::filesystem::exists(game / original_name)) return Result<bool>::success(false);
    if (std::filesystem::exists(marker)) {
        const auto parsed = parse_marker(marker);
        if (parsed.has_value() && parsed.value().state == "installed") {
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
        // Schema 1 could remain after an older build had already restored the
        // active DLL but failed to remove its marker. Remove only this harmless
        // residue after both active runtime and backup match its pinned hash.
        if (!parsed.has_value() &&
            !std::filesystem::exists(game / original_name)) {
            const auto legacy = parse_legacy_original_hash(marker);
            const auto active_hash = hash_file(game / active_name);
            const auto backup_hash = hash_file(state / backup_name);
            if (legacy.has_value() && active_hash.has_value() &&
                backup_hash.has_value() &&
                active_hash.value() == legacy.value() &&
                backup_hash.value() == legacy.value()) {
                std::error_code ec;
                std::filesystem::remove(marker, ec);
                if (ec) return Result<bool>::failure(
                    {ErrorCode::io_failure,
                     L"Legacy FleX marker cleanup failed",
                     static_cast<std::uint32_t>(ec.value())});
                return Result<bool>::success(true);
            }
        }
    }
    return restore_offline_lab(game, state, running);
}

}  // namespace kf2::flex
