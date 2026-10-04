#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/platform/windows/state_environment.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/update/update_helper.hpp"
#include "kf2/update/update_transaction.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

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

enum class ControlFileMutation { none, replace, grow };
ControlFileMutation control_file_mutation{ControlFileMutation::none};
std::filesystem::path control_file_replacement;
bool control_file_mutated{false};

void mutate_control_file(const std::filesystem::path& path) {
    if (control_file_mutation == ControlFileMutation::replace) {
        control_file_mutated = ReplaceFileW(
            path.c_str(), control_file_replacement.c_str(), nullptr,
            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
    } else if (control_file_mutation == ControlFileMutation::grow) {
        HANDLE file = CreateFileW(
            path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        const std::string growth(64U * 1024U, 'x');
        DWORD written = 0;
        control_file_mutated = WriteFile(
            file, growth.data(), static_cast<DWORD>(growth.size()),
            &written, nullptr) != FALSE && written == growth.size();
        CloseHandle(file);
    }
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::uint64_t process_start_id(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32U) |
        creation.dwLowDateTime;
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
        const auto hash = kf2::security::sha256_file_hex(path);
        if (!hash.has_value()) std::abort();
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/'
                                                 : static_cast<char>(character));
        }
        integrity += "file=" + narrow + "|" + hash.value() + "\r\n";
    }
    write_file(root / L"Data/package-integrity.ini", integrity);
    write_file(root / L"Data/package-manifest.json",
               "{\n  \"package_version\": \"" + std::string{version} +
                   "\"\n}\n");
}

bool write_request(const std::filesystem::path& work,
                   const std::filesystem::path& target,
                   std::string_view token) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(work / L"staged", error);
    if (error) return false;
    const std::string bytes =
        "schema_version=2\nparent_process_id=" +
        std::to_string(GetCurrentProcessId()) +
        "\nparent_process_start_id=" +
        std::to_string(process_start_id(GetCurrentProcess())) +
        "\ntarget_root=" + target.string() +
        "\nstaged_root=" + (work / L"staged").string() +
        "\nbackup_root=" + (work / L"backup").string() +
        "\nreceipt_path=" + (work / L"ready.receipt").string() +
        "\nexpected_version=0.0.5\ntoken=" + std::string{token} + "\n";
    return kf2::platform::windows::atomic_replace_utf8(
               work / L"update.marker", token).has_value() &&
        kf2::platform::windows::atomic_replace_utf8(
               work / L"update-request.ini", bytes).has_value();
}

