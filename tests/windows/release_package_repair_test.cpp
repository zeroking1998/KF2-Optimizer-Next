#include "kf2/security/release_package_repair.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/update/github_release_client.hpp"

#define CHECK(expression) \
    do { if (!(expression)) { std::cerr << __LINE__ << ": " #expression << '\n'; \
        return EXIT_FAILURE; } } while (false)

namespace {
namespace fs = std::filesystem;
constexpr std::string_view kArchiveBytes{"verified repair archive bytes"};

void write_file(const fs::path& path, std::string_view bytes) {
    const auto native = kf2::platform::windows::extended_length_path(path);
    fs::create_directories(native.parent_path());
    std::ofstream output(native, std::ios::binary | std::ios::trunc);
    output << bytes;
}

void write_package(const fs::path& root) {
    std::string manifest = "schema_version=1\r\nproduct=KF2OptimizerNext\r\n"
        "source_identity=repair-build\r\nfile_count=13\r\n";
    for (const auto relative : kf2::security::managed_package_payload_paths()) {
        write_file(root / relative, "payload:" + std::string{relative});
        const auto hash = kf2::security::sha256_file_hex(root / relative);
        if (!hash.has_value()) std::abort();
        manifest += "file=" + std::string{relative} + "|" + hash.value() + "\r\n";
    }
    write_file(root / "Data/package-integrity.ini", manifest);
    write_file(root / "Data/package-manifest.json",
        "{\"package_version\":\"0.0.3-alpha\"}");
}

std::string release_json(std::uint64_t size, std::string_view digest) {
    return "{\"tag_name\":\"v0.0.3-alpha\",\"draft\":false,"
        "\"published_at\":\"2026-08-22T12:00:00Z\",\"assets\":[{"
        "\"name\":\"KF2OptimizerNext-v0.0.3-alpha-win64.zip\",\"size\":" +
        std::to_string(size) + ",\"digest\":" + std::string{digest} +
        ",\"browser_download_url\":\"https://github.com/example/KF2-Optimizer-Next/"
        "releases/download/v0.0.3-alpha/KF2OptimizerNext-v0.0.3-alpha-win64.zip\"}]}";
}

std::map<fs::path, std::string> snapshot(const fs::path& root) {
    std::map<fs::path, std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        files.emplace(entry.path().lexically_relative(root),
            std::string{std::istreambuf_iterator<char>{input},
                        std::istreambuf_iterator<char>{}});
    }
    return files;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 5) {
        const auto repaired =
            kf2::security::download_and_repair_release_package(
                std::filesystem::path{argv[1]},
                std::filesystem::path{argv[2]}, argv[3], argv[4]);
        if (!repaired.has_value()) {
            std::wcerr << repaired.error().message << L'\n';
            return EXIT_FAILURE;
        }
        if (repaired.value().repaired_files == 0 ||
            !repaired.value().restart_required) {
            return EXIT_FAILURE;
        }
        std::cout << "PASS: exact-version GitHub Auto Repair restored "
                  << repaired.value().repaired_files << " files\n";
        return EXIT_SUCCESS;
    }
    if (argc != 1) return EXIT_FAILURE;
    const auto alpha =
        kf2::security::exact_release_repair_plan("0.0.2-alpha");
    CHECK(alpha.has_value());
    CHECK(alpha.value().tag == L"v0.0.2-alpha");
    CHECK(alpha.value().asset_name ==
          L"KF2OptimizerNext-v0.0.2-alpha-win64.zip");
    CHECK(alpha.value().url.starts_with(L"https://github.com/"));
    CHECK(alpha.value().url.ends_with(
          L"/releases/download/v0.0.2-alpha/KF2OptimizerNext-v0.0.2-alpha-win64.zip"));
    CHECK(alpha.value().url.find(L"/latest/") == std::wstring::npos);

    const auto exact_intermediate =
        kf2::security::exact_release_repair_plan("0.0.27-alpha");
    CHECK(exact_intermediate.has_value());
    CHECK(exact_intermediate.value().tag == L"v0.0.27-alpha");

    CHECK(!kf2::security::exact_release_repair_plan("").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("../0.0.2-alpha").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("0.0.2 alpha").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("0.0.2-alpha/other").has_value());

    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    const auto installed = root / "installed";
    const auto work = root / "work";
    write_package(installed);
    write_file(installed / "Data/settings.ini", "user-owned settings");
    write_file(work / "unrelated.txt", "must survive cleanup");
    fs::remove(installed / "Data/Lab/KF2OptimizerTelemetry.u");
    const auto before = snapshot(installed);
    const auto hash = kf2::security::sha256_hex(kArchiveBytes);
    CHECK(hash.has_value());
    const auto metadata = release_json(kArchiveBytes.size(),
        "\"sha256:" + hash.value() + "\"");
    int prepared_count = 0;
    int downloads = 0;
    int extractions = 0;
    std::string downloaded_bytes{kArchiveBytes};
    kf2::security::ReleaseRepairOperations operations{
        .query_release = [&](std::string_view version) {
            return kf2::update::parse_exact_github_release(
                metadata, kf2::update::official_release_repository(), version);
        },
        .prepare_package = [&](const kf2::update::ReleaseInfo& release,
                               const fs::path& destination) {
            ++prepared_count;
            return kf2::update::prepare_update_package_with_operations(
                release, destination,
                {.download = [&](const kf2::update::ReleaseAsset&,
                                 const fs::path& archive) {
                     ++downloads;
                     write_file(archive, downloaded_bytes);
                     return kf2::Result<bool>::success(true);
                 },
                 .extract = [&](const fs::path&, const fs::path& extraction) {
                     ++extractions;
                     write_package(extraction / "KF2OptimizerNext");
                     return kf2::Result<bool>::success(true);
                 }});
        }};
    const auto repair = [&] {
        return kf2::security::download_and_repair_release_package_with_operations(
            installed, work, "0.0.3-alpha", "repair-build", operations);
    };

    // A self-consistent extracted package must not authorize substituted bytes.
    downloaded_bytes[0] = 'X'; // same size, different SHA-256
    CHECK(!repair().has_value());
    CHECK(downloads == 1 && extractions == 0);
    CHECK(snapshot(installed) == before);
    CHECK(snapshot(work).size() == 1);
    downloaded_bytes = kArchiveBytes;

    for (const auto invalid : {
             release_json(kArchiveBytes.size() + 1, "\"sha256:" + hash.value() + "\""),
             release_json(kArchiveBytes.size(), "\"sha256:" + std::string(64, '0') + "\""),
             release_json(kArchiveBytes.size(), "null"),
             release_json(kArchiveBytes.size(), "\"sha256:wrong\"")}) {
        operations.query_release = [invalid](std::string_view version) {
            return kf2::update::parse_exact_github_release(
                invalid, kf2::update::official_release_repository(), version);
        };
        CHECK(!repair().has_value());
        CHECK(extractions == 0);
        CHECK(snapshot(installed) == before);
        CHECK(snapshot(work).size() == 1);
    }
    const auto queried = kf2::update::parse_exact_github_release(
        metadata, kf2::update::official_release_repository(), "0.0.3-alpha");
    CHECK(queried.has_value());
    const int prepared_before = prepared_count;
    for (int invalid = 0; invalid < 4; ++invalid) {
        auto foreign = queried.value();
        if (invalid == 0) foreign.version = "0.0.4-alpha";
        if (invalid == 1) foreign.repository = "https://github.com/other/repo";
        if (invalid == 2) foreign.asset->file_name = "other.zip";
        if (invalid == 3) foreign.asset->download_url = "https://evil.example/file.zip";
        operations.query_release = [foreign](std::string_view) {
            return kf2::Result<kf2::update::ReleaseInfo>::success(foreign);
        };
        CHECK(!repair().has_value());
        CHECK(prepared_count == prepared_before);
        CHECK(snapshot(installed) == before);
    }
    operations.query_release = [queried](std::string_view) { return queried; };
    const auto repaired = repair();
    CHECK(repaired.has_value());
    CHECK(repaired.value().repaired_files == 1);
    CHECK(repaired.value().restart_required);
    CHECK(extractions == 1);
    const auto audit = kf2::security::audit_package_integrity(installed, "repair-build");
    CHECK(audit.has_value() && audit.value().verified);
    CHECK(snapshot(installed).at("Data/settings.ini") == "user-owned settings");
    CHECK(snapshot(work).size() == 1);
    const auto unchanged = repair();
    CHECK(unchanged.has_value() && unchanged.value().repaired_files == 0);
    CHECK(!unchanged.value().restart_required);
    CHECK(snapshot(work).size() == 1);
    auto long_work = root;
    while (long_work.wstring().size() < 290) long_work /= "long-path-segment";
    const auto long_repair =
        kf2::security::download_and_repair_release_package_with_operations(
            installed, long_work, "0.0.3-alpha", "repair-build", operations);
    CHECK(long_repair.has_value() && long_repair.value().repaired_files == 0);
    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
