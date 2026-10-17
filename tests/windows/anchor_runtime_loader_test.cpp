#include <Windows.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/game/anchor_runtime_loader.hpp"
#include "kf2/game/game_session.hpp"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

std::wstring event_name(std::wstring_view kind, std::wstring_view token) {
    return L"Local\\KF2AnchorRuntimeLoader_" + std::wstring{kind} + L"_" +
        std::wstring{token};
}

int child(std::wstring_view token) {
    const auto ready_name = event_name(L"Ready", token);
    const auto stop_name = event_name(L"Stop", token);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name.c_str());
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, stop_name.c_str());
    if (!ready || !stop) return 3;
    const bool signaled = SetEvent(ready) != FALSE;
    const DWORD wait = signaled ? WaitForSingleObject(stop, 15000) : WAIT_FAILED;
    CloseHandle(stop);
    CloseHandle(ready);
    return wait == WAIT_OBJECT_0 ? 0 : 4;
}

struct ChildProcess {
    HANDLE process{};
    HANDLE thread{};
    HANDLE ready{};
    HANDLE stop{};

    ~ChildProcess() {
        if (stop) SetEvent(stop);
        if (process && WaitForSingleObject(process, 2000) == WAIT_TIMEOUT) {
            TerminateProcess(process, 5);
            WaitForSingleObject(process, 2000);
        }
        if (thread) CloseHandle(thread);
        if (process) CloseHandle(process);
        if (ready) CloseHandle(ready);
        if (stop) CloseHandle(stop);
    }
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view{argv[1]} == L"--child") {
        return child(argv[2]);
    }
    if (argc != 2) return 2;

    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    CHECK(length != 0 && length != 32768);
    const std::filesystem::path executable_path{
        std::wstring{executable, length}};
    const auto token = std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64());
    const auto ready_name = event_name(L"Ready", token);
    const auto stop_name = event_name(L"Stop", token);
    ChildProcess owned;
    owned.ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    owned.stop = CreateEventW(nullptr, TRUE, FALSE, stop_name.c_str());
    CHECK(owned.ready && owned.stop);

    std::wstring command = L"\"" + executable_path.native() +
        L"\" --child " + token;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                         &process));
    owned.process = process.hProcess;
    owned.thread = process.hThread;
    CHECK(WaitForSingleObject(owned.ready, 5000) == WAIT_OBJECT_0);

    const auto identity = kf2::game::bind_game_process(
        process.dwProcessId, executable_path);
    CHECK(identity.has_value());
    const auto runtime = std::filesystem::absolute(
        std::filesystem::path{argv[1]});
    CHECK(!kf2::game::load_anchor_runtime(
        identity.value(), runtime.parent_path() / L"wrong.dll").has_value());
    auto stale = identity.value();
    ++stale.process_start_id;
    CHECK(!kf2::game::load_anchor_runtime(stale, runtime).has_value());
    CHECK(GetModuleHandleW(L"kf2_anchor_runtime.dll") == nullptr);

    const auto loaded = kf2::game::load_anchor_runtime(
        identity.value(), runtime);
    CHECK(loaded.has_value());
    CHECK(loaded.value() == kf2::game::AnchorRuntimeLoadState::loaded);
    CHECK(GetModuleHandleW(L"kf2_anchor_runtime.dll") == nullptr);
    const auto repeated = kf2::game::load_anchor_runtime(
        identity.value(), runtime);
    CHECK(repeated.has_value());
    CHECK(repeated.value() ==
          kf2::game::AnchorRuntimeLoadState::already_loaded);

    CHECK(SetEvent(owned.stop));
    CHECK(WaitForSingleObject(owned.process, 5000) == WAIT_OBJECT_0);
    DWORD exit_code{};
    CHECK(GetExitCodeProcess(owned.process, &exit_code) && exit_code == 0);
    return 0;
}
