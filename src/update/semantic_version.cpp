#include "kf2/update/semantic_version.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace kf2::update {
namespace {

Result<std::uint32_t> parse_number(std::string_view value) {
    if (value.empty() || (value.size() > 1 && value.front() == '0')) {
        return Result<std::uint32_t>::failure(
            {ErrorCode::invalid_argument, L"Version number is invalid", 0});
    }
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return Result<std::uint32_t>::failure(
            {ErrorCode::invalid_argument, L"Version number is invalid", 0});
    }
    return Result<std::uint32_t>::success(parsed);
}

bool valid_identifier(std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-';
    });
}

bool numeric_identifier(std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

int compare_identifier(std::string_view left, std::string_view right) noexcept {
    const bool left_numeric = numeric_identifier(left);
    const bool right_numeric = numeric_identifier(right);
    if (left_numeric && right_numeric) {
        if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
        if (left == right) return 0;
        return left < right ? -1 : 1;
    }
    if (left_numeric != right_numeric) return left_numeric ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

}  // namespace

Result<SemanticVersion> parse_semantic_version(std::string_view text) {
    if (text.starts_with('v')) text.remove_prefix(1);
    if (text.empty() || text.size() > 128) {
        return Result<SemanticVersion>::failure(
            {ErrorCode::invalid_argument, L"Version is invalid", 0});
    }
    const auto plus = text.find('+');
    const std::string_view precedence = text.substr(0, plus);
    const std::string_view build = plus == std::string_view::npos
        ? std::string_view{} : text.substr(plus + 1);
    if (plus != std::string_view::npos) {
        if (build.empty()) {
            return Result<SemanticVersion>::failure(
                {ErrorCode::invalid_argument, L"Version metadata is invalid", 0});
        }
        std::size_t offset = 0;
        while (offset <= build.size()) {
            const auto end = build.find('.', offset);
            const auto identifier = build.substr(
                offset, end == std::string_view::npos ? build.size() - offset
                                                       : end - offset);
            if (!valid_identifier(identifier)) {
                return Result<SemanticVersion>::failure(
                    {ErrorCode::invalid_argument,
                     L"Version metadata is invalid", 0});
            }
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
    }

    const auto dash = precedence.find('-');
    const auto core = precedence.substr(0, dash);
    const auto first = core.find('.');
    const auto second = first == std::string_view::npos
        ? std::string_view::npos : core.find('.', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        core.find('.', second + 1) != std::string_view::npos) {
        return Result<SemanticVersion>::failure(
            {ErrorCode::invalid_argument,
             L"Version must contain major, minor and patch numbers", 0});
    }
    const auto major = parse_number(core.substr(0, first));
    const auto minor = parse_number(core.substr(first + 1, second - first - 1));
    const auto patch = parse_number(core.substr(second + 1));
    if (!major.has_value() || !minor.has_value() || !patch.has_value()) {
        return Result<SemanticVersion>::failure(
            {ErrorCode::invalid_argument, L"Version core is invalid", 0});
    }

    SemanticVersion parsed{major.value(), minor.value(), patch.value()};
    if (dash != std::string_view::npos) {
        const auto suffix = precedence.substr(dash + 1);
        std::size_t offset = 0;
        while (offset <= suffix.size()) {
            const auto end = suffix.find('.', offset);
            const auto identifier = suffix.substr(
                offset, end == std::string_view::npos ? suffix.size() - offset
                                                       : end - offset);
            if (!valid_identifier(identifier) ||
                (numeric_identifier(identifier) && identifier.size() > 1 &&
                 identifier.front() == '0')) {
                return Result<SemanticVersion>::failure(
                    {ErrorCode::invalid_argument,
                     L"Version prerelease identifier is invalid", 0});
            }
            parsed.prerelease.emplace_back(identifier);
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
    }
    parsed.canonical = std::string{text};
    return Result<SemanticVersion>::success(std::move(parsed));
}

int compare_semantic_versions(const SemanticVersion& left,
                              const SemanticVersion& right) noexcept {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    for (std::size_t index = 0;
         index < std::min(left.prerelease.size(), right.prerelease.size());
         ++index) {
        const int compared = compare_identifier(left.prerelease[index],
                                                right.prerelease[index]);
        if (compared != 0) return compared;
    }
    if (left.prerelease.size() == right.prerelease.size()) return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

}  // namespace kf2::update
