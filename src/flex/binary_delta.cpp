#include "kf2/flex/binary_delta.hpp"

#include <limits>

namespace kf2::flex {

Result<std::vector<std::uint8_t>> apply_binary_delta(
    std::span<const std::uint8_t> original,
    std::span<const std::uint8_t> literals,
    std::span<const BinaryDeltaOperation> operations,
    std::size_t expected_size) {
    if (expected_size == 0 ||
        expected_size > static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {
        return Result<std::vector<std::uint8_t>>::failure(
            {ErrorCode::invalid_argument,
             L"Optimized FleX recipe target size is invalid", 0});
    }

    std::vector<std::uint8_t> output;
    output.reserve(expected_size);
    for (const auto& operation : operations) {
        const auto input = operation.source == BinaryDeltaSource::original
            ? original
            : literals;
        const auto offset = static_cast<std::size_t>(operation.offset);
        const auto length = static_cast<std::size_t>(operation.length);
        if (length == 0 || offset > input.size() ||
            length > input.size() - offset ||
            length > expected_size - output.size()) {
            return Result<std::vector<std::uint8_t>>::failure(
                {ErrorCode::invalid_argument,
                 L"Optimized FleX recipe operation is invalid", 0});
        }
        output.insert(output.end(), input.begin() +
            static_cast<std::ptrdiff_t>(offset), input.begin() +
            static_cast<std::ptrdiff_t>(offset + length));
    }
    if (output.size() != expected_size) {
        return Result<std::vector<std::uint8_t>>::failure(
            {ErrorCode::invalid_argument,
             L"Optimized FleX recipe produced an unexpected size", 0});
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

}  // namespace kf2::flex
