#pragma once

#include <cstddef>
#include <functional>
#include <utility>

namespace pmon::util::hash {

inline std::size_t HashCombine(std::size_t left, std::size_t right) noexcept {
    return left ^ (right + 0x9e3779b9U + (left << 6U) + (left >> 2U));
}

template <typename Left, typename Right>
std::size_t DualHash(const Left& left, const Right& right) noexcept {
    return HashCombine(std::hash<Left>{}(left), std::hash<Right>{}(right));
}

}  // namespace pmon::util::hash
