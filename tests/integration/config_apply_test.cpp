#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "kf2/backup/backup_store.hpp"
#include "kf2/config/apply_transaction.hpp"

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
    fs::remove_all(root);
    return EXIT_SUCCESS;
}