// Cleanup runs asynchronously. A failed status read is uncertainty, not
// confirmed absence; keep it inside the same bounded wait, without throwing.
bool wait_for_work_removal(
    const std::filesystem::path& work, DWORD timeout_ms,
    bool (*read_status)(const std::filesystem::path&, std::error_code&) =
        &std::filesystem::exists) {
    const auto deadline = GetTickCount64() + timeout_ms;
    std::error_code error;
    do {
        const bool exists = read_status(work, error);
        if (!error && !exists) return true;
        if (GetTickCount64() >= deadline) break;
        Sleep(25);
    } while (true);
    if (error) {
        std::cerr << "Cleanup directory status remains uncertain: "
                  << error.value() << ' ' << error.message() << '\n';
    }
    return false;
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view{argv[1]} == L"--child") {
        Sleep(400);
        return 0;
    }
    namespace fs = std::filesystem;

    wchar_t executable[MAX_PATH + 1]{};
    CHECK(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0);
    const auto start_child = [&]() {
        std::wstring command = L"\"" + std::wstring{executable} +
            L"\" --child";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr,
                            FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &child)) {
            std::abort();
        }
        CloseHandle(child.hThread);
        return child.hProcess;
    };

    HANDLE live_child = start_child();
    const auto live_child_id = GetProcessId(live_child);
    const auto live_child_start = process_start_id(live_child);
    CHECK(live_child_id != 0);
    CHECK(live_child_start != 0);
    CHECK(kf2::update::wait_for_update_process_for_testing(
              live_child_id, live_child_start, 0) ==
          kf2::update::UpdateProcessWaitResult::timed_out);
    CHECK(kf2::update::wait_for_update_process_for_testing(
              live_child_id, live_child_start + 1, 30'000) ==
          kf2::update::UpdateProcessWaitResult::exited_or_missing);
    CHECK(kf2::update::wait_for_update_process_for_testing(
              live_child_id, 0, 0) ==
          kf2::update::UpdateProcessWaitResult::failed);
    kf2::update::set_update_helper_stop_fault_for_testing(
        kf2::update::UpdateHelperStopFault::termination_failure);
    CHECK(!kf2::update::stop_update_child_for_testing(live_child));
    CHECK(WaitForSingleObject(live_child, 0) == WAIT_TIMEOUT);
    CHECK(TerminateProcess(live_child, 99));
    CHECK(WaitForSingleObject(live_child, 2'000) == WAIT_OBJECT_0);
    CloseHandle(live_child);

    for (const auto fault : {
             kf2::update::UpdateHelperStopFault::wait_failure,
             kf2::update::UpdateHelperStopFault::wait_timeout}) {
        live_child = start_child();
        kf2::update::set_update_helper_stop_fault_for_testing(fault);
        CHECK(!kf2::update::stop_update_child_for_testing(live_child));
        CHECK(WaitForSingleObject(live_child, 2'000) == WAIT_OBJECT_0);
        CloseHandle(live_child);
    }

    live_child = start_child();
    kf2::update::set_update_helper_stop_fault_for_testing(
        kf2::update::UpdateHelperStopFault::none);
    CHECK(kf2::update::stop_update_child_for_testing(live_child));
    CHECK(WaitForSingleObject(live_child, 0) == WAIT_OBJECT_0);
    CloseHandle(live_child);

    HANDLE stopped_child = start_child();
    const auto stopped_child_id = GetProcessId(stopped_child);
    const auto stopped_child_start = process_start_id(stopped_child);
    CHECK(WaitForSingleObject(stopped_child, 2'000) == WAIT_OBJECT_0);
    CHECK(kf2::update::wait_for_update_process_for_testing(
              stopped_child_id, stopped_child_start, 30'000) ==
          kf2::update::UpdateProcessWaitResult::exited_or_missing);
    CHECK(kf2::update::stop_update_child_for_testing(stopped_child));
    CloseHandle(stopped_child);

    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    CHECK(!wait_for_work_removal(root, 0));
    CHECK(wait_for_work_removal(root / L"not-created", 0));
    CHECK(!wait_for_work_removal(root, 0,
        [](const fs::path&, std::error_code& status_error) {
            status_error = std::make_error_code(std::errc::permission_denied);
            return false;
        }));
    static unsigned status_reads = 0;
    CHECK(wait_for_work_removal(root, 5'000,
        [](const fs::path&, std::error_code& status_error) {
            if (++status_reads == 1) {
                status_error = std::make_error_code(std::errc::permission_denied);
            } else {
                status_error.clear();
            }
            return false;
        }));
    CHECK(status_reads == 2);

    const auto control_root = root / L"control-files";
    fs::create_directories(control_root);
    for (const auto* filename : {
             L"update-request.ini", L"update.marker", L"ready.receipt"}) {
        const auto control = control_root / filename;
        for (const auto mutation : {
                 ControlFileMutation::replace, ControlFileMutation::grow}) {
            write_file(control, "trusted");
            control_file_replacement =
                control_root / (std::wstring{filename} + L".replacement");
            write_file(control_file_replacement, "foreign");
            control_file_mutation = mutation;
            control_file_mutated = false;
            kf2::platform::windows::set_bounded_read_hook_for_testing(
                &mutate_control_file);
            const auto read =
                kf2::update::read_update_control_file_for_testing(control);
            kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
            control_file_mutation = ControlFileMutation::none;
            CHECK(control_file_mutated);
            CHECK(!read.has_value());
            CHECK(read.error().code == kf2::ErrorCode::stale_data);
        }
    }
    const auto temporary = kf2::platform::windows::temporary_directory();
    CHECK(temporary.has_value());
    const auto update_root =
        temporary.value() / L"KF2OptimizerNext-Update";
    const auto work_name = std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    const auto work = update_root / work_name;
    fs::remove_all(work, error);
    const std::string token = "0123456789abcdef0123456789abcdef";
    const auto target = root / L"target";
    const auto staged = work / L"staged";
    const auto backup = work / L"backup";
    write_package(target, "old-build", "0.0.4", "old");
    write_package(staged, "new-build", "0.0.5", "new");
    CHECK(write_request(work, target, token));
    const kf2::update::UpdateTransactionRequest transaction{
        .target_root = target,
        .staged_root = staged,
        .backup_root = backup,
        .expected_new_version = "0.0.5",
    };
    CHECK(kf2::update::apply_update_transaction(transaction).has_value());
    const auto transaction_owner =
        kf2::update::update_transaction_owner_identity(transaction);
    CHECK(transaction_owner.has_value());
    CHECK(transaction_owner.value().process_id == GetCurrentProcessId());
    CHECK(transaction_owner.value().process_start_id ==
          process_start_id(GetCurrentProcess()));

    std::wstring command = L"\"" + std::wstring{executable} +
        L"\" --child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    CHECK(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child));
    CloseHandle(child.hThread);

    const auto receipt = work / L"ready.receipt";
    const auto signaled =
        kf2::update::signal_update_ready_and_schedule_cleanup({
            .receipt_path = receipt,
            .helper_process_id = child.dwProcessId,
            .helper_process_start_id = process_start_id(child.hProcess),
            .work_root = work,
            .token = token});
    CHECK(signaled.has_value());
    CHECK(read_file(receipt) == token);
    CloseHandle(child.hProcess);

    CHECK(wait_for_work_removal(work, 5'000));

    // Cleanup uncertainty must preserve every byte of the recovery material,
    // record why it was deferred, and still permit a later verified rollback.
    const auto deferred_nonce = GetTickCount64() + 100U;
    std::uint64_t case_index = 0;
    for (const auto fault : {
             kf2::update::UpdateCleanupWaitFault::none,
             kf2::update::UpdateCleanupWaitFault::open_failure,
             kf2::update::UpdateCleanupWaitFault::wait_failure,
             kf2::update::UpdateCleanupWaitFault::wait_timeout}) {
        const auto case_work = update_root /
            (std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(deferred_nonce + case_index));
        const auto case_target = root / std::to_wstring(case_index++);
        const auto case_backup = case_work / L"backup";
        write_package(case_target, "old-build", "0.0.4", "old");
        write_package(case_work / L"staged", "new-build", "0.0.5", "new");
        CHECK(write_request(case_work, case_target, token));
        const kf2::update::UpdateTransactionRequest case_transaction{
            .target_root = case_target,
            .staged_root = case_work / L"staged",
            .backup_root = case_backup,
            .expected_new_version = "0.0.5"};
        CHECK(kf2::update::apply_update_transaction(case_transaction).has_value());
        CHECK(kf2::update::mark_update_transaction_handoff_ready(
            case_transaction).has_value());
        write_file(case_work / L"ready.receipt", token);
        std::map<fs::path, std::string> retained;
        for (const auto& entry : fs::recursive_directory_iterator(case_work)) {
            if (entry.is_regular_file()) {
                retained.emplace(entry.path(), read_file(entry.path()));
            }
        }
        kf2::update::set_update_cleanup_wait_fault_for_testing(fault);
        const bool removed = kf2::update::cleanup_update_work_for_testing(
            GetCurrentProcessId(), process_start_id(GetCurrentProcess()),
            case_work, token, 0);
        kf2::update::set_update_cleanup_wait_fault_for_testing(
            kf2::update::UpdateCleanupWaitFault::none);
        CHECK(!removed);
        if (fault == kf2::update::UpdateCleanupWaitFault::open_failure) {
            const auto report_path = case_work / L"cleanup-deferred.ini";
            const auto original_report = read_file(report_path);
            HANDLE locked_report = CreateFileW(report_path.c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                nullptr);
            CHECK(locked_report != INVALID_HANDLE_VALUE);
            const bool removed_with_locked_report =
                kf2::update::cleanup_update_work_for_testing(
                    GetCurrentProcessId(), process_start_id(GetCurrentProcess()),
                    case_work, token, 0);
            CloseHandle(locked_report);
            CHECK(!removed_with_locked_report);
            CHECK(read_file(report_path) == original_report);
        }
        for (const auto& [path, bytes] : retained) {
            CHECK(fs::is_regular_file(path));
            CHECK(read_file(path) == bytes);
        }
        const auto report = read_file(case_work / L"cleanup-deferred.ini");
        CHECK(report.find("state=deferred\n") != std::string::npos);
        CHECK(report.find(fault == kf2::update::UpdateCleanupWaitFault::none ||
                fault == kf2::update::UpdateCleanupWaitFault::wait_timeout
            ? "reason=helper_wait_timeout\n"
            : "reason=helper_exit_unconfirmed\n") != std::string::npos);
        // A missed readiness receipt must not lose the verified old package.
        CHECK(fs::remove(case_work / L"ready.receipt"));
        CHECK(kf2::update::rollback_update_transaction(
            case_target, case_backup).has_value());
        CHECK(kf2::update::package_version(case_target).value() == "0.0.4");
        CHECK(read_file(case_target / L"KF2Optimizer.exe") == "old executable");
        // A reused PID belongs to a different process instance; never wait
        // on or terminate that unrelated process to clean the verified result.
        CHECK(kf2::update::cleanup_update_work_for_testing(
            GetCurrentProcessId(), process_start_id(GetCurrentProcess()) + 1U,
            case_work, token, 30'000));
        CHECK(!fs::exists(case_work));
        CHECK(WaitForSingleObject(GetCurrentProcess(), 0) == WAIT_TIMEOUT);
    }

    // A matching marker alone must never authorize recursive deletion.
    const auto marker_only = update_root /
        (std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64() + 1U));
    fs::create_directories(marker_only);
    CHECK(kf2::platform::windows::atomic_replace_utf8(
              marker_only / L"update.marker", token).has_value());
    CHECK(!kf2::update::schedule_update_cleanup(
               1, 1, marker_only, token).has_value());
    CHECK(fs::exists(marker_only));

    // A complete-looking transaction outside the canonical update root is
    // still unrelated data and must be rejected.
    const auto unrelated = root /
        (std::to_wstring(GetCurrentProcessId()) + L"-1");
    CHECK(write_request(unrelated, root / L"target", token));
    CHECK(!kf2::update::schedule_update_cleanup(
               1, 1, unrelated, token).has_value());
    CHECK(fs::exists(unrelated));

    // Only direct children are valid; nested workspaces and lexical aliases
    // cannot cross or disguise the cleanup boundary.
    const auto nested = marker_only /
        (std::to_wstring(GetCurrentProcessId()) + L"-2");
    CHECK(write_request(nested, root / L"target", token));
    CHECK(!kf2::update::schedule_update_cleanup(
               1, 1, nested, token).has_value());
    const auto alias_component = update_root / L"alias-component";
    fs::create_directories(alias_component);
    const auto aliased = alias_component / L".." / marker_only.filename();
    CHECK(!kf2::update::schedule_update_cleanup(
               1, 1, aliased, token).has_value());

    // Reparse-point substitution is rejected when the platform permits the
    // unprivileged test to create a directory symlink.
    const auto reparse_target = root / L"reparse-target";
    CHECK(write_request(reparse_target, root / L"target", token));
    const auto reparse_work = update_root /
        (std::to_wstring(GetCurrentProcessId()) + L"-3");
    fs::create_directories(update_root);
    fs::create_directory_symlink(reparse_target, reparse_work, error);
    if (!error) {
        CHECK(!kf2::update::schedule_update_cleanup(
                   1, 1, reparse_work, token).has_value());
        fs::remove(reparse_work, error);
    }

    CHECK(!kf2::update::schedule_update_cleanup(
               1, 1, marker_only, "not-a-valid-token").has_value());
    fs::remove_all(marker_only, error);
    fs::remove(alias_component, error);
    fs::remove_all(root, error);
    CHECK(kf2::update::run_update_helper(
              root / L"missing-request.ini") == 20);
    return EXIT_SUCCESS;
}
