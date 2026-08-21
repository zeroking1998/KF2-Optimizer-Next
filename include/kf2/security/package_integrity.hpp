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

[[nodiscard]] Result<PackageIntegrityAudit> audit_package_integrity(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity);

}  // namespace kf2::security
