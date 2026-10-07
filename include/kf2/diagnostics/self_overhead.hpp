#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace kf2::diagnostics {

// Cumulative work clocks use nanoseconds; missing/reset clocks never become
// fabricated zeroes. Called only by the enabled, one-second Debug overlay poll.
inline std::optional<double> work_ms_per_second(
    std::optional<std::uint64_t> previous,
    std::optional<std::uint64_t> current, std::uint64_t elapsed_ns) noexcept {
    if (!previous || !current || *current < *previous || elapsed_ns == 0) {
        return std::nullopt;
    }
    return static_cast<double>(*current - *previous) * 1000.0 /
        static_cast<double>(elapsed_ns);
}

struct SelfOverheadPresentation final {
    // CPU, RAM, I/O, threads; resource-worker CPU, telemetry UI elapsed,
    // DXGI-worker CPU, Governor/overlay/script/FleX elapsed; whole app CPU.
    // Elapsed scopes are not CPU attribution and must never be added to CPU.
    // Additional process GPU, local/non-local GPU memory and private commit.
    std::array<std::wstring, 16> values{
        L"—", L"—", L"—", L"—", L"—", L"—", L"—", L"—", L"—", L"—", L"—", L"—",
        L"—", L"—", L"—", L"—"};
    bool operator==(const SelfOverheadPresentation&) const = default;
};
}  // namespace kf2::diagnostics
