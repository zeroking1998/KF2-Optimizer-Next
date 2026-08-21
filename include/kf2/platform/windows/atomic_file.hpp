#pragma once

#include <filesystem>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

[[nodiscard]] Result<bool> atomic_replace_utf8(
    const std::filesystem::path& target,
    std::string_view bytes);
[[nodiscard]] Result<std::filesystem::path> quarantine_regular_file(
    const std::filesystem::path& source,
    std::wstring_view suffix = L".corrupt",
    std::size_t maximum_retained = 4);

}  // namespace kf2::platform::windows
