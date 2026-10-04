#include "kf2/security/release_package_repair.hpp"

#include <Windows.h>

#include <atomic>
#include <utility>

#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/update/github_release_client.hpp"
#include "kf2/update/semantic_version.hpp"

namespace kf2::security {
namespace {

std::atomic<unsigned long> temporary_sequence{0};

struct TemporaryRepairFiles {
    std::filesystem::path root;
    ~TemporaryRepairFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

std::wstring widen_ascii(std::string_view value) {
    return {value.begin(), value.end()};
}

Result<std::filesystem::path> new_repair_work_root(
    const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute()) return Result<std::filesystem::path>::failure(
        {ErrorCode::invalid_argument, L"Auto Repair working directory is invalid", 0});
    const auto root = path.wstring().size() >= MAX_PATH
        ? platform::windows::extended_length_path(path) : path;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const DWORD attributes = GetFileAttributesW(
        platform::windows::extended_length_path(root).c_str());
    if (error || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair working directory is unsafe or unavailable", 0});
    }
    for (unsigned int attempt = 0; attempt < 64; ++attempt) {
        const auto candidate = root / (L"release-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(temporary_sequence.fetch_add(1) + 1));
        if (!std::filesystem::exists(candidate, error) && !error) {
            // The shared preparation path creates this directory exclusively.
            return Result<std::filesystem::path>::success(candidate);
        }
        if (error) break;
    }
    return Result<std::filesystem::path>::failure(
        {ErrorCode::io_failure, L"Auto Repair could not allocate a temporary path", 0});
}

}  // namespace

Result<ReleaseRepairPlan> exact_release_repair_plan(
    std::string_view installed_version) {
    const auto version = update::parse_semantic_version(installed_version);
    const auto repository = update::official_release_repository();
    constexpr std::string_view prefix{"https://github.com/"};
    const auto slug = repository.starts_with(prefix)
        ? repository.substr(prefix.size()) : std::string_view{};
    const auto slash = slug.find('/');
    if (!version.has_value() || version.value().canonical != installed_version ||
        installed_version.find_first_not_of(
            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-") !=
            std::string_view::npos || repository.size() > 240 ||
        repository.find_first_of("?#\\") != std::string_view::npos ||
        repository.find("..") != std::string_view::npos ||
        slash == std::string_view::npos || slash == 0 || slash + 1 >= slug.size() ||
        slug.find('/', slash + 1) != std::string_view::npos) {
        return Result<ReleaseRepairPlan>::failure(
            {ErrorCode::invalid_argument,
             L"Installed build cannot be mapped to an exact official GitHub release", 0});
    }
    ReleaseRepairPlan plan;
    plan.tag = L"v" + widen_ascii(installed_version);
    plan.asset_name = L"KF2OptimizerNext-" + plan.tag + L"-win64.zip";
    plan.url = widen_ascii(repository) + L"/releases/download/" +
        plan.tag + L"/" + plan.asset_name;
    return Result<ReleaseRepairPlan>::success(std::move(plan));
}

Result<PackageRepairResult> download_and_repair_release_package(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity) {
    return download_and_repair_release_package_with_operations(
        executable_directory, working_directory, installed_version,
        expected_source_identity,
        {.query_release = update::query_exact_official_github_release,
         .prepare_package = update::prepare_update_package});
}

Result<PackageRepairResult> download_and_repair_release_package_with_operations(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,
    std::string_view installed_version,
    std::string_view expected_source_identity,
    const ReleaseRepairOperations& operations) {
    const auto plan = exact_release_repair_plan(installed_version);
    if (!plan.has_value()) return Result<PackageRepairResult>::failure(plan.error());
    if (!operations.query_release || !operations.prepare_package) {
        return Result<PackageRepairResult>::failure(
            {ErrorCode::invalid_argument, L"Auto Repair operations are incomplete", 0});
    }
    const auto release = operations.query_release(installed_version);
    if (!release.has_value()) return Result<PackageRepairResult>::failure(release.error());
    if (release.value().version != installed_version ||
        release.value().repository != update::official_release_repository() ||
        widen_ascii(release.value().tag) != plan.value().tag ||
        !release.value().asset ||
        widen_ascii(release.value().asset->file_name) != plan.value().asset_name ||
        widen_ascii(release.value().asset->download_url) != plan.value().url) {
        return Result<PackageRepairResult>::failure(
            {ErrorCode::access_denied,
             L"Auto Repair requires the exact official release asset", 0});
    }
    const auto root = new_repair_work_root(working_directory);
    if (!root.has_value()) return Result<PackageRepairResult>::failure(root.error());
    const auto package = operations.prepare_package(release.value(), root.value());
    if (!package.has_value()) return Result<PackageRepairResult>::failure(package.error());
    TemporaryRepairFiles cleanup{root.value()};
    return repair_package_from_directory(
        executable_directory, package.value().staged_root, expected_source_identity);
}

}  // namespace kf2::security
