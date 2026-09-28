#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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
        "schema_version=1\nparent_process_id=" +
        std::to_string(GetCurrentProcessId()) +
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

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view{argv[1]} == L"--child") {
        Sleep(400);
        return 0;
    }
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
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
    CHECK(kf2::update::apply_update_transaction({
        .target_root = target,
        .staged_root = staged,
        .backup_root = backup,
        .expected_new_version = "0.0.5",
    }).has_value());

    wchar_t executable[MAX_PATH + 1]{};
    CHECK(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0);
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
            .work_root = work,
            .token = token});
    CHECK(signaled.has_value());
    CHECK(read_file(receipt) == token);
    CloseHandle(child.hProcess);

    const auto deadline = GetTickCount64() + 5'000;
    while (fs::exists(work) && GetTickCount64() < deadline) Sleep(25);
    CHECK(!fs::exists(work));

    // A matching marker alone must never authorize recursive deletion.
    const auto marker_only = update_root /
        (std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64() + 1U));
    fs::create_directories(marker_only);
    CHECK(kf2::platform::windows::atomic_replace_utf8(
              marker_only / L"update.marker", token).has_value());
    CHECK(!kf2::update::schedule_update_cleanup(
               1, marker_only, token).has_value());
    CHECK(fs::exists(marker_only));

    // A complete-looking transaction outside the canonical update root is
    // still unrelated data and must be rejected.
    const auto unrelated = root /
        (std::to_wstring(GetCurrentProcessId()) + L"-1");
    CHECK(write_request(unrelated, root / L"target", token));
    CHECK(!kf2::update::schedule_update_cleanup(
               1, unrelated, token).has_value());
    CHECK(fs::exists(unrelated));

    // Only direct children are valid; nested workspaces and lexical aliases
    // cannot cross or disguise the cleanup boundary.
    const auto nested = marker_only /
        (std::to_wstring(GetCurrentProcessId()) + L"-2");
    CHECK(write_request(nested, root / L"target", token));
    CHECK(!kf2::update::schedule_update_cleanup(
               1, nested, token).has_value());
    const auto alias_component = update_root / L"alias-component";
    fs::create_directories(alias_component);
    const auto aliased = alias_component / L".." / marker_only.filename();
    CHECK(!kf2::update::schedule_update_cleanup(
               1, aliased, token).has_value());

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
                   1, reparse_work, token).has_value());
        fs::remove(reparse_work, error);
    }

    CHECK(!kf2::update::schedule_update_cleanup(
               1, marker_only, "not-a-valid-token").has_value());
    fs::remove_all(marker_only, error);
    fs::remove(alias_component, error);
    fs::remove_all(root, error);
    CHECK(kf2::update::run_update_helper(
              root / L"missing-request.ini") == 20);
    return EXIT_SUCCESS;
}
