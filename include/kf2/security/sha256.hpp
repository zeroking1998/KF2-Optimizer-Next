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

#if defined(KF2_SHA256_TESTING)
using Sha256FileReadHook = void (*)(const std::filesystem::path&);
void set_sha256_file_read_hook_for_testing(Sha256FileReadHook hook) noexcept;
#endif

}  // namespace kf2::security
