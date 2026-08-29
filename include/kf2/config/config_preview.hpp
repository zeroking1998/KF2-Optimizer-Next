#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "kf2/config/ini_document.hpp"
#include "kf2/config/kf2_catalog.hpp"
#include "kf2/game/game_discovery.hpp"

namespace kf2::config {

enum class ChangeSource { adaptive, explicit_user };
enum class PreviewState { ready, unchanged };

struct RequestedChange {
    SettingId id;
    SettingValue value;
    ChangeSource source{ChangeSource::explicit_user};
    std::wstring reason;
};

struct PreviewItem {
    SettingId id;
    std::filesystem::path relative_path;
    std::wstring section;
    std::wstring key;
    SettingValue before;
    SettingValue after;
    ChangeSource source;
    std::wstring reason;
    PreviewState state{PreviewState::ready};
    bool restore_available{true};
    bool existed_before{true};
};

struct PreviewFile {
    std::filesystem::path relative_path;
    std::string original_bytes;
    std::string proposed_bytes;
};

struct ConfigPreview {
    std::filesystem::path config_root;
    std::vector<PreviewItem> items;
    std::vector<PreviewFile> files;
};

[[nodiscard]] Result<ConfigPreview> build_preview(
    const game::GameInstallation& installation,
    const std::vector<RequestedChange>& requests,
    const std::map<std::filesystem::path, IniDocument>& documents);

}  // namespace kf2::config
