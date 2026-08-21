#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstdint>

namespace pmon::util {

inline std::int64_t GetCurrentTimestamp() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

inline double GetTimestampPeriodSeconds() noexcept {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0 ? 1.0 / static_cast<double>(frequency.QuadPart)
                                  : 0.0;
}

inline double TimestampDeltaToSeconds(std::int64_t start, std::int64_t end,
                                      double period) noexcept {
    return static_cast<double>(end - start) * period;
}

class PrecisionWaiter {
public:
    explicit PrecisionWaiter(double = 0.001) noexcept {}

    void Wait(double seconds, bool = false) noexcept {
        const auto milliseconds = static_cast<DWORD>(
            std::max(0.0, seconds) * 1000.0);
        if (milliseconds > 0) Sleep(milliseconds);
    }
};

}  // namespace pmon::util
