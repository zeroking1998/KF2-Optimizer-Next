#include "kf2/config/apply_transaction.hpp"

#include <Windows.h>

#include <fstream>
#include <iterator>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::config {
namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const auto& component : path) {
        if (component == L".." || component == L".") return false;
    }
    return true;
}

bool safe_target(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(file, &information) &&
                      information.nNumberOfLinks == 1;
    CloseHandle(file);
    return safe;
}

Result<bool> write_journal(const backup::BackupSet& backup,
                           std::string_view state) {
    return platform::windows::atomic_replace_utf8(
        backup.journal_path,
        "version=1\nstate=" + std::string{state} + "\nid=" + backup.id + "\n");
}

Result<std::filesystem::path> existing_space_probe_path(
    std::filesystem::path path) {
    std::error_code error;
    while (!path.empty()) {
        if (std::filesystem::exists(path, error)) {
            if (!error) return Result<std::filesystem::path>::success(std::move(path));
            break;
        }
        if (error) break;
        const auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return Result<std::filesystem::path>::failure(
        {ErrorCode::io_failure, L"Free space probe path cannot be resolved",
         static_cast<std::uint32_t>(error.value())});
}

}  // namespace

Result<ApplyResult> apply_preview(const ConfigPreview& preview,
                                  backup::BackupStore& store,
                                  const ApplyPreconditions& preconditions) {
    if (preconditions.game_running) {
        return Result<ApplyResult>::failure(
            {ErrorCode::access_denied, L"KF2 must be stopped before applying config", 0});
    }
    if (preview.config_root.empty() || preview.files.empty()) {
        return Result<ApplyResult>::failure(
            {ErrorCode::invalid_argument, L"Configuration preview is empty", 0});
    }
    std::uintmax_t required_bytes = 4096;
    for (const auto& file : preview.files) {
        if (!safe_relative_path(file.relative_path)) {
            return Result<ApplyResult>::failure(
                {ErrorCode::access_denied, L"Configuration target is outside allowlist", 0});
        }
        const auto target = preview.config_root / file.relative_path;
        if (!safe_target(target)) {
            return Result<ApplyResult>::failure(
                {ErrorCode::access_denied, L"Configuration target identity is unsafe", 0});
        }
        if (read_bytes(target) != file.original_bytes) {
            return Result<ApplyResult>::failure(
                {ErrorCode::stale_data, L"Configuration changed after preview", 0});
        }
        required_bytes += file.original_bytes.size() + file.proposed_bytes.size();
    }
    std::uintmax_t available = 0;
    if (preconditions.available_bytes) {
        available = *preconditions.available_bytes;
    } else {
        auto probe = existing_space_probe_path(store.state_root());
        if (!probe.has_value()) return Result<ApplyResult>::failure(probe.error());
        std::error_code space_error;
        available = std::filesystem::space(probe.value(), space_error).available;
        if (space_error) {
            return Result<ApplyResult>::failure(
                {ErrorCode::io_failure, L"Free space cannot be determined",
                 static_cast<std::uint32_t>(space_error.value())});
        }
    }
    if (available < required_bytes) {
        return Result<ApplyResult>::failure(
            {ErrorCode::io_failure, L"Insufficient space for safe configuration apply", 0});
    }
    auto backup = store.create(preview);
    if (!backup.has_value()) return Result<ApplyResult>::failure(backup.error());
    auto verified = store.verify(backup.value());
    if (!verified.has_value()) return Result<ApplyResult>::failure(verified.error());
    auto journal = write_journal(backup.value(), "replacement_started");
    if (!journal.has_value()) return Result<ApplyResult>::failure(journal.error());

    std::vector<const PreviewFile*> written_files;
    written_files.reserve(preview.files.size());
    for (const auto& file : preview.files) {
        if (file.original_bytes == file.proposed_bytes) continue;
        auto written = platform::windows::atomic_replace_utf8(
            preview.config_root / file.relative_path, file.proposed_bytes);
        if (!written.has_value()) {
            for (const auto* rollback : written_files) {
                static_cast<void>(platform::windows::atomic_replace_utf8(
                    preview.config_root / rollback->relative_path,
                    rollback->original_bytes));
            }
            return Result<ApplyResult>::failure(written.error());
        }
        written_files.push_back(&file);
    }
    for (const auto& file : preview.files) {
        if (read_bytes(preview.config_root / file.relative_path) != file.proposed_bytes) {
            for (const auto& rollback : preview.files) {
                static_cast<void>(platform::windows::atomic_replace_utf8(
                    preview.config_root / rollback.relative_path,
                    rollback.original_bytes));
            }
            return Result<ApplyResult>::failure(
                {ErrorCode::io_failure, L"Applied configuration verification failed", 0});
        }
    }
    journal = write_journal(backup.value(), "verification_complete");
    if (!journal.has_value()) return Result<ApplyResult>::failure(journal.error());
    journal = write_journal(backup.value(), "complete");
    if (!journal.has_value()) return Result<ApplyResult>::failure(journal.error());
    return Result<ApplyResult>::success(
        {std::move(backup.value()), written_files.size()});
}

}  // namespace kf2::config
