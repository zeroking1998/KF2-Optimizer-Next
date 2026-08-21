#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::game {

enum class DiscoverySource {
    steam_registry,
    steam_library,
    manual,
};

struct FileIdentity {
    std::uint64_t volume_serial{0};
    std::uint64_t file_index{0};
};

struct GameDiscoveryInput {
    std::vector<std::filesystem::path> steam_registry_candidates;
    std::vector<std::filesystem::path> steam_library_candidates;
    std::vector<std::filesystem::path> manual_candidates;
    std::filesystem::path config_root;
    std::filesystem::path allowed_config_parent;
};

struct GameInstallation {
    std::filesystem::path install_root;
    std::filesystem::path executable;
    std::filesystem::path config_root;
    DiscoverySource source{DiscoverySource::manual};
    FileIdentity executable_identity;
    std::size_t duplicate_candidates_ignored{0};
};

[[nodiscard]] Result<GameInstallation> validate_game_candidate(
    const std::filesystem::path& install_root,
    const std::filesystem::path& config_root,
    const std::filesystem::path& allowed_config_parent,
    DiscoverySource source);

[[nodiscard]] Result<GameInstallation> discover_game_installation(
    const GameDiscoveryInput& input);
[[nodiscard]] Result<GameDiscoveryInput> default_game_discovery_input();
[[nodiscard]] Result<std::vector<std::filesystem::path>>
parse_steam_library_folders(std::string_view document);

}  // namespace kf2::game
