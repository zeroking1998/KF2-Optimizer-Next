#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::app {

enum class StateLocationKind {
    portable,
    per_user_fallback,
};

struct StateLocation {
    std::filesystem::path root;
    StateLocationKind kind{StateLocationKind::portable};
    std::wstring reason;
};

using WritableProbe = std::function<bool(const std::filesystem::path&)>;

[[nodiscard]] Result<StateLocation> choose_state_location(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& local_app_data,
    const WritableProbe& is_writable);

}  // namespace kf2::app
