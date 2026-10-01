#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "kf2/backup/backup_store.hpp"
#include "kf2/backup/restore_transaction.hpp"
#include "kf2/config/apply_transaction.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
}

HANDLE lock_without_read_sharing(const std::filesystem::path& path) {
    return CreateFileW(path.c_str(), GENERIC_READ,
                       FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

std::filesystem::path concurrent_target;

void change_target_before_commit() {
    write_bytes(concurrent_target, "concurrent-user-edit");
}

std::filesystem::path failed_write_target;
std::filesystem::path failed_rollback_target;
HANDLE write_lock = INVALID_HANDLE_VALUE;
HANDLE rollback_lock = INVALID_HANDLE_VALUE;
std::filesystem::path rollback_readback_target;
int rollback_read_count{};
int rollback_read_trigger = 3;
void change_rollback_readback(const std::filesystem::path& target) {
    if (target == rollback_readback_target &&
        ++rollback_read_count == rollback_read_trigger) {
        write_bytes(target, "external-change-after-rollback");
    }
}

void lock_transaction_targets(
    kf2::platform::windows::AtomicFileMutationStage stage,
    const std::filesystem::path& target) {
    if (stage != kf2::platform::windows::AtomicFileMutationStage::conditional_after_validation ||
        target != failed_write_target) return;
    write_lock = CreateFileW(target.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (!failed_rollback_target.empty()) {
        rollback_lock = CreateFileW(failed_rollback_target.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    const auto config_root = root / L"Config";
    const auto target = config_root / L"KFEngine.ini";
    const std::string original = "[Engine.Engine]\r\nMaxSmoothedFrameRate=62\r\n";
    const std::string proposed = "[Engine.Engine]\r\nMaxSmoothedFrameRate=90\r\n";
    write_bytes(target, original);

    kf2::config::ConfigPreview preview;
    preview.config_root = config_root;
    preview.files.push_back({L"KFEngine.ini", original, proposed});
    kf2::backup::BackupStore store{root / L"State"};
    const auto applied = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(applied.has_value());
    CHECK(read_bytes(target) == proposed);
    CHECK(fs::exists(applied.value().backup.manifest_path));
    CHECK(applied.value().backup.snapshots.size() == 1);
    CHECK(read_bytes(applied.value().backup.snapshots[0].object_path) == original);
    CHECK(store.verify(applied.value().backup).has_value());

    write_bytes(target, original);
    const auto second = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(second.has_value());
    CHECK(second.value().backup.snapshots[0].object_path ==
          applied.value().backup.snapshots[0].object_path);

    write_bytes(target, original + "; changed after preview\r\n");
    const auto drifted = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(!drifted.has_value());
    CHECK(drifted.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(target).ends_with("; changed after preview\r\n"));

    const auto concurrent_root = root / L"ConcurrentApply";
    const auto concurrent_first = concurrent_root / L"KFEngine.ini";
    const auto concurrent_middle = concurrent_root / L"KFGame.ini";
    const auto concurrent_later =
        concurrent_root / L"KFSystemSettings.ini";
    write_bytes(concurrent_first, "first-original");
    write_bytes(concurrent_middle, "middle-original");
    write_bytes(concurrent_later, "later-original");
    kf2::config::ConfigPreview concurrent_preview;
    concurrent_preview.config_root = concurrent_root;
    concurrent_preview.files.push_back(
        {L"KFEngine.ini", "first-original", "first-proposed"});
    concurrent_preview.files.push_back(
        {L"KFGame.ini", "middle-original", "middle-proposed"});
    concurrent_preview.files.push_back(
        {L"KFSystemSettings.ini", "later-original", "later-proposed"});
    kf2::backup::BackupStore concurrent_store{
        root / L"ConcurrentApplyState"};
    concurrent_target = concurrent_middle;
    kf2::config::set_apply_commit_hook_for_testing(
        &change_target_before_commit);
    const auto concurrent = kf2::config::apply_preview(
        concurrent_preview, concurrent_store, {.game_running = false});
    kf2::config::set_apply_commit_hook_for_testing(nullptr);
    CHECK(!concurrent.has_value());
    CHECK(concurrent.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(concurrent_first) == "first-original");
    CHECK(read_bytes(concurrent_middle) == "concurrent-user-edit");
    CHECK(read_bytes(concurrent_later) == "later-original");
    const auto concurrent_backups = concurrent_store.list_backups();
    CHECK(concurrent_backups.has_value());
    CHECK(concurrent_backups.value().size() == 1);
    CHECK(read_bytes(concurrent_backups.value().front().journal_path)
              .find("state=complete") != std::string::npos);
    const auto concurrent_recovery = kf2::backup::recover_transactions(
        concurrent_store, concurrent_root);
    CHECK(concurrent_recovery.has_value());
    CHECK(concurrent_recovery.value().outcome ==
          kf2::backup::RecoveryOutcome::clean);
    CHECK(concurrent_recovery.value().transactions_recovered == 0);
    CHECK(read_bytes(concurrent_middle) == "concurrent-user-edit");

    // Real sharing locks fail a later write and optionally one rollback.
    // An older successful write must still be restored after that failure.
    for (int mode = 0; mode < 3; ++mode) {
        const bool fail_rollback = mode == 1;
        const bool mismatch = mode == 2;
        const bool debt = fail_rollback || mismatch;
        const auto rollback_root = root / (L"Rollback" + std::to_wstring(mode));
        const auto first = rollback_root / L"KFEngine.ini";
        const auto middle = rollback_root / L"KFGame.ini";
        const auto later = rollback_root / L"KFSystemSettings.ini";
        write_bytes(first, "first-original");
        write_bytes(middle, "middle-original");
        write_bytes(later, "later-original");
        auto rollback_preview = concurrent_preview;
        rollback_preview.config_root = rollback_root;
        kf2::backup::BackupStore rollback_store{root / (L"RollbackState" + std::to_wstring(mode))};
        failed_write_target = later;
        failed_rollback_target = fail_rollback ? middle : fs::path{};
        rollback_readback_target = mismatch ? first : fs::path{};
        rollback_read_count = 0;
        rollback_read_trigger = 3;
        kf2::platform::windows::set_bounded_read_hook_for_testing(change_rollback_readback);
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(lock_transaction_targets);
        const auto failed = kf2::config::apply_preview(rollback_preview, rollback_store, {});
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(nullptr);
        kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
        const bool locks_created = write_lock != INVALID_HANDLE_VALUE &&
            (!fail_rollback || rollback_lock != INVALID_HANDLE_VALUE);
        if (write_lock != INVALID_HANDLE_VALUE) CloseHandle(write_lock);
        if (rollback_lock != INVALID_HANDLE_VALUE) CloseHandle(rollback_lock);
        write_lock = rollback_lock = INVALID_HANDLE_VALUE;
        CHECK(locks_created);
        CHECK(!failed.has_value());
        CHECK(failed.error().code == (debt ? kf2::ErrorCode::recovery_required : kf2::ErrorCode::io_failure));
        CHECK(read_bytes(first) == (mismatch ? "external-change-after-rollback" : "first-original"));
        CHECK(read_bytes(middle) == (fail_rollback ? "middle-proposed" : "middle-original"));
        CHECK(read_bytes(later) == "later-original");
        const auto backups = rollback_store.list_backups();
        CHECK(backups.has_value() && backups.value().size() == 1);
        CHECK(read_bytes(backups.value().front().journal_path).find(
            debt ? "state=replacement_started" : "state=complete") != std::string::npos);
        if (mismatch) {
            const auto conflict = kf2::backup::recover_transactions(rollback_store, rollback_root);
            CHECK(!conflict.has_value());
            CHECK(conflict.error().code == kf2::ErrorCode::stale_data);
            CHECK(read_bytes(first) == "external-change-after-rollback");
            write_bytes(first, "first-original");
        }
        // A new store models the next startup reading durable state.
        kf2::backup::BackupStore restarted_store{rollback_store.state_root()};
        if (fail_rollback) {
            rollback_readback_target = middle;
            rollback_read_count = 0;
            rollback_read_trigger = 2;
            kf2::platform::windows::set_bounded_read_hook_for_testing(change_rollback_readback);
            const auto failed_recovery = kf2::backup::recover_transactions(
                restarted_store, rollback_root);
            kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
            CHECK(!failed_recovery.has_value());
            CHECK(failed_recovery.error().code == kf2::ErrorCode::recovery_required);
            CHECK(read_bytes(middle) == "external-change-after-rollback");
            CHECK(read_bytes(backups.value().front().journal_path).find(
                "state=replacement_started") != std::string::npos);
            write_bytes(middle, "middle-original");
        }
        const auto recovered = kf2::backup::recover_transactions(restarted_store, rollback_root);
        CHECK(recovered.has_value());
        CHECK(recovered.value().transactions_recovered == (debt ? 1U : 0U));
        CHECK(read_bytes(first) == "first-original");
        CHECK(read_bytes(middle) == "middle-original");
        CHECK(read_bytes(later) == "later-original");
    }

    write_bytes(target, original);
    const auto running = kf2::config::apply_preview(
        preview, store, {.game_running = true});
    CHECK(!running.has_value());
    CHECK(read_bytes(target) == original);

    const auto unchanged_target = config_root / L"KFGame.ini";
    const auto changed_target = config_root / L"KFSystemSettings.ini";
    write_bytes(unchanged_target, "same");
    write_bytes(changed_target, "before");
    kf2::config::ConfigPreview mixed;
    mixed.config_root = config_root;
    mixed.files.push_back({L"KFGame.ini", "same", "same"});
    mixed.files.push_back({L"KFSystemSettings.ini", "before", "after"});
    const auto mixed_applied = kf2::config::apply_preview(
        mixed, store, {.game_running = false});
    CHECK(mixed_applied.has_value());
    CHECK(mixed_applied.value().files_changed == 1);
    CHECK(read_bytes(unchanged_target) == "same");
    CHECK(read_bytes(changed_target) == "after");

    const auto locked_root = root / L"LockedApply";
    const auto locked_target = locked_root / L"KFEngine.ini";
    write_bytes(locked_target, "");
    kf2::config::ConfigPreview locked_preview;
    locked_preview.config_root = locked_root;
    locked_preview.files.push_back({L"KFEngine.ini", "", "replacement"});
    kf2::backup::BackupStore locked_store{root / L"LockedApplyState"};
    HANDLE locked_file = lock_without_read_sharing(locked_target);
    CHECK(locked_file != INVALID_HANDLE_VALUE);
    const auto unreadable_apply = kf2::config::apply_preview(
        locked_preview, locked_store, {.game_running = false});
    CHECK(!unreadable_apply.has_value());
    CHECK(unreadable_apply.error().message.find(L"read") !=
          std::wstring::npos);
    CHECK(CloseHandle(locked_file) != FALSE);
    CHECK(read_bytes(locked_target).empty());
    const auto locked_backups = locked_store.list_backups();
    CHECK(locked_backups.has_value());
    CHECK(locked_backups.value().empty());

    const auto retained = store.prune_verified({.keep_latest = 2});
    CHECK(retained.has_value());
    const auto listed_after_prune = store.list_backups();
    CHECK(listed_after_prune.has_value());
    CHECK(listed_after_prune.value().size() <= 2);

    auto foreign = preview;
    foreign.files[0].relative_path = L"..\\foreign.ini";
    const auto rejected = kf2::config::apply_preview(
        foreign, store, {.game_running = false});
    CHECK(!rejected.has_value());
    CHECK(read_bytes(target) == original);

    const auto no_space = kf2::config::apply_preview(
        preview, store, {.game_running = false, .available_bytes = 1});
    CHECK(!no_space.has_value());
    CHECK(no_space.error().code == kf2::ErrorCode::io_failure);
    CHECK(no_space.error().message.find(L"Insufficient space") !=
          std::wstring::npos);
    CHECK(read_bytes(target) == original);

    fs::resize_file(target, 16U * 1024U * 1024U + 1U);
    const auto oversized_live_ini = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(!oversized_live_ini.has_value());
    CHECK(oversized_live_ini.error().code == kf2::ErrorCode::access_denied);
    fs::remove_all(root);
    return EXIT_SUCCESS;
}
