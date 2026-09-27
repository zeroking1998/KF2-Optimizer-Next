#pragma once

#include <cstdint>
#include <optional>

namespace kf2::platform::windows::detail {

inline constexpr unsigned atomic_replace_attempt_count = 8;

[[nodiscard]] constexpr std::optional<std::uint32_t>
atomic_replace_backoff_after(unsigned attempt,
                             bool retryable) noexcept {
    if (!retryable || attempt + 1 >= atomic_replace_attempt_count) {
        return std::nullopt;
    }
    return 10U << attempt;
}

}  // namespace kf2::platform::windows::detail
