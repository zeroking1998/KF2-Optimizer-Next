#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

[[nodiscard]] Result<std::filesystem::path> executable_directory();
[[nodiscard]] Result<std::filesystem::path> local_app_data_directory();
[[nodiscard]] bool probe_writable_directory(
    const std::filesystem::path& directory) noexcept;

}  // namespace kf2::platform::windows
