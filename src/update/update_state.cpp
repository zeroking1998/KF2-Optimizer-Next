#include "kf2/update/update_state.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::update {
namespace {

bool valid_cached_version(std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '.' ||
                   character == '+' || character == '-';
        });
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
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<PersistedUpdateState>::failure(
        {ErrorCode::io_failure, L"Update state cannot be read", 0});
    const std::string bytes{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};
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
    if (bytes.size() > 256) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument, L"Update state is invalid", 0});
    }
    std::istringstream stream{bytes};
    std::string schema;
    std::string timestamp_line;
    std::string result_line;
    std::string version_line;
    std::string ignored_line;
    std::string extra;
    if (!std::getline(stream, schema) || schema != "schema_version=2" ||
        !std::getline(stream, timestamp_line) ||
        !timestamp_line.starts_with("last_check_unix_seconds=") ||
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
    constexpr std::string_view result_key{"last_result="};
    constexpr std::string_view version_key{"available_version="};
    constexpr std::string_view ignored_key{"ignored_version="};
    const auto timestamp = parse_timestamp(
        std::string_view{timestamp_line}.substr(timestamp_key.size()));
    if (!timestamp.has_value()) {
        return Result<PersistedUpdateState>::failure(timestamp.error());
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
         std::string{ignored}});
}

Result<bool> save_update_state(
    const std::filesystem::path& path, const PersistedUpdateState& state) {
    if (state.last_check_unix_seconds < 0) {
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
        "schema_version=2\nlast_check_unix_seconds=" +
        std::to_string(state.last_check_unix_seconds) +
        "\nlast_result=" + result +
        "\navailable_version=" + state.available_version +
        "\nignored_version=" + state.ignored_version + "\n";
    return platform::windows::atomic_replace_utf8(path, bytes);
}

}  // namespace kf2::update
