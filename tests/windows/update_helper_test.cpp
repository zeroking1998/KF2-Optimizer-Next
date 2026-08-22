#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/update/update_helper.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
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
    const auto work = root / L"KF2OptimizerNext-Update" / L"test-token";
    fs::create_directories(work);
    const std::string token = "0123456789abcdef0123456789abcdef";
    CHECK(kf2::platform::windows::atomic_replace_utf8(
              work / L"update.marker", token).has_value());

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

    fs::create_directories(work);
    CHECK(kf2::platform::windows::atomic_replace_utf8(
              work / L"update.marker", token).has_value());
    CHECK(!kf2::update::schedule_update_cleanup(
               1, work, "not-a-valid-token").has_value());
    fs::remove_all(root, error);
    CHECK(kf2::update::run_update_helper(
              root / L"missing-request.ini") == 20);
    return EXIT_SUCCESS;
}
