#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::security {

struct PackageIntegrityAudit {
    bool managed_package{false};
    bool verified{false};
    std::size_t verified_files{0};
    std::string source_identity;
    std::wstring message;
};

struct PackageRepairResult {
    std::size_t repaired_files{0};
    std::size_t already_valid_files{0};
    bool restart_required{false};
};

[[nodiscard]] Result<PackageIntegrityAudit> audit_package_integrity(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity);

[[nodiscard]] Result<PackageRepairResult> repair_package_from_directory(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& source_directory,
    std::string_view expected_source_identity);

}  // namespace kf2::security
