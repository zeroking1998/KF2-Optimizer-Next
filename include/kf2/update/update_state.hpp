#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::update {

enum class PersistedCheckResult {
    unknown,
    current,
    available,
};

struct PersistedUpdateState {
    std::int64_t last_check_unix_seconds{};
    PersistedCheckResult last_result{PersistedCheckResult::unknown};
    std::string available_version;
    std::string ignored_version;
};

[[nodiscard]] Result<PersistedUpdateState> load_update_state(
    const std::filesystem::path& path);
[[nodiscard]] Result<bool> save_update_state(
    const std::filesystem::path& path, const PersistedUpdateState& state);

}  // namespace kf2::update
