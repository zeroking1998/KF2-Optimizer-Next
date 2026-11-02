#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::flex {

enum class BinaryDeltaSource : std::uint8_t {
    original,
    literal,
};

struct BinaryDeltaOperation {
    BinaryDeltaSource source{};
    std::uint32_t offset{};
    std::uint32_t length{};
};

struct BinaryDeltaRecipe {
    std::string_view original_sha256;
    std::string_view target_sha256;
    std::size_t target_size{};
    std::span<const std::uint8_t> literals;
    std::span<const BinaryDeltaOperation> operations;
};

[[nodiscard]] const BinaryDeltaRecipe& optimized_core_recipe();
[[nodiscard]] const BinaryDeltaRecipe& optimized_extension_recipe();

[[nodiscard]] Result<std::vector<std::uint8_t>> apply_binary_delta(
    std::span<const std::uint8_t> original,
    std::span<const std::uint8_t> literals,
    std::span<const BinaryDeltaOperation> operations,
    std::size_t expected_size);

}  // namespace kf2::flex
