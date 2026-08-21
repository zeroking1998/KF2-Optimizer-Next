#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::security {

[[nodiscard]] Result<std::string> sha256_hex(std::string_view bytes);
[[nodiscard]] Result<std::string> sha256_file_hex(
    const std::filesystem::path& path,
    std::uint64_t maximum_bytes = 64ULL * 1024ULL * 1024ULL);

}  // namespace kf2::security
