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

constexpr std::pair<const wchar_t*, const char*> kFiles[]{
    {L"KF2Optimizer.exe", "test executable"},
    {L"Data/Lab/flexRelease_x64.forwarder-lab.dll", "test forwarder"},
    {L"Data/Lab/KF2OptimizerTelemetry.u", "test telemetry module"},
    {L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", "test matrix"},
    {L"Data/Documentation/README.md", "test documentation index"},
    {L"Data/Documentation/USER_GUIDE.md", "test user guide"},
    {L"Data/Documentation/UPDATES.md", "test update guide"},
    {L"Data/Documentation/FEATURE_REFERENCE.md", "test feature reference"},
    {L"Data/Documentation/SAFETY.md", "test safety guide"},
    {L"Data/Documentation/SUPPORT.md", "test support guide"},
    {L"Data/Documentation/LICENSE", "test license"},
    {L"Data/Documentation/THIRD_PARTY_NOTICES.md", "test notices"},
    {L"Data/Documentation/issue72-feature-inventory.json", "test inventory"},
    {L"Data/Documentation/PresentMon-LICENSE.txt", "test license"},
};

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_package(const std::filesystem::path& root,
                   std::string_view identity) {
    std::string manifest =
        "schema_version=1\nproduct=KF2OptimizerNext\nsource_identity=" +
        std::string{identity} + "\nfile_count=14\n";
    for (const auto& [relative, content] : kFiles) {
        const auto path = root / relative;
        write_file(path, content);
        const auto hash = kf2::security::sha256_file_hex(path);
        if (!hash.has_value()) std::abort();
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/' :
                             static_cast<char>(character));
        }
        manifest += "file=" + narrow + "|" + hash.value() + "\n";
    }
    write_file(root / L"Data/package-integrity.ini", manifest);
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

    write_package(root, "test-build");
    CHECK(kf2::security::managed_package_payload_paths().size() == 14);
    const auto source_identity = kf2::security::package_source_identity(root);
    CHECK(source_identity.has_value());
    CHECK(source_identity.value() == "test-build");
    const auto verified =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(verified.has_value());
    CHECK(verified.value().managed_package);
    CHECK(verified.value().verified);
    CHECK(verified.value().verified_files == 14);

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

    const auto repair_source = root / L"repair-source";
    const auto repair_target = root / L"repair-target";
    write_package(repair_source, "test-build");
    write_package(repair_target, "test-build");
    CHECK(fs::remove(repair_target /
                     L"Data/Lab/KF2OptimizerTelemetry.u"));
    write_file(repair_target / L"Data/Documentation/SAFETY.md", "damaged");
    CHECK(fs::remove(repair_target / L"Data/package-integrity.ini"));

    const auto repaired = kf2::security::repair_package_from_directory(
        repair_target, repair_source, "test-build");
    CHECK(repaired.has_value());
    CHECK(repaired.value().repaired_files == 3);
    CHECK(repaired.value().already_valid_files == 12);
    CHECK(repaired.value().restart_required);
    const auto repaired_audit =
        kf2::security::audit_package_integrity(repair_target, "test-build");
    CHECK(repaired_audit.has_value());
    CHECK(repaired_audit.value().verified);

    const auto unchanged = kf2::security::repair_package_from_directory(
        repair_target, repair_source, "test-build");
    CHECK(unchanged.has_value());
    CHECK(unchanged.value().repaired_files == 0);
    CHECK(unchanged.value().already_valid_files == 14);
    CHECK(!unchanged.value().restart_required);

    write_file(repair_target / L"Data/Documentation/SAFETY.md",
               "target must remain unchanged");
    write_file(repair_source /
                   L"Data/Documentation/PresentMon-LICENSE.txt",
               "tampered source");
    const auto tampered_source =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    CHECK(!tampered_source.has_value());
    CHECK(read_file(repair_target / L"Data/Documentation/SAFETY.md") ==
          "target must remain unchanged");
    write_package(repair_source, "test-build");

    write_file(repair_target / L"KF2Optimizer.exe", "different executable");
    const auto wrong_executable =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    CHECK(!wrong_executable.has_value());

    const auto wrong_build = kf2::security::repair_package_from_directory(
        repair_source, repair_source, "other-build");
    CHECK(!wrong_build.has_value());
    return EXIT_SUCCESS;
}
