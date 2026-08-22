#include "kf2/update/update_state.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::update {

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
    constexpr std::string_view prefix{
        "schema_version=1\nlast_check_unix_seconds="};
    if (!bytes.starts_with(prefix) || bytes.size() > 128) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument, L"Update state is invalid", 0});
    }
    std::string_view value{bytes.data() + prefix.size(),
                           bytes.size() - prefix.size()};
    if (!value.empty() && value.back() == '\n') value.remove_suffix(1);
    std::int64_t parsed = 0;
    const auto converted = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size() || parsed < 0) {
        return Result<PersistedUpdateState>::failure(
            {ErrorCode::invalid_argument, L"Update state timestamp is invalid", 0});
    }
    return Result<PersistedUpdateState>::success({parsed});
}

Result<bool> save_update_state(
    const std::filesystem::path& path, const PersistedUpdateState& state) {
    if (state.last_check_unix_seconds < 0) return Result<bool>::failure(
        {ErrorCode::invalid_argument, L"Update state timestamp is invalid", 0});
    const std::string bytes = "schema_version=1\nlast_check_unix_seconds=" +
        std::to_string(state.last_check_unix_seconds) + "\n";
    return platform::windows::atomic_replace_utf8(path, bytes);
}

}  // namespace kf2::update
