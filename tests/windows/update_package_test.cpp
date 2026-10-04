#include <Windows.h>

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/core/result.hpp"
#include "kf2/platform/windows/state_environment.hpp"
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
};

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    const auto native =
        kf2::platform::windows::extended_length_path(path);
    std::filesystem::create_directories(native.parent_path());
    std::ofstream output(native, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_package(const std::filesystem::path& root) {
    std::string integrity =
        "schema_version=1\r\nproduct=KF2OptimizerNext\r\n"
        "source_identity=new-build\r\nfile_count=13\r\n";
    for (const auto& [relative, bytes] : kFiles) {
        const auto path = root / relative;
        write_file(path, bytes);
        const auto hash = kf2::security::sha256_file_hex(
            kf2::platform::windows::extended_length_path(path));
        if (!hash.has_value()) std::abort();
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/' :
                             static_cast<char>(character));
        }
        integrity += "file=" + narrow + "|" + hash.value() + "\r\n";
    }
    write_file(root / L"Data/package-integrity.ini", integrity);
    write_file(root / L"Data/package-manifest.json",
               "{\"package_version\":\"0.0.3-alpha\"}\n");
}

// A stored ZIP fixture keeps the integration test independent of Shell,
// PowerShell, external compressors and the production extraction code.
void write_zip(const std::filesystem::path& archive,
               const std::vector<std::pair<std::string, std::string>>& files) {
    std::string output;
    std::string directory;
    const auto append = [](std::string& target, std::uint32_t value, int bytes) {
        for (int index = 0; index < bytes; ++index) {
            target.push_back(static_cast<char>(value & 0xffU));
            value >>= 8;
        }
    };
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        auto value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
        }
        table[index] = value;
    }
    for (const auto& [name, bytes] : files) {
        std::uint32_t crc = 0xffffffffU;
        for (const unsigned char byte : bytes) {
            crc = table[(crc ^ byte) & 0xffU] ^ (crc >> 8);
        }
        crc ^= 0xffffffffU;
        const auto offset = static_cast<std::uint32_t>(output.size());
        append(output, 0x04034b50, 4);
        append(output, 20, 2);
        append(output, 0, 2); // flags
        append(output, 0, 2); // stored
        append(output, 0, 2); // time
        append(output, 0x0021, 2); // 1980-01-01
        append(output, crc, 4);
        append(output, static_cast<std::uint32_t>(bytes.size()), 4);
        append(output, static_cast<std::uint32_t>(bytes.size()), 4);
        append(output, static_cast<std::uint32_t>(name.size()), 2);
        append(output, 0, 2);
        output += name;
        output += bytes;

        append(directory, 0x02014b50, 4);
        append(directory, 20, 2);
        directory.append(output, offset + 4, 26);
        append(directory, 0, 2); // comment length
        append(directory, 0, 2); // disk
        append(directory, 0, 2); // internal attributes
        append(directory, 0, 4); // external attributes
        append(directory, offset, 4);
        directory += name;
    }
    const auto directory_offset = static_cast<std::uint32_t>(output.size());
    output += directory;
    append(output, 0x06054b50, 4);
    append(output, 0, 2);
    append(output, 0, 2);
    append(output, static_cast<std::uint32_t>(files.size()), 2);
    append(output, static_cast<std::uint32_t>(files.size()), 2);
    append(output, static_cast<std::uint32_t>(directory.size()), 4);
    append(output, directory_offset, 4);
    append(output, 0, 2);
    write_file(archive, output);
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
    fs::remove_all(
        kf2::platform::windows::extended_length_path(root), error);
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

    auto long_work = root;
    while (long_work.wstring().size() < MAX_PATH + 32) {
        long_work /= L"long-path-segment";
    }
    const auto long_prepared =
        kf2::update::prepare_update_package_with_operations(
            release, long_work,
            {.download = [](const kf2::update::ReleaseAsset&,
                            const fs::path& destination) {
                 write_file(
                     kf2::platform::windows::extended_length_path(destination),
                     "verified archive bytes");
                 return kf2::Result<bool>::success(true);
             },
             .extract = [](const fs::path&, const fs::path& destination) {
                 write_package(
                     kf2::platform::windows::extended_length_path(destination) /
                     L"KF2OptimizerNext");
                 return kf2::Result<bool>::success(true);
             }});
    CHECK(long_prepared.has_value());
    if (long_prepared.has_value()) {
        CHECK(long_prepared.value().staged_root ==
              kf2::platform::windows::extended_length_path(long_work) /
                  L"extracted" / L"KF2OptimizerNext");
    }

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

    // Exercise the real Windows boundary, not a synchronous fake. A small
    // manifest appears first; large nested output must be complete on return.
    const auto real_zip = root / L"actual extraction.zip";
    const std::string large(32U * 1024U * 1024U, 'z');
    const auto real_source = root / L"zip source" / L"KF2OptimizerNext";
    write_package(real_source);
    std::vector<std::pair<std::string, std::string>> zip_files;
    for (const auto& entry : fs::recursive_directory_iterator(real_source)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        zip_files.emplace_back("KF2OptimizerNext/" +
            fs::relative(entry.path(), real_source).generic_string(),
            std::string{std::istreambuf_iterator<char>{input},
                        std::istreambuf_iterator<char>{}});
    }
    const auto manifest = std::find_if(zip_files.begin(), zip_files.end(),
        [](const auto& file) { return file.first.ends_with("package-integrity.ini"); });
    CHECK(manifest != zip_files.end());
    std::iter_swap(zip_files.begin(), manifest);
    zip_files.emplace_back("KF2OptimizerNext/Data/large nested payload.bin", large);
    zip_files.emplace_back("last.txt", "final bytes");
    write_zip(real_zip, zip_files);
    const auto expected_large = root / L"expected.bin";
    write_file(expected_large, large);
    const auto expected_hash = kf2::security::sha256_file_hex(expected_large);
    CHECK(expected_hash.has_value());
    const auto real_destination = root / L"real extraction";
    const auto extracted = kf2::update::extract_update_archive(
        real_zip, real_destination);
    if (!extracted.has_value()) {
        std::wcerr << extracted.error().message << L' ' <<
            extracted.error().native_code << L'\n';
    }
    CHECK(extracted.has_value());
    const auto large_path = real_destination /
        L"KF2OptimizerNext/Data/large nested payload.bin";
    CHECK(fs::file_size(large_path) == large.size());
    const auto extracted_hash = kf2::security::sha256_file_hex(large_path);
    CHECK(extracted_hash.has_value());
    CHECK(extracted_hash.value() == expected_hash.value());
    CHECK(fs::file_size(real_destination / L"last.txt") == 11);
    // Immediate exclusive access and cleanup must not race an active copy.
    const HANDLE completed = CreateFileW(large_path.c_str(), GENERIC_READ,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(completed != INVALID_HANDLE_VALUE);
    CloseHandle(completed);
    fs::remove_all(real_destination, error);
    CHECK(!error && !fs::exists(real_destination));

    CHECK(!kf2::update::extract_update_archive(
        archive, root / L"invalid extraction").has_value());
    CHECK(!kf2::update::extract_update_archive(
        root / L"missing.zip", root / L"missing extraction").has_value());
    CHECK(!kf2::update::extract_update_archive(
        real_zip, root).has_value());
    CHECK(!kf2::update::extract_update_archive(
        real_source, root / L"directory extraction").has_value());
    CHECK(fs::file_size(expected_large) == large.size());

    const auto real_zip_hash = kf2::security::sha256_file_hex(real_zip);
    CHECK(real_zip_hash.has_value());
    const auto actual_release = release_for(fs::file_size(real_zip), real_zip_hash.value());
    const auto actual_prepared = kf2::update::prepare_update_package_with_operations(
        actual_release, root / L"actual prepared",
        {.download = [&](const kf2::update::ReleaseAsset&, const fs::path& path) {
             fs::copy_file(real_zip, path);
             return kf2::Result<bool>::success(true);
         }, .extract = kf2::update::extract_update_archive});
    CHECK(actual_prepared.has_value());
    CHECK(fs::file_size(actual_prepared.value().staged_root /
        L"Data/large nested payload.bin") == large.size());

    // A real copy error must propagate instead of accepting partial output.
    const auto invalid_zip = root / L"invalid entries.zip";
    std::erase_if(zip_files, [](const auto& file) {
        return file.first.ends_with("large nested payload.bin");
    });
    zip_files.emplace_back("invalid:name.txt", "invalid");
    write_zip(invalid_zip, zip_files);
    const auto invalid_hash = kf2::security::sha256_file_hex(invalid_zip);
    CHECK(invalid_hash.has_value());
    const auto invalid_work = root / L"copy failure";
    const auto copy_failure = kf2::update::prepare_update_package_with_operations(
        release_for(fs::file_size(invalid_zip), invalid_hash.value()), invalid_work,
        {.download = [&](const kf2::update::ReleaseAsset&, const fs::path& path) {
             fs::copy_file(invalid_zip, path);
             return kf2::Result<bool>::success(true);
         }, .extract = kf2::update::extract_update_archive});
    CHECK(!copy_failure.has_value());
    CHECK(copy_failure.error().message == L"Update ZIP extraction failed");
    CHECK(!fs::exists(invalid_work));
    fs::remove_all(
        kf2::platform::windows::extended_length_path(root), error);
    return EXIT_SUCCESS;
}
