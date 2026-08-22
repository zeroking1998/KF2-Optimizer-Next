#pragma once

#include <cstdint>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::update {

struct PersistedUpdateState {
    std::int64_t last_check_unix_seconds{};
};

[[nodiscard]] Result<PersistedUpdateState> load_update_state(
    const std::filesystem::path& path);
[[nodiscard]] Result<bool> save_update_state(
    const std::filesystem::path& path, const PersistedUpdateState& state);

}  // namespace kf2::update
