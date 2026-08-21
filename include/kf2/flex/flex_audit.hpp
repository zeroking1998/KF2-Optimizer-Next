#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::flex {

struct RuntimeAudit {
    std::filesystem::path path;
    std::uint64_t size_bytes{};
    std::string sha256;
    std::wstring file_version;
    std::vector<std::string> exports;
    std::vector<std::string> missing_required_exports;
    bool abi_compatible{false};
    bool exact_known_runtime{false};
};

enum class HookGateState {
    blocked_abi,
    blocked_online_uncertain,
    blocked_game_running,
    blocked_stability_quarantine,
    eligible_offline_lab,
};

struct HookGateEvidence {
    bool abi_compatible{false};
    bool exact_runtime_identity{false};
    bool game_running{false};
    bool explicit_offline_opt_in{false};
    bool platform_offline_confirmed{false};
    bool replacement_stability_qualified{false};
};

struct HookGateDecision {
    HookGateState state{HookGateState::blocked_abi};
    bool writable{false};
    std::wstring reason;
};

[[nodiscard]] Result<RuntimeAudit> audit_runtime(
    const std::filesystem::path& path,
    bool allow_transaction_original_name = false);
[[nodiscard]] std::string serialize_audit_json(const RuntimeAudit& audit);
[[nodiscard]] HookGateDecision evaluate_hook_gate(
    const HookGateEvidence& evidence);

}  // namespace kf2::flex
