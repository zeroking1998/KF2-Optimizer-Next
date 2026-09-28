#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "kf2/platform/windows/state_environment.hpp"
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
};

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    const auto native =
        kf2::platform::windows::extended_length_path(path);
    std::filesystem::create_directories(native.parent_path());
    std::ofstream output(native, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(
        kf2::platform::windows::extended_length_path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_package(const std::filesystem::path& root,
                   std::string_view identity,
                   std::string_view version,
                   std::string_view generation) {
    std::string integrity =
        "schema_version=1\r\nproduct=KF2OptimizerNext\r\nsource_identity=" +
        std::string{identity} + "\r\nfile_count=13\r\n";
    for (const auto& [relative, base] : kFiles) {
        const std::string bytes = std::string{generation} + " " + base;
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
    std::filesystem::remove_all(
        kf2::platform::windows::extended_length_path(root), error);
    std::filesystem::create_directories(root);
}

std::filesystem::path partial_read_target;

void truncate_managed_read(const std::filesystem::path& path) {
    if (path == partial_read_target) write_file(path, "short");
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
    CHECK(applied.value().replaced_files == 15);
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

    constexpr std::size_t managed_file_count = std::size(kFiles) + 2U;
    for (std::size_t interruption = 1; interruption <= managed_file_count;
         ++interruption) {
        const auto interrupted_root = root /
            (L"interrupted-" + std::to_wstring(interruption));
        const auto interrupted_target = interrupted_root / L"target";
        const auto interrupted_staged = interrupted_root / L"staged";
        const auto interrupted_backup = interrupted_root / L"backup";
        write_package(interrupted_target, "old-build", "0.0.2-alpha", "old");
        write_package(interrupted_staged, "new-build", "0.0.3-alpha", "new");
        write_user_data(interrupted_target);
        const kf2::update::UpdateTransactionRequest request{
            .target_root = interrupted_target,
            .staged_root = interrupted_staged,
            .backup_root = interrupted_backup,
            .expected_new_version = "0.0.3-alpha",
            .fault = kf2::update::UpdateFaultInjection::
                interrupt_after_replacement,
            .fault_after_replacements = interruption,
        };
        CHECK(!kf2::update::apply_update_transaction(request).has_value());
        const auto interrupted_journal = read_file(
            interrupted_root / L"update-transaction.ini");
        CHECK(interrupted_journal.find(
                  "state=replacement_in_progress\n") != std::string::npos);
        CHECK(interrupted_journal.find(
                  "replaced_files=" + std::to_string(interruption) + "\n") !=
              std::string::npos);
        if (interruption == 1U) {
            const auto substituted_staged =
                interrupted_root / L"substituted-staged";
            write_package(substituted_staged, "new-build", "0.0.3-alpha",
                          "new");
            auto substituted_request = request;
            substituted_request.staged_root = substituted_staged;
            CHECK(!kf2::update::recover_update_transaction(
                       substituted_request).has_value());
        }
        const auto recovered =
            kf2::update::recover_update_transaction(request);
        CHECK(recovered.has_value());
        CHECK(recovered.value().state ==
              kf2::update::UpdateRecoveryState::rollback_verified);
        CHECK(recovered.value().replaced_files == interruption);
        CHECK(kf2::update::package_version(interrupted_target).value() ==
              "0.0.2-alpha");
        CHECK(read_file(interrupted_target / L"KF2Optimizer.exe") ==
              "old executable");
        CHECK(user_data_unchanged(interrupted_target));
        CHECK(kf2::update::update_transaction_allows_cleanup(request).value());
        CHECK(read_file(interrupted_root / L"update-transaction.ini").find(
                  "state=rollback_verified\n") != std::string::npos);
    }

    const auto verified_root = root / L"interrupted-after-verification";
    const auto verified_target = verified_root / L"target";
    const auto verified_staged = verified_root / L"staged";
    const auto verified_backup = verified_root / L"backup";
    write_package(verified_target, "old-build", "0.0.2-alpha", "old");
    write_package(verified_staged, "new-build", "0.0.3-alpha", "new");
    const kf2::update::UpdateTransactionRequest verified_request{
        .target_root = verified_target,
        .staged_root = verified_staged,
        .backup_root = verified_backup,
        .expected_new_version = "0.0.3-alpha",
        .fault = kf2::update::UpdateFaultInjection::
            interrupt_after_verification,
    };
    CHECK(!kf2::update::apply_update_transaction(
               verified_request).has_value());
    CHECK(read_file(verified_root / L"update-transaction.ini").find(
              "state=update_verified\n") != std::string::npos);
    const auto verified_recovery =
        kf2::update::recover_update_transaction(verified_request);
    CHECK(verified_recovery.has_value());
    CHECK(verified_recovery.value().state ==
          kf2::update::UpdateRecoveryState::update_verified);
    CHECK(kf2::update::package_version(verified_target).value() ==
          "0.0.3-alpha");
    CHECK(!kf2::update::update_transaction_allows_cleanup(
               verified_request).value());
    CHECK(kf2::update::mark_update_transaction_handoff_ready(
               verified_request).has_value());
    CHECK(read_file(verified_root / L"update-transaction.ini").find(
              "state=handoff_ready\n") != std::string::npos);
    CHECK(kf2::update::update_transaction_allows_cleanup(
               verified_request).value());

    const auto failed_rollback_root = root / L"failed-rollback";
    const auto failed_rollback_target = failed_rollback_root / L"target";
    const auto failed_rollback_staged = failed_rollback_root / L"staged";
    const auto failed_rollback_backup = failed_rollback_root / L"backup";
    write_package(failed_rollback_target, "old-build", "0.0.2-alpha", "old");
    write_package(failed_rollback_staged, "new-build", "0.0.3-alpha", "new");
    const kf2::update::UpdateTransactionRequest failed_rollback_request{
        .target_root = failed_rollback_target,
        .staged_root = failed_rollback_staged,
        .backup_root = failed_rollback_backup,
        .expected_new_version = "0.0.3-alpha",
        .fault = kf2::update::UpdateFaultInjection::rollback_failure,
    };
    CHECK(!kf2::update::apply_update_transaction(
               failed_rollback_request).has_value());
    CHECK(read_file(failed_rollback_root / L"update-transaction.ini").find(
              "state=rollback_failed\n") != std::string::npos);
    CHECK(!kf2::update::update_transaction_allows_cleanup(
               failed_rollback_request).value());
    const auto rollback_recovery =
        kf2::update::recover_update_transaction(failed_rollback_request);
    CHECK(rollback_recovery.has_value());
    CHECK(rollback_recovery.value().state ==
          kf2::update::UpdateRecoveryState::rollback_verified);
    CHECK(kf2::update::package_version(failed_rollback_target).value() ==
          "0.0.2-alpha");
    CHECK(kf2::update::update_transaction_allows_cleanup(
               failed_rollback_request).value());

    const auto missing_journal_root = root / L"missing-journal";
    const auto missing_journal_target = missing_journal_root / L"target";
    const auto missing_journal_staged = missing_journal_root / L"staged";
    const auto missing_journal_backup = missing_journal_root / L"backup";
    write_package(missing_journal_target, "old-build", "0.0.2-alpha", "old");
    write_package(missing_journal_staged, "new-build", "0.0.3-alpha", "new");
    const kf2::update::UpdateTransactionRequest missing_journal_request{
        .target_root = missing_journal_target,
        .staged_root = missing_journal_staged,
        .backup_root = missing_journal_backup,
        .expected_new_version = "0.0.3-alpha",
        .fault = kf2::update::UpdateFaultInjection::
            interrupt_after_replacement,
        .fault_after_replacements = 1U,
    };
    CHECK(!kf2::update::apply_update_transaction(
               missing_journal_request).has_value());
    CHECK(fs::remove(missing_journal_root / L"update-transaction.ini"));
    const auto missing_journal_recovery =
        kf2::update::recover_update_transaction(missing_journal_request);
    CHECK(missing_journal_recovery.has_value());
    CHECK(missing_journal_recovery.value().state ==
          kf2::update::UpdateRecoveryState::rollback_verified);
    CHECK(kf2::update::package_version(missing_journal_target).value() ==
          "0.0.2-alpha");
    CHECK(read_file(missing_journal_target / L"KF2Optimizer.exe") ==
          "old executable");
    CHECK(kf2::update::update_transaction_allows_cleanup(
               missing_journal_request).value());

    const auto corrupt_journal_root = root / L"corrupt-journal";
    const auto corrupt_journal_target = corrupt_journal_root / L"target";
    const auto corrupt_journal_staged = corrupt_journal_root / L"staged";
    const auto corrupt_journal_backup = corrupt_journal_root / L"backup";
    write_package(corrupt_journal_target, "old-build", "0.0.2-alpha", "old");
    write_package(corrupt_journal_staged, "new-build", "0.0.3-alpha", "new");
    const kf2::update::UpdateTransactionRequest corrupt_journal_request{
        .target_root = corrupt_journal_target,
        .staged_root = corrupt_journal_staged,
        .backup_root = corrupt_journal_backup,
        .expected_new_version = "0.0.3-alpha",
        .fault = kf2::update::UpdateFaultInjection::
            interrupt_after_replacement,
        .fault_after_replacements = 1U,
    };
    CHECK(!kf2::update::apply_update_transaction(
               corrupt_journal_request).has_value());
    write_file(corrupt_journal_root / L"update-transaction.ini", "invalid\n");
    CHECK(!kf2::update::recover_update_transaction(
               corrupt_journal_request).has_value());
    CHECK(read_file(corrupt_journal_root / L"update-transaction.ini") ==
          "invalid\n");

    const auto not_started_root = root / L"not-started";
    const auto not_started_target = not_started_root / L"target";
    const auto not_started_staged = not_started_root / L"staged";
    const auto not_started_backup = not_started_root / L"backup";
    write_package(not_started_target, "old-build", "0.0.2-alpha", "old");
    write_package(not_started_staged, "new-build", "0.0.3-alpha", "new");
    const kf2::update::UpdateTransactionRequest not_started_request{
        .target_root = not_started_target,
        .staged_root = not_started_staged,
        .backup_root = not_started_backup,
        .expected_new_version = "0.0.3-alpha",
    };
    const auto not_started_recovery =
        kf2::update::recover_update_transaction(not_started_request);
    CHECK(not_started_recovery.has_value());
    CHECK(not_started_recovery.value().state ==
          kf2::update::UpdateRecoveryState::not_started);
    CHECK(kf2::update::update_transaction_allows_cleanup(
               not_started_request).value());
    CHECK(read_file(not_started_target / L"KF2Optimizer.exe") ==
          "old executable");

    const auto interrupted_backup_root = root / L"interrupted-backup";
    const auto interrupted_backup_target = interrupted_backup_root / L"target";
    const auto interrupted_backup_staged = interrupted_backup_root / L"staged";
    const auto interrupted_backup = interrupted_backup_root / L"backup";
    write_package(interrupted_backup_target, "old-build", "0.0.2-alpha", "old");
    write_package(interrupted_backup_staged, "new-build", "0.0.3-alpha", "new");
    write_file(interrupted_backup / L"KF2Optimizer.exe", "partial backup");
    const kf2::update::UpdateTransactionRequest interrupted_backup_request{
        .target_root = interrupted_backup_target,
        .staged_root = interrupted_backup_staged,
        .backup_root = interrupted_backup,
        .expected_new_version = "0.0.3-alpha",
    };
    const auto interrupted_backup_recovery =
        kf2::update::recover_update_transaction(interrupted_backup_request);
    CHECK(interrupted_backup_recovery.has_value());
    CHECK(interrupted_backup_recovery.value().state ==
          kf2::update::UpdateRecoveryState::not_started);
    CHECK(kf2::update::update_transaction_allows_cleanup(
               interrupted_backup_request).value());
    CHECK(read_file(interrupted_backup_target / L"KF2Optimizer.exe") ==
          "old executable");

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

    auto long_root = root;
    while (long_root.wstring().size() < MAX_PATH + 32) {
        long_root /= L"long-path-segment";
    }
    const auto native_long_root =
        kf2::platform::windows::extended_length_path(long_root);
    const auto long_target = native_long_root / L"target";
    const auto long_staged = native_long_root / L"staged";
    const auto long_backup = native_long_root / L"backup";
    write_package(long_target, "old-build", "0.0.2-alpha", "old");
    write_package(long_staged, "new-build", "0.0.3-alpha", "new");
    write_user_data(long_target);
    const auto long_applied = kf2::update::apply_update_transaction({
        .target_root = long_target,
        .staged_root = long_staged,
        .backup_root = long_backup,
        .expected_new_version = "0.0.3-alpha",
    });
    CHECK(long_applied.has_value());
    CHECK(kf2::update::package_version(long_target).value() ==
          "0.0.3-alpha");
    CHECK(user_data_unchanged(long_target));

    const auto partial_backup_root = root / L"partial-backup";
    const auto partial_backup_target = partial_backup_root / L"target";
    const auto partial_backup_staged = partial_backup_root / L"staged";
    const auto partial_backup = partial_backup_root / L"backup";
    write_package(partial_backup_target, "old-build", "0.0.2-alpha", "old");
    write_package(partial_backup_staged, "new-build", "0.0.3-alpha", "new");
    partial_read_target = partial_backup_target / L"KF2Optimizer.exe";
    kf2::update::set_managed_read_hook_for_testing(&truncate_managed_read);
    const auto partial_backup_result =
        kf2::update::apply_update_transaction({
            .target_root = partial_backup_target,
            .staged_root = partial_backup_staged,
            .backup_root = partial_backup,
            .expected_new_version = "0.0.3-alpha",
        });
    kf2::update::set_managed_read_hook_for_testing(nullptr);
    CHECK(!partial_backup_result.has_value());
    CHECK(!fs::exists(partial_backup));

    const auto partial_staged_root = root / L"partial-staged";
    const auto partial_staged_target = partial_staged_root / L"target";
    const auto partial_staged = partial_staged_root / L"staged";
    const auto partial_staged_backup = partial_staged_root / L"backup";
    write_package(partial_staged_target, "old-build", "0.0.2-alpha", "old");
    write_package(partial_staged, "new-build", "0.0.3-alpha", "new");
    partial_read_target = partial_staged / L"KF2Optimizer.exe";
    kf2::update::set_managed_read_hook_for_testing(&truncate_managed_read);
    const auto partial_staged_result =
        kf2::update::apply_update_transaction({
            .target_root = partial_staged_target,
            .staged_root = partial_staged,
            .backup_root = partial_staged_backup,
            .expected_new_version = "0.0.3-alpha",
        });
    kf2::update::set_managed_read_hook_for_testing(nullptr);
    CHECK(!partial_staged_result.has_value());
    CHECK(!fs::exists(partial_staged_backup));
    CHECK(read_file(partial_staged_target / L"KF2Optimizer.exe") ==
          "old executable");

    const auto partial_rollback_root = root / L"partial-rollback";
    const auto partial_rollback_target = partial_rollback_root / L"target";
    const auto partial_rollback_staged = partial_rollback_root / L"staged";
    const auto partial_rollback_backup = partial_rollback_root / L"backup";
    write_package(partial_rollback_target, "old-build", "0.0.2-alpha", "old");
    write_package(partial_rollback_staged, "new-build", "0.0.3-alpha", "new");
    CHECK(kf2::update::apply_update_transaction({
        .target_root = partial_rollback_target,
        .staged_root = partial_rollback_staged,
        .backup_root = partial_rollback_backup,
        .expected_new_version = "0.0.3-alpha",
    }).has_value());
    partial_read_target = partial_rollback_backup / L"KF2Optimizer.exe";
    kf2::update::set_managed_read_hook_for_testing(&truncate_managed_read);
    const auto partial_rollback_result =
        kf2::update::rollback_update_transaction(
            partial_rollback_target, partial_rollback_backup);
    kf2::update::set_managed_read_hook_for_testing(nullptr);
    CHECK(!partial_rollback_result.has_value());
    CHECK(read_file(partial_rollback_target / L"KF2Optimizer.exe") ==
          "new executable");

    std::error_code cleanup_error;
    fs::remove_all(native_long_root, cleanup_error);
    fs::remove_all(
        kf2::platform::windows::extended_length_path(root), cleanup_error);
    return EXIT_SUCCESS;
}
