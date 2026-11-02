#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <system_error>
#include <vector>

#include "kf2/config/config_preview.hpp"
#include "kf2/core/result.hpp"

namespace kf2::backup {

#if defined(KF2_BACKUP_RECOVERY_TESTING)
using BackupStatusHook = bool (*)(const std::filesystem::path&,
                                  std::error_code&);
using BackupReadHook = Result<std::string> (*)(const std::filesystem::path&,
                                             std::uintmax_t);
using BackupManifestWriteHook = void (*)(std::ostream&);
using BackupWriteTimeHook = std::filesystem::file_time_type (*)(
    const std::filesystem::path&, std::error_code&);
void set_backup_status_hook_for_testing(BackupStatusHook hook) noexcept;
void set_backup_read_hook_for_testing(BackupReadHook hook) noexcept;
void set_backup_manifest_write_hook_for_testing(BackupManifestWriteHook hook) noexcept;
void set_backup_write_time_hook_for_testing(BackupWriteTimeHook hook) noexcept;
#endif

struct FileSnapshot {
    std::filesystem::path relative_path;
    std::uintmax_t size{0};
    std::string sha256;
    std::uintmax_t desired_size{0};
    std::string desired_sha256;
    std::filesystem::path object_path;
    std::uint64_t volume_serial{0};
    std::uint64_t file_index{0};
};

struct BackupSet {
    std::string id;
    std::filesystem::path config_root;
    std::filesystem::path manifest_path;
    std::filesystem::path journal_path;
    std::vector<FileSnapshot> snapshots;
};

class BackupStore final {
public:
    explicit BackupStore(std::filesystem::path state_root);
    [[nodiscard]] Result<BackupSet> create(const config::ConfigPreview& preview);
    [[nodiscard]] Result<BackupSet> create_standalone(
        const config::ConfigPreview& preview);
    [[nodiscard]] Result<bool> verify(const BackupSet& backup) const;
    [[nodiscard]] Result<BackupSet> load_backup(std::string_view id) const;
    [[nodiscard]] Result<std::vector<BackupSet>> list_backups() const;
    struct RetentionPolicy { std::size_t keep_latest{1}; };
    [[nodiscard]] Result<std::size_t> prune_verified(RetentionPolicy policy);
    [[nodiscard]] const std::filesystem::path& state_root() const noexcept;

private:
    enum class LoadFailure { operational, corrupt };
    [[nodiscard]] Result<BackupSet> load_backup(
        std::string_view id, LoadFailure& failure) const;
    std::filesystem::path state_root_;
};

}  // namespace kf2::backup
