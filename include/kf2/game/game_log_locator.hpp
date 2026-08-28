#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "kf2/core/result.hpp"

namespace kf2::game {

struct ActiveGameLog final {
    std::filesystem::path path;
    std::uint64_t last_write_filetime{0};
};

[[nodiscard]] Result<std::optional<ActiveGameLog>> find_active_game_log(
    const std::filesystem::path& log_directory,
    std::uint64_t process_start_filetime);

}  // namespace kf2::game
