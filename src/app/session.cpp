#include "kf2/app/session.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::app {
namespace {

struct ParsedMarker {
    SessionIdentity identity;
    bool clean{false};
};

template <typename Integer>
bool parse_integer(const std::string& value, Integer& output) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

Result<ParsedMarker> parse_marker(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream input{text};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0 ||
            !values.emplace(line.substr(0, equals), line.substr(equals + 1)).second) {
            return Result<ParsedMarker>::failure(
                {ErrorCode::invalid_argument, L"Session marker is malformed", 0});
        }
    }

    ParsedMarker marker;
    int version = 0;
    if (values.size() != 4 || !values.contains("version") ||
        !values.contains("pid") || !values.contains("process_start_id") ||
        !values.contains("clean_shutdown") ||
        !parse_integer(values["version"], version) || version != 1 ||
        !parse_integer(values["pid"], marker.identity.pid) ||
        !parse_integer(values["process_start_id"], marker.identity.process_start_id) ||
        (values["clean_shutdown"] != "true" &&
         values["clean_shutdown"] != "false")) {
        return Result<ParsedMarker>::failure(
            {ErrorCode::invalid_argument, L"Session marker fields are invalid", 0});
    }
    marker.clean = values["clean_shutdown"] == "true";
    return Result<ParsedMarker>::success(marker);
}

std::string serialize_marker(SessionIdentity identity, bool clean) {
    std::ostringstream output;
    output << "version=1\npid=" << identity.pid << "\nprocess_start_id="
           << identity.process_start_id << "\nclean_shutdown="
           << (clean ? "true" : "false") << '\n';
    return output.str();
}

}  // namespace

SessionGuard::SessionGuard(std::filesystem::path marker_path,
                           SessionIdentity identity, bool previous_unclean)
    : marker_path_{std::move(marker_path)},
      identity_{identity},
      previous_unclean_{previous_unclean} {}

Result<SessionGuard> SessionGuard::start(
    const std::filesystem::path& marker_path,
    SessionIdentity identity) {
    if (marker_path.empty() || identity.pid == 0 || identity.process_start_id == 0) {
        return Result<SessionGuard>::failure(
            {ErrorCode::invalid_argument, L"Session start arguments are invalid", 0});
    }

    bool previous_unclean = false;
    if (std::filesystem::exists(marker_path)) {
        std::string text;
        {
            std::ifstream input(marker_path, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>{input},
                        std::istreambuf_iterator<char>{});
        }
        const auto parsed = parse_marker(text);
        if (parsed.has_value()) {
            previous_unclean = !parsed.value().clean;
        } else {
            previous_unclean = true;
            const auto quarantined =
                platform::windows::quarantine_regular_file(marker_path);
            if (!quarantined.has_value()) {
                return Result<SessionGuard>::failure(
                    {ErrorCode::io_failure, L"Corrupt session marker quarantine failed",
                     quarantined.error().native_code});
            }
        }
    }

    const auto written = platform::windows::atomic_replace_utf8(
        marker_path, serialize_marker(identity, false));
    if (!written.has_value()) {
        return Result<SessionGuard>::failure(written.error());
    }
    return Result<SessionGuard>::success(
        SessionGuard{marker_path, identity, previous_unclean});
}

bool SessionGuard::previous_session_unclean() const noexcept {
    return previous_unclean_;
}

Result<bool> SessionGuard::mark_clean() {
    return platform::windows::atomic_replace_utf8(
        marker_path_, serialize_marker(identity_, true));
}

}  // namespace kf2::app
