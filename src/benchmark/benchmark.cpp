#include "kf2/benchmark/benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <limits>
#include <sstream>

namespace kf2::benchmark {
namespace {

bool finite_positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

double percent_change(double before, double after) {
    return (after - before) * 100.0 / before;
}

Result<double> number(std::string_view text) {
    try {
        std::size_t consumed = 0;
        const double value = std::stod(std::string{text}, &consumed);
        if (consumed != text.size() || !finite_positive(value)) {
            return Result<double>::failure(
                {ErrorCode::invalid_argument, L"Benchmark metric is invalid", 0});
        }
        return Result<double>::success(value);
    } catch (...) {
        return Result<double>::failure(
            {ErrorCode::invalid_argument, L"Benchmark metric is invalid", 0});
    }
}

std::string hex_encode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char character : value) {
        result.push_back(digits[character >> 4]);
        result.push_back(digits[character & 0x0f]);
    }
    return result;
}

std::optional<std::string> hex_decode(std::string_view value) {
    if (value.size() % 2 != 0 || value.size() > 1024) return std::nullopt;
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    std::string result;
    result.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = nibble(value[index]);
        const int low = nibble(value[index + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return result;
}

std::vector<std::string_view> fields(std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find('|', start);
        result.push_back(value.substr(start, end == std::string_view::npos
            ? value.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

}  // namespace

Result<bool> validate(const Run& run) {
    if (run.scenario_id.empty() || run.configuration_id.empty() ||
        run.scenario_id.find_first_of("\r\n=") != std::string::npos ||
        run.configuration_id.find_first_of("\r\n=") != std::string::npos ||
        !finite_positive(run.average_fps) || !finite_positive(run.one_percent_low_fps) ||
        !finite_positive(run.p95_frame_time_ms)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"Benchmark run is invalid", 0});
    }
    return Result<bool>::success(true);
}

ComparisonResult compare(const Run& baseline, const Run& candidate) {
    if (!validate(baseline).has_value() || !validate(candidate).has_value() ||
        baseline.scenario_id != candidate.scenario_id) {
        return {Comparison::incompatible, 0.0, 0.0, 0.0,
                L"Runs must be valid and use the same scenario"};
    }
    ComparisonResult result;
    result.average_fps_change_percent =
        percent_change(baseline.average_fps, candidate.average_fps);
    result.one_percent_low_change_percent =
        percent_change(baseline.one_percent_low_fps, candidate.one_percent_low_fps);
    result.p95_frame_time_change_percent =
        percent_change(baseline.p95_frame_time_ms, candidate.p95_frame_time_ms);
    if (result.average_fps_change_percent >= 3.0 &&
        result.one_percent_low_change_percent >= 0.0 &&
        result.p95_frame_time_change_percent <= 0.0 &&
        candidate.stutter_count <= baseline.stutter_count) {
        result.outcome = Comparison::improved;
        result.reason = L"FPS improved without a frame-time, 1% low, or stutter regression";
    } else if (result.average_fps_change_percent <= -3.0 ||
               result.one_percent_low_change_percent <= -3.0 ||
               result.p95_frame_time_change_percent >= 5.0 ||
               candidate.stutter_count > baseline.stutter_count + 2) {
        result.outcome = Comparison::regressed;
        result.reason = L"One or more protected performance metrics regressed";
    } else {
        result.outcome = Comparison::inconclusive;
        result.reason = L"The measured change is below the acceptance threshold";
    }
    return result;
}

std::string serialize(const Run& run) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "scenario=" << run.scenario_id << '\n'
           << "configuration=" << run.configuration_id << '\n'
           << "average_fps=" << run.average_fps << '\n'
           << "one_percent_low_fps=" << run.one_percent_low_fps << '\n'
           << "p95_frame_time_ms=" << run.p95_frame_time_ms << '\n'
           << "stutter_count=" << run.stutter_count << '\n';
    return output.str();
}

Result<Run> parse(std::string_view text) {
    std::map<std::string, std::string> values;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto end = text.find('\n', offset);
        const auto line = text.substr(offset, end == std::string_view::npos
                                                  ? text.size() - offset : end - offset);
        const auto equal = line.find('=');
        if (equal == std::string_view::npos || equal == 0 ||
            !values.emplace(std::string{line.substr(0, equal)},
                            std::string{line.substr(equal + 1)}).second) {
            return Result<Run>::failure(
                {ErrorCode::invalid_argument, L"Benchmark record is malformed", 0});
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    const auto average = number(values["average_fps"]);
    const auto low = number(values["one_percent_low_fps"]);
    const auto p95 = number(values["p95_frame_time_ms"]);
    try {
        std::size_t consumed = 0;
        const auto stutters = std::stoull(values.at("stutter_count"), &consumed);
        if (consumed != values.at("stutter_count").size()) throw std::invalid_argument{""};
        Run run{values.at("scenario"), values.at("configuration"),
                average.value(), low.value(), p95.value(), stutters};
        if (!average.has_value() || !low.has_value() || !p95.has_value() ||
            !validate(run).has_value()) throw std::invalid_argument{""};
        return Result<Run>::success(std::move(run));
    } catch (...) {
        return Result<Run>::failure(
            {ErrorCode::invalid_argument, L"Benchmark record is invalid", 0});
    }
}

std::string serialize_history(std::span<const HistoryEntry> entries) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << "version=1\n";
    for (const auto& entry : entries) {
        output << "entry=" << entry.sequence << '|'
               << hex_encode(entry.run.scenario_id) << '|'
               << hex_encode(entry.run.configuration_id) << '|'
               << entry.run.average_fps << '|'
               << entry.run.one_percent_low_fps << '|'
               << entry.run.p95_frame_time_ms << '|'
               << entry.run.stutter_count << '\n';
    }
    return output.str();
}

Result<std::vector<HistoryEntry>> parse_history(std::string_view text) {
    if (text.size() > 256 * 1024 || !text.starts_with("version=1\n")) {
        return Result<std::vector<HistoryEntry>>::failure(
            {ErrorCode::invalid_argument, L"Benchmark history version is invalid", 0});
    }
    std::vector<HistoryEntry> result;
    std::size_t offset = std::string_view{"version=1\n"}.size();
    std::uint64_t previous_sequence = 0;
    while (offset < text.size()) {
        const auto end = text.find('\n', offset);
        const auto line = text.substr(offset, end == std::string_view::npos
            ? text.size() - offset : end - offset);
        if (line.empty()) {
            if (end == std::string_view::npos) break;
            offset = end + 1;
            continue;
        }
        if (!line.starts_with("entry=") || result.size() >= 64) {
            return Result<std::vector<HistoryEntry>>::failure(
                {ErrorCode::invalid_argument, L"Benchmark history is malformed", 0});
        }
        const auto parts = fields(line.substr(6));
        if (parts.size() != 7) {
            return Result<std::vector<HistoryEntry>>::failure(
                {ErrorCode::invalid_argument, L"Benchmark history entry is malformed", 0});
        }
        try {
            std::size_t consumed = 0;
            const auto sequence = std::stoull(std::string{parts[0]}, &consumed);
            if (consumed != parts[0].size() || sequence == 0 ||
                sequence <= previous_sequence) throw std::invalid_argument{""};
            const auto scenario = hex_decode(parts[1]);
            const auto configuration = hex_decode(parts[2]);
            const auto average = number(parts[3]);
            const auto low = number(parts[4]);
            const auto p95 = number(parts[5]);
            consumed = 0;
            const auto stutters = std::stoull(std::string{parts[6]}, &consumed);
            if (!scenario || !configuration || !average.has_value() ||
                !low.has_value() || !p95.has_value() ||
                consumed != parts[6].size()) throw std::invalid_argument{""};
            Run run{*scenario, *configuration, average.value(), low.value(),
                    p95.value(), stutters};
            if (!validate(run).has_value()) throw std::invalid_argument{""};
            result.push_back({sequence, std::move(run)});
            previous_sequence = sequence;
        } catch (...) {
            return Result<std::vector<HistoryEntry>>::failure(
                {ErrorCode::invalid_argument, L"Benchmark history entry is invalid", 0});
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    return Result<std::vector<HistoryEntry>>::success(std::move(result));
}

std::vector<HistoryEntry> append_history(std::vector<HistoryEntry> entries,
                                         Run run,
                                         std::size_t maximum_entries) {
    maximum_entries = std::clamp<std::size_t>(maximum_entries, 1, 64);
    if (!entries.empty() &&
        entries.back().sequence == std::numeric_limits<std::uint64_t>::max()) {
        for (std::size_t index = 0; index < entries.size(); ++index) {
            entries[index].sequence = static_cast<std::uint64_t>(index + 1);
        }
    }
    std::uint64_t sequence = entries.empty() ? 1 : entries.back().sequence + 1;
    entries.push_back({sequence, std::move(run)});
    if (entries.size() > maximum_entries) {
        entries.erase(entries.begin(), entries.begin() +
            static_cast<std::ptrdiff_t>(entries.size() - maximum_entries));
    }
    return entries;
}

}  // namespace kf2::benchmark
