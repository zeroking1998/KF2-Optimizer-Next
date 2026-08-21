#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const auto development =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(development.has_value());
    CHECK(!development.value().managed_package);
    CHECK(development.value().verified);

    const std::pair<const wchar_t*, const char*> files[]{
        {L"KF2Optimizer.exe", "test executable"},
        {L"Data/Lab/flexRelease_x64.forwarder-lab.dll", "test forwarder"},
        {L"Data/Lab/KF2OptimizerTelemetry.u", "test telemetry module"},
        {L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", "test matrix"},
        {L"Data/Documentation/issue72-feature-inventory.json", "test inventory"},
        {L"Data/Documentation/PresentMon-LICENSE.txt", "test license"},
    };
    std::string manifest =
        "schema_version=1\nproduct=KF2OptimizerNext\n"
        "source_identity=test-build\nfile_count=6\n";
    for (const auto& [relative, content] : files) {
        const auto path = root / relative;
        write_file(path, content);
        const auto hash = kf2::security::sha256_file_hex(path);
        CHECK(hash.has_value());
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/' :
                             static_cast<char>(character));
        }
        manifest += "file=" + narrow + "|" + hash.value() + "\n";
    }
    write_file(root / L"Data/package-integrity.ini", manifest);
    const auto verified =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(verified.has_value());
    CHECK(verified.value().managed_package);
    CHECK(verified.value().verified);
    CHECK(verified.value().verified_files == 6);

    write_file(root / L"Data/Documentation/PresentMon-LICENSE.txt", "damaged");
    const auto damaged =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(damaged.has_value());
    CHECK(damaged.value().managed_package);
    CHECK(!damaged.value().verified);

    const auto mixed_build =
        kf2::security::audit_package_integrity(root, "other-build");
    CHECK(mixed_build.has_value());
    CHECK(!mixed_build.value().verified);
    return EXIT_SUCCESS;
}
