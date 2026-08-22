#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/update/update_package.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

constexpr std::pair<const wchar_t*, const char*> kFiles[]{
    {L"KF2Optimizer.exe", "executable"},
    {L"Data/Lab/flexRelease_x64.forwarder-lab.dll", "forwarder"},
    {L"Data/Lab/KF2OptimizerTelemetry.u", "telemetry"},
    {L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", "matrix"},
    {L"Data/Documentation/README.md", "index"},
    {L"Data/Documentation/USER_GUIDE.md", "guide"},
    {L"Data/Documentation/UPDATES.md", "updates"},
    {L"Data/Documentation/FEATURE_REFERENCE.md", "reference"},
    {L"Data/Documentation/SAFETY.md", "safety"},
    {L"Data/Documentation/SUPPORT.md", "support"},
    {L"Data/Documentation/LICENSE", "license"},
    {L"Data/Documentation/THIRD_PARTY_NOTICES.md", "notices"},
    {L"Data/Documentation/issue72-feature-inventory.json", "inventory"},
    {L"Data/Documentation/PresentMon-LICENSE.txt", "presentmon"},
};

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_package(const std::filesystem::path& root) {
    std::string integrity =
        "schema_version=1\nproduct=KF2OptimizerNext\n"
        "source_identity=new-build\nfile_count=14\n";
    for (const auto& [relative, bytes] : kFiles) {
        const auto path = root / relative;
        write_file(path, bytes);
        const auto hash = kf2::security::sha256_file_hex(path);
        if (!hash.has_value()) std::abort();
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/' :
                             static_cast<char>(character));
        }
        integrity += "file=" + narrow + "|" + hash.value() + "\n";
    }
    write_file(root / L"Data/package-integrity.ini", integrity);
    write_file(root / L"Data/package-manifest.json",
               "{\"package_version\":\"0.0.3-alpha\"}\n");
}

kf2::update::ReleaseInfo release_for(std::uint64_t size,
                                     std::string hash) {
    return {
        .repository = "https://github.com/example/KF2-Optimizer-Next",
        .tag = "v0.0.3-alpha",
        .version = "0.0.3-alpha",
        .published_at = "2026-08-22T12:00:00Z",
        .changelog = "## What's new\n- Updates.",
        .asset = kf2::update::ReleaseAsset{
            .file_name = "KF2OptimizerNext-v0.0.3-alpha-win64.zip",
            .download_url =
                "https://github.com/example/KF2-Optimizer-Next/releases/"
                "download/v0.0.3-alpha/"
                "KF2OptimizerNext-v0.0.3-alpha-win64.zip",
            .size_bytes = size,
            .sha256 = std::move(hash)},
    };
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const auto archive = root / L"archive.zip";
    write_file(archive, "verified archive bytes");
    const auto archive_hash = kf2::security::sha256_file_hex(archive);
    CHECK(archive_hash.has_value());
    const auto size = fs::file_size(archive);
    const auto release = release_for(size, archive_hash.value());
    CHECK(kf2::update::verify_update_archive(
              archive, *release.asset).has_value());

    auto bad_hash = *release.asset;
    bad_hash.sha256.assign(64, '0');
    CHECK(!kf2::update::verify_update_archive(archive, bad_hash).has_value());
    auto bad_size = *release.asset;
    ++bad_size.size_bytes;
    CHECK(!kf2::update::verify_update_archive(archive, bad_size).has_value());

    const auto work = root / L"prepared";
    const auto prepared = kf2::update::prepare_update_package_with_operations(
        release, work,
        {.download = [](const kf2::update::ReleaseAsset&,
                        const fs::path& destination) {
             write_file(destination, "verified archive bytes");
             return kf2::Result<bool>::success(true);
         },
         .extract = [](const fs::path&, const fs::path& destination) {
             write_package(destination / L"KF2OptimizerNext");
             return kf2::Result<bool>::success(true);
         }});
    CHECK(prepared.has_value());
    CHECK(prepared.value().staged_root ==
          work / L"extracted" / L"KF2OptimizerNext");

    const auto failed_work = root / L"download-failure";
    const auto failed = kf2::update::prepare_update_package_with_operations(
        release, failed_work,
        {.download = [](const kf2::update::ReleaseAsset&, const fs::path&) {
             return kf2::Result<bool>::failure(
                 {kf2::ErrorCode::io_failure, L"Injected download failure", 0});
         },
         .extract = [](const fs::path&, const fs::path&) {
             return kf2::Result<bool>::success(true);
         }});
    CHECK(!failed.has_value());
    CHECK(!fs::exists(failed_work));
    return EXIT_SUCCESS;
}
