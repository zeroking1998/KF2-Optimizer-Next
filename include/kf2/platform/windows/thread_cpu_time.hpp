#pragma once

#include <Windows.h>
#include <cstdint>
#include <optional>

namespace kf2::platform::windows {
// Windows-accounted CPU time, not elapsed time or a cycle-rate estimate.
inline std::optional<std::uint64_t> thread_cpu_ns(void* thread) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!thread || !GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
        return std::nullopt;
    }
    const auto ticks = [](FILETIME time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) |
            time.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) * 100ULL;
}
}  // namespace kf2::platform::windows
