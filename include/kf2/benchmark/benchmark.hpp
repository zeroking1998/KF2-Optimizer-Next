#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::benchmark {

struct Run {
    std::string scenario_id;
    std::string configuration_id;
    double average_fps{0.0};
    double one_percent_low_fps{0.0};
    double p95_frame_time_ms{0.0};
    std::uint64_t stutter_count{0};
};

enum class Comparison { improved, regressed, inconclusive, incompatible };

struct ComparisonResult {
    Comparison outcome{Comparison::inconclusive};
    double average_fps_change_percent{0.0};
    double one_percent_low_change_percent{0.0};
    double p95_frame_time_change_percent{0.0};
    std::wstring reason;
};

struct HistoryEntry {
    std::uint64_t sequence{0};
    Run run;
};

[[nodiscard]] Result<bool> validate(const Run& run);
[[nodiscard]] ComparisonResult compare(const Run& baseline, const Run& candidate);
[[nodiscard]] std::string serialize(const Run& run);
[[nodiscard]] Result<Run> parse(std::string_view text);
[[nodiscard]] std::string serialize_history(
    std::span<const HistoryEntry> entries);
[[nodiscard]] Result<std::vector<HistoryEntry>> parse_history(
    std::string_view text);
[[nodiscard]] std::vector<HistoryEntry> append_history(
    std::vector<HistoryEntry> entries, Run run,
    std::size_t maximum_entries = 64);

}  // namespace kf2::benchmark
