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

[[nodiscard]] Result<RuntimeAudit> audit_runtime(
    const std::filesystem::path& path,
    bool allow_transaction_original_name = false);

}  // namespace kf2::flex
