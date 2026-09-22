#pragma once

#include <algorithm>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

[[nodiscard]] inline std::filesystem::path extended_length_path(
    const std::filesystem::path& path) {
    auto value = path.wstring();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.starts_with(L"\\\\?\\")) {
        return std::filesystem::path{value};
    }
    if (value.starts_with(L"\\\\")) {
        return std::filesystem::path{L"\\\\?\\UNC\\" + value.substr(2)};
    }
    if (value.size() >= 3 && value[1] == L':' && value[2] == L'\\') {
        return std::filesystem::path{L"\\\\?\\" + value};
    }
    return std::filesystem::path{value};
}
[[nodiscard]] Result<std::filesystem::path> executable_path();
[[nodiscard]] Result<std::filesystem::path> executable_directory();
[[nodiscard]] Result<std::filesystem::path> temporary_directory();
[[nodiscard]] Result<std::filesystem::path> local_app_data_directory();
[[nodiscard]] bool probe_writable_directory(
    const std::filesystem::path& directory) noexcept;

}  // namespace kf2::platform::windows
