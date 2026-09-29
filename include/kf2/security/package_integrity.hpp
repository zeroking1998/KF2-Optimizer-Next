#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

struct PackageIntegrityFile {
    std::string relative_path;
    std::string sha256;
};

struct PackageIntegrityManifest {
    std::string source_identity;
    std::string document;
    std::vector<PackageIntegrityFile> files;
};

#if defined(KF2_PACKAGE_INTEGRITY_TESTING)
enum class PackageRepairFaultInjection {
    none,
    after_replacement,
    final_verification,
    rollback_failure,
};

void set_package_repair_fault_for_testing(
    PackageRepairFaultInjection fault,
    std::size_t after_replacements = 0) noexcept;
#endif

[[nodiscard]] std::span<const std::string_view>
managed_package_payload_paths() noexcept;

[[nodiscard]] Result<PackageIntegrityManifest> load_package_integrity_manifest(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity);

[[nodiscard]] Result<std::string> package_source_identity(
    const std::filesystem::path& executable_directory);

[[nodiscard]] Result<PackageIntegrityAudit> audit_package_integrity(
    const std::filesystem::path& executable_directory,
    std::string_view expected_source_identity);

[[nodiscard]] Result<PackageRepairResult> repair_package_from_directory(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& source_directory,
    std::string_view expected_source_identity);

}  // namespace kf2::security
