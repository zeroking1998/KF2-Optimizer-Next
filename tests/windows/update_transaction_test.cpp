#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/update/update_transaction.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

constexpr std::pair<const wchar_t*, const char*> kFiles[]{
    {L"KF2Optimizer.exe", "executable"},
    {L"Data/Lab/flexRelease_x64.forwarder-lab.dll", "forwarder"},
    {L"Data/Lab/KF2OptimizerTelemetry.u", "telemetry"},
    {L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", "matrix"},
    {L"Data/Documentation/README.md", "documentation index"},
    {L"Data/Documentation/USER_GUIDE.md", "user guide"},
    {L"Data/Documentation/UPDATES.md", "update guide"},
    {L"Data/Documentation/FEATURE_REFERENCE.md", "feature reference"},
    {L"Data/Documentation/SAFETY.md", "safety guide"},
    {L"Data/Documentation/SUPPORT.md", "support guide"},
    {L"Data/Documentation/LICENSE", "license"},
    {L"Data/Documentation/THIRD_PARTY_NOTICES.md", "notices"},
    {L"Data/Documentation/issue72-feature-inventory.json", "inventory"},
    {L"Data/Documentation/PresentMon-LICENSE.txt", "presentmon license"},
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
                   std::string_view identity,
                   std::string_view version,
                   std::string_view generation) {
    std::string integrity =
        "schema_version=1\nproduct=KF2OptimizerNext\nsource_identity=" +
        std::string{identity} + "\nfile_count=14\n";
    for (const auto& [relative, base] : kFiles) {
        const std::string bytes = std::string{generation} + " " + base;
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
               "{\n  \"package_version\": \"" + std::string{version} +
                   "\"\n}\n");
}

void write_user_data(const std::filesystem::path& root) {
    write_file(root / L"Data/settings.ini", "user settings");
    write_file(root / L"Data/logs/session-events.json", "user log");
    write_file(root / L"Data/backups/verified.ini", "user backup");
    write_file(root / L"Data/profiles/custom.ini", "user profile");
}

bool user_data_unchanged(const std::filesystem::path& root) {
    return read_file(root / L"Data/settings.ini") == "user settings" &&
        read_file(root / L"Data/logs/session-events.json") == "user log" &&
        read_file(root / L"Data/backups/verified.ini") == "user backup" &&
        read_file(root / L"Data/profiles/custom.ini") == "user profile";
}

void reset_root(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    reset_root(root);

    const auto target = root / L"target";
    const auto staged = root / L"staged";
    const auto backup = root / L"backup";
    write_package(target, "old-build", "0.0.2-alpha", "old");
    write_package(staged, "new-build", "0.0.3-alpha", "new");
    write_user_data(target);

    const auto applied = kf2::update::apply_update_transaction({
        .target_root = target,
        .staged_root = staged,
        .backup_root = backup,
        .expected_new_version = "0.0.3-alpha",
    });
    CHECK(applied.has_value());
    CHECK(!applied.value().rolled_back);
    CHECK(applied.value().replaced_files == 16);
    CHECK(applied.value().previous_version == "0.0.2-alpha");
    CHECK(applied.value().installed_version == "0.0.3-alpha");
    CHECK(kf2::update::package_version(target).value() == "0.0.3-alpha");
    CHECK(kf2::update::package_version(backup).value() == "0.0.2-alpha");
    CHECK(user_data_unchanged(target));

    const auto rolled_back =
        kf2::update::rollback_update_transaction(target, backup);
    CHECK(rolled_back.has_value());
    CHECK(kf2::update::package_version(target).value() == "0.0.2-alpha");
    CHECK(user_data_unchanged(target));

    const auto failure_root = root / L"injected";
    const auto failure_target = failure_root / L"target";
    const auto failure_staged = failure_root / L"staged";
    const auto failure_backup = failure_root / L"backup";
    write_package(failure_target, "old-build", "0.0.2-alpha", "old");
    write_package(failure_staged, "new-build", "0.0.3-alpha", "new");
    write_user_data(failure_target);
    const auto injected = kf2::update::apply_update_transaction({
        .target_root = failure_target,
        .staged_root = failure_staged,
        .backup_root = failure_backup,
        .expected_new_version = "0.0.3-alpha",
        .fault = kf2::update::UpdateFaultInjection::after_first_replacement,
    });
    CHECK(!injected.has_value());
    CHECK(kf2::update::package_version(failure_target).value() ==
          "0.0.2-alpha");
    CHECK(read_file(failure_target / L"KF2Optimizer.exe") ==
          "old executable");
    CHECK(user_data_unchanged(failure_target));

    const auto wrong_root = root / L"wrong-version";
    const auto wrong_target = wrong_root / L"target";
    const auto wrong_staged = wrong_root / L"staged";
    write_package(wrong_target, "old-build", "0.0.2-alpha", "old");
    write_package(wrong_staged, "same-build", "0.0.2-alpha", "same");
    const auto wrong = kf2::update::apply_update_transaction({
        .target_root = wrong_target,
        .staged_root = wrong_staged,
        .backup_root = wrong_root / L"backup",
        .expected_new_version = "0.0.2-alpha",
    });
    CHECK(!wrong.has_value());
    CHECK(kf2::update::package_version(wrong_target).value() ==
          "0.0.2-alpha");
    return EXIT_SUCCESS;
}
