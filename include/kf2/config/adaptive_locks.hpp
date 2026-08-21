#pragma once

#include <map>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/optimizer/adaptive_registry.hpp"

namespace kf2::config {

using AdaptiveLocks =
    std::map<std::string, optimizer::ManualLockState, std::less<>>;

[[nodiscard]] Result<AdaptiveLocks> parse_adaptive_locks(
    std::string_view text);
[[nodiscard]] std::string serialize_adaptive_locks(
    const AdaptiveLocks& locks);
[[nodiscard]] std::string_view adaptive_lock_name(
    optimizer::ManualLockState state) noexcept;

}  // namespace kf2::config
