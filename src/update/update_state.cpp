#include "kf2/update/update_state.hpp"

#include <charconv>
#include <sstream>
#include <string>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/update/semantic_version.hpp"

namespace kf2::update {
namespace {

bool valid_cached_version(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    const auto parsed = parse_semantic_version(value);
    return parsed.has_value() && parsed.value().canonical == value;
}

Result<std::int64_t> parse_timestamp(std::string_view value) {
    std::int64_t parsed = 0;
    const auto converted = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size() || parsed < 0) {
        return Result<std::int64_t>::failure(
            {ErrorCode::invalid_argument,
             L"Update state timestamp is invalid", 0});
    }
    return Result<std::int64_t>::success(parsed);
}

Result<std::uint32_t> parse_failure_count(std::string_view value) {
    std::uint32_t parsed = 0;
    const auto converted = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size()) {
        return Result<std::uint32_t>::failure(
            {ErrorCode::invalid_argument,
             L"Update state failure count is invalid", 0});
    }
    return Result<std::uint32_t>::success(parsed);
}

}  // namespace

Result<PersistedUpdateState> load_update_state(
    const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (!error) return Result<PersistedUpdateState>::success({});
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::io_failure, L"Update state cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    const auto document =
        platform::windows::read_bounded_verified_file(path, 384);
    if (!document.has_value()) {
        return Result<PersistedUpdateState>::failure(document.error());
    }
    const auto& bytes = document.value();
    constexpr std::string_view legacy_prefix{
        "schema_version=1\nlast_check_unix_seconds="};
    if (bytes.starts_with(legacy_prefix) && bytes.size() <= 128) {
        std::string_view value{bytes.data() + legacy_prefix.size(),
                               bytes.size() - legacy_prefix.size()};
        if (!value.empty() && value.back() == '\n') value.remove_suffix(1);
        const auto parsed = parse_timestamp(value);
        return parsed.has_value()
            ? Result<PersistedUpdateState>::success(
                  {0, PersistedCheckResult::unknown, {}, {}})
            : Result<PersistedUpdateState>::failure(parsed.error());
    }
    std::istringstream stream{bytes};
    std::string schema;
    std::string timestamp_line;
    std::string attempt_line;
    std::string failure_count_line;
    std::string result_line;
    std::string version_line;
    std::string ignored_line;
    std::string extra;
    if (!std::getline(stream, schema) ||
        (schema != "schema_version=2" && schema != "schema_version=3") ||
        !std::getline(stream, timestamp_line) ||
        !timestamp_line.starts_with("last_check_unix_seconds=") ||
        (schema == "schema_version=3" &&
         (!std::getline(stream, attempt_line) ||
          !attempt_line.starts_with("last_attempt_unix_seconds=") ||
          !std::getline(stream, failure_count_line) ||
          !failure_count_line.starts_with("automatic_failure_count="))) ||
        !std::getline(stream, result_line) ||
        !result_line.starts_with("last_result=") ||
        !std::getline(stream, version_line) ||
        !version_line.starts_with("available_version=") ||
        !std::getline(stream, ignored_line) ||
        !ignored_line.starts_with("ignored_version=") ||
        std::getline(stream, extra)) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument, L"Update state is invalid", 0});
    }
    constexpr std::string_view timestamp_key{"last_check_unix_seconds="};
    constexpr std::string_view attempt_key{"last_attempt_unix_seconds="};
    constexpr std::string_view failure_count_key{"automatic_failure_count="};
    constexpr std::string_view result_key{"last_result="};
    constexpr std::string_view version_key{"available_version="};
    constexpr std::string_view ignored_key{"ignored_version="};
    const auto timestamp = parse_timestamp(
        std::string_view{timestamp_line}.substr(timestamp_key.size()));
    if (!timestamp.has_value()) {
        return Result<PersistedUpdateState>::failure(timestamp.error());
    }
    std::int64_t last_attempt = 0;
    std::uint32_t failure_count = 0;
    if (schema == "schema_version=3") {
        const auto parsed_attempt = parse_timestamp(
            std::string_view{attempt_line}.substr(attempt_key.size()));
        if (!parsed_attempt.has_value()) {
            return Result<PersistedUpdateState>::failure(
                parsed_attempt.error());
        }
        const auto parsed_failure_count = parse_failure_count(
            std::string_view{failure_count_line}.substr(
                failure_count_key.size()));
        if (!parsed_failure_count.has_value()) {
            return Result<PersistedUpdateState>::failure(
                parsed_failure_count.error());
        }
        last_attempt = parsed_attempt.value();
        failure_count = parsed_failure_count.value();
        if (failure_count > 0 && last_attempt == 0) {
            return Result<PersistedUpdateState>::failure(
                {ErrorCode::invalid_argument,
                 L"Update state retry metadata is invalid", 0});
        }
    }
    const auto result_text =
        std::string_view{result_line}.substr(result_key.size());
    const auto version =
        std::string_view{version_line}.substr(version_key.size());
    const auto ignored =
        std::string_view{ignored_line}.substr(ignored_key.size());
    PersistedCheckResult result = PersistedCheckResult::unknown;
    if (result_text == "current" && version.empty()) {
        result = PersistedCheckResult::current;
    } else if (result_text == "available" && valid_cached_version(version)) {
        result = PersistedCheckResult::available;
    } else if (result_text != "unknown" || !version.empty()) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument,
             L"Cached update result is invalid", 0});
    }
    if (!ignored.empty() && !valid_cached_version(ignored)) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument,
             L"Ignored update version is invalid", 0});
    }
    return Result<PersistedUpdateState>::success(
        {timestamp.value(), result, std::string{version},
         std::string{ignored}, last_attempt, failure_count});
}

Result<bool> save_update_state(
    const std::filesystem::path& path, const PersistedUpdateState& state) {
    if (state.last_check_unix_seconds < 0 ||
        state.last_attempt_unix_seconds < 0 ||
        (state.automatic_failure_count > 0 &&
         state.last_attempt_unix_seconds == 0)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Update state timestamp is invalid", 0});
    }
    std::string result;
    switch (state.last_result) {
        case PersistedCheckResult::unknown: result = "unknown"; break;
        case PersistedCheckResult::current: result = "current"; break;
        case PersistedCheckResult::available: result = "available"; break;
    }
    if ((state.last_result == PersistedCheckResult::available &&
         !valid_cached_version(state.available_version)) ||
        (state.last_result != PersistedCheckResult::available &&
         !state.available_version.empty())) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Cached update result is invalid", 0});
    }
    if (!state.ignored_version.empty() &&
        !valid_cached_version(state.ignored_version)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Ignored update version is invalid", 0});
    }
    const std::string bytes =
        "schema_version=3\nlast_check_unix_seconds=" +
        std::to_string(state.last_check_unix_seconds) +
        "\nlast_attempt_unix_seconds=" +
        std::to_string(state.last_attempt_unix_seconds) +
        "\nautomatic_failure_count=" +
        std::to_string(state.automatic_failure_count) +
        "\nlast_result=" + result +
        "\navailable_version=" + state.available_version +
        "\nignored_version=" + state.ignored_version + "\n";
    return platform::windows::atomic_replace_utf8(path, bytes);
}

}  // namespace kf2::update
