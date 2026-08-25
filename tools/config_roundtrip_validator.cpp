#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>

#include "kf2/backup/restore_transaction.hpp"
#include "kf2/config/config_preview.hpp"
#include "kf2/config/setting_catalog.hpp"

namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

int fail(const kf2::Error& error) {
    std::wcerr << L"FAIL: " << error.message << L" (native "
               << error.native_code << L")\n";
    return 1;
}

kf2::config::SettingValue alternate_value(
    const kf2::config::SettingDefinition& definition,
    const kf2::config::SettingValue& current) {
    using kf2::config::SettingType;
    using kf2::config::SettingValue;
    if (definition.type == SettingType::boolean) {
        return SettingValue{!std::get<bool>(current)};
    }
    if (definition.type == SettingType::integer) {
        const int value = std::get<int>(current);
        const int minimum = static_cast<int>(definition.minimum);
        const int maximum = static_cast<int>(definition.maximum);
        return SettingValue{value == minimum ? maximum : minimum};
    }
    const double value = std::get<double>(current);
    return SettingValue{value == definition.minimum
                            ? definition.maximum
                            : definition.minimum};
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: KF2ConfigRoundtrip <config-copy> <state-root>\n";
        return 2;
    }
    const std::filesystem::path config_root{argv[1]};
    const std::filesystem::path state_root{argv[2]};
    const std::array<std::filesystem::path, 3> relative_paths{
        L"KFEngine.ini", L"KFGame.ini", L"KFSystemSettings.ini"};
    std::map<std::filesystem::path, std::string> originals;
    std::map<std::filesystem::path, kf2::config::IniDocument> documents;
    for (const auto& relative : relative_paths) {
        const auto bytes = read_bytes(config_root / relative);
        if (bytes.empty()) {
            std::wcerr << L"BLOCKED: " << relative.wstring()
                       << L" is missing or empty\n";
            return 3;
        }
        auto document = kf2::config::IniDocument::parse(bytes);
        if (!document.has_value()) return fail(document.error());
        originals.emplace(relative, bytes);
        documents.emplace(relative, std::move(document.value()));
    }

    const auto catalog_values = kf2::config::read_catalog_values(config_root);
    if (!catalog_values.has_value()) return fail(catalog_values.error());
    std::vector<kf2::config::RequestedChange> changes;
    changes.reserve(kf2::config::all_settings().size());
    for (const auto& definition : kf2::config::all_settings()) {
        const auto current = catalog_values.value().find(definition.id);
        if (current == catalog_values.value().end()) {
            if (!definition.insert_if_missing ||
                definition.type != kf2::config::SettingType::boolean) {
                std::wcerr << L"FAIL: catalog value is missing\n";
                return 1;
            }
            changes.push_back({
                definition.id, true,
                kf2::config::ChangeSource::explicit_user,
                L"Complete catalog roundtrip validation"});
            continue;
        }
        changes.push_back({
            definition.id, alternate_value(definition, current->second),
            kf2::config::ChangeSource::explicit_user,
            L"Complete catalog roundtrip validation"});
    }
    // Keep the coupled smoothing range valid while still changing both ends.
    for (auto& change : changes) {
        if (change.id == kf2::config::SettingId::minimum_smooth_frame_rate) {
            change.value = 1;
        } else if (change.id == kf2::config::SettingId::target_fps) {
            change.value = 240;
        }
    }
    const std::size_t expected_catalog_items = kf2::config::all_settings().size();
    if (changes.size() != expected_catalog_items) {
        std::wcerr << L"FAIL: expected " << expected_catalog_items
                   << L" complete catalog changes, got "
                   << changes.size() << L'\n';
        return 1;
    }
    kf2::game::GameInstallation installation;
    installation.config_root = config_root;
    auto preview = kf2::config::build_preview(
        installation, changes, documents);
    if (!preview.has_value()) return fail(preview.error());
    if (preview.value().items.size() != expected_catalog_items ||
        preview.value().files.size() != relative_paths.size()) {
        std::wcerr << L"FAIL: complete catalog preview is incomplete\n";
        return 1;
    }
    kf2::backup::BackupStore store{state_root};
    auto applied = kf2::config::apply_preview(
        preview.value(), store, {.game_running = false});
    if (!applied.has_value()) return fail(applied.error());
    auto restored = kf2::backup::restore_backup(
        store, applied.value().backup.id, config_root,
        {.game_running = false});
    if (!restored.has_value()) return fail(restored.error());
    for (const auto& [relative, original] : originals) {
        if (read_bytes(config_root / relative) != original) {
            std::wcerr << L"FAIL: " << relative.wstring()
                       << L" restore was not byte-identical\n";
            return 1;
        }
    }
    std::wcout << L"PASS: all " << expected_catalog_items
               << L" catalog settings previewed across 3 INIs; "
                  L"apply/verified-backup/restore is byte-identical\n";
    return 0;
}
