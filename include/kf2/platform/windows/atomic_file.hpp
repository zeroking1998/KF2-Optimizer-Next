#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

[[nodiscard]] Result<std::string> read_bounded_verified_file(
    const std::filesystem::path& path,
    std::uintmax_t maximum_bytes);

#if defined(KF2_ATOMIC_FILE_TESTING)
using BoundedReadHook = void (*)(const std::filesystem::path&);
void set_bounded_read_hook_for_testing(BoundedReadHook hook) noexcept;
#endif

[[nodiscard]] Result<bool> atomic_replace_utf8(
    const std::filesystem::path& target,
    std::string_view bytes);
[[nodiscard]] Result<bool> atomic_replace_utf8_if_unchanged(
    const std::filesystem::path& target,
    std::string_view expected_bytes,
    std::string_view replacement_bytes);
[[nodiscard]] Result<std::filesystem::path> quarantine_regular_file(
    const std::filesystem::path& source,
    std::wstring_view suffix = L".corrupt",
    std::size_t maximum_retained = 4);

}  // namespace kf2::platform::windows
