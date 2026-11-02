#pragma once

#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::flex {

struct PreparedOptimizedRuntime {
    std::filesystem::path core;
    std::filesystem::path extension;
    std::string core_sha256;
    std::string extension_sha256;
};

[[nodiscard]] Result<PreparedOptimizedRuntime> prepare_optimized_runtime(
    const std::filesystem::path& stock_core,
    const std::filesystem::path& stock_extension,
    const std::filesystem::path& state_directory);

}  // namespace kf2::flex
