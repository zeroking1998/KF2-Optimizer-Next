#include "kf2/config/adaptive_locks.hpp"

#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace kf2::config {
namespace {

bool safe_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 96) return false;
    for (const unsigned char character : name) {
        if (!std::isalnum(character) && character != '_') return false;
    }
    return true;
}

std::optional<optimizer::ManualLockState> parse_lock(
    std::string_view value) noexcept {
    using optimizer::ManualLockState;
    if (value == "AUTO") return ManualLockState::automatic;
    if (value == "LOCK_CURRENT") return ManualLockState::lock_current;
    if (value == "LOCK_MINIMUM") return ManualLockState::lock_minimum;
    if (value == "LOCK_MAXIMUM") return ManualLockState::lock_maximum;
    if (value == "MANUAL_VALUE") return ManualLockState::manual_value;
    return std::nullopt;
}

}  // namespace

std::string_view adaptive_lock_name(
    optimizer::ManualLockState state) noexcept {
    using optimizer::ManualLockState;
    switch (state) {
        case ManualLockState::automatic: return "AUTO";
        case ManualLockState::lock_current: return "LOCK_CURRENT";
        case ManualLockState::lock_minimum: return "LOCK_MINIMUM";
        case ManualLockState::lock_maximum: return "LOCK_MAXIMUM";
        case ManualLockState::manual_value: return "MANUAL_VALUE";
    }
    return "AUTO";
}

Result<AdaptiveLocks> parse_adaptive_locks(std::string_view text) {
    AdaptiveLocks locks;
    std::set<std::string> seen;
    bool schema_seen = false;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find('\n', offset);
        std::string line{text.substr(
            offset, end == std::string_view::npos ? text.size() - offset
                                                   : end - offset)};
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            const auto equals = line.find('=');
            if (equals == std::string::npos || equals == 0) {
                return Result<AdaptiveLocks>::failure({
                    ErrorCode::invalid_argument,
                    L"Adaptive lock line is malformed", 0});
            }
            const std::string name = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if (!seen.insert(name).second) {
                return Result<AdaptiveLocks>::failure({
                    ErrorCode::invalid_argument,
                    L"Adaptive lock is duplicated", 0});
            }
            if (name == "schema_version") {
                if (value != "1") {
                    return Result<AdaptiveLocks>::failure({
                        ErrorCode::invalid_argument,
                        L"Adaptive lock schema is unsupported", 0});
                }
                schema_seen = true;
            } else {
                const auto state = parse_lock(value);
                const auto* setting = optimizer::find_adaptive_setting(name);
                if (!safe_name(name) || !state || !setting ||
                    setting->role != optimizer::AdaptiveRole::adaptive_knob) {
                    return Result<AdaptiveLocks>::failure({
                        ErrorCode::invalid_argument,
                        L"Adaptive lock target or state is invalid", 0});
                }
                locks.emplace(name, *state);
            }
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    if (!schema_seen) {
        return Result<AdaptiveLocks>::failure({
            ErrorCode::invalid_argument,
            L"Adaptive lock schema is missing", 0});
    }
    return Result<AdaptiveLocks>::success(std::move(locks));
}

std::string serialize_adaptive_locks(const AdaptiveLocks& locks) {
    std::ostringstream output;
    output << "schema_version=1\n";
    for (const auto& [name, state] : locks) {
        output << name << '=' << adaptive_lock_name(state) << '\n';
    }
    return output.str();
}

}  // namespace kf2::config
