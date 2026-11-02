#include "kf2/game/anchor_runtime_loader.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>

#include "kf2/game/game_session.hpp"

namespace kf2::game {
namespace {

constexpr wchar_t kRuntimeName[] = L"kf2_anchor_runtime.dll";

using UniqueHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

std::wstring folded(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return value;
}

std::uint64_t file_time_value(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
        value.dwLowDateTime;
}

Result<std::filesystem::path> pin_runtime(
    const std::filesystem::path& path, UniqueHandle& pinned) {
    if (!path.is_absolute() || path.filename() != kRuntimeName) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::invalid_argument,
             L"Anchor runtime path or filename is invalid", 0});
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
    pinned.reset(file == INVALID_HANDLE_VALUE ? nullptr : file);
    if (!pinned) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::not_found, L"Anchor runtime cannot be opened",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(pinned.get(), &info)) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::access_denied, L"Anchor runtime file is unsafe",
             GetLastError()});
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                  FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::access_denied, L"Anchor runtime file is unsafe", 0});
    }
    std::error_code error;
    auto canonical = std::filesystem::canonical(path, error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::io_failure,
             L"Anchor runtime path cannot be canonicalized",
             static_cast<std::uint32_t>(error.value())});
    }
    return Result<std::filesystem::path>::success(std::move(canonical));
}

enum class ModuleState { absent, exact, conflict };

Result<ModuleState> module_state(std::uint32_t pid,
                                 const std::filesystem::path& expected) {
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt != 4; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                      TH32CS_SNAPMODULE32, pid);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) {
            break;
        }
    }
    UniqueHandle snapshot{raw == INVALID_HANDLE_VALUE ? nullptr : raw,
                          &CloseHandle};
    if (!snapshot) {
        return Result<ModuleState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 module list cannot be inspected", GetLastError()});
    }
    MODULEENTRY32W entry{sizeof(entry)};
    if (!Module32FirstW(snapshot.get(), &entry)) {
        return Result<ModuleState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 module list is unavailable", GetLastError()});
    }
    const auto expected_path = folded(expected.native());
    do {
        if (folded(entry.szModule) != folded(kRuntimeName)) continue;
        std::error_code error;
        const auto actual = std::filesystem::weakly_canonical(
            std::filesystem::path{entry.szExePath}, error);
        if (!error && folded(actual.native()) == expected_path) {
            return Result<ModuleState>::success(ModuleState::exact);
        }
        return Result<ModuleState>::success(ModuleState::conflict);
    } while (Module32NextW(snapshot.get(), &entry));
    const DWORD native = GetLastError();
    if (native != ERROR_NO_MORE_FILES) {
        return Result<ModuleState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 module list inspection failed", native});
    }
    return Result<ModuleState>::success(ModuleState::absent);
}

Result<LPTHREAD_START_ROUTINE> remote_load_library(std::uint32_t pid) {
    const auto local = reinterpret_cast<unsigned char*>(GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HMODULE local_owner{};
    if (!local || !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(local), &local_owner)) {
        return Result<LPTHREAD_START_ROUTINE>::failure(
            {ErrorCode::platform_failure,
             L"Windows loader entry point is unavailable", GetLastError()});
    }
    wchar_t owner_path[32768]{};
    const DWORD length = GetModuleFileNameW(local_owner, owner_path, 32768);
    if (length == 0 || length == 32768) {
        return Result<LPTHREAD_START_ROUTINE>::failure(
            {ErrorCode::platform_failure,
             L"Windows loader module cannot be identified", GetLastError()});
    }
    const auto owner_name = folded(
        std::filesystem::path{std::wstring{owner_path, length}}
            .filename().native());
    const auto local_base = reinterpret_cast<unsigned char*>(local_owner);
    const auto offset = static_cast<std::size_t>(local - local_base);

    HANDLE raw = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt != 4; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                      TH32CS_SNAPMODULE32, pid);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) {
            break;
        }
    }
    UniqueHandle snapshot{raw == INVALID_HANDLE_VALUE ? nullptr : raw,
                          &CloseHandle};
    if (!snapshot) {
        return Result<LPTHREAD_START_ROUTINE>::failure(
            {ErrorCode::platform_failure,
             L"Remote Windows loader cannot be inspected", GetLastError()});
    }
    MODULEENTRY32W entry{sizeof(entry)};
    if (!Module32FirstW(snapshot.get(), &entry)) {
        return Result<LPTHREAD_START_ROUTINE>::failure(
            {ErrorCode::platform_failure,
             L"Remote Windows loader module list is unavailable",
             GetLastError()});
    }
    do {
        if (folded(entry.szModule) == owner_name &&
            offset < entry.modBaseSize) {
            return Result<LPTHREAD_START_ROUTINE>::success(
                reinterpret_cast<LPTHREAD_START_ROUTINE>(
                    entry.modBaseAddr + offset));
        }
    } while (Module32NextW(snapshot.get(), &entry));
    return Result<LPTHREAD_START_ROUTINE>::failure(
        {ErrorCode::not_found,
         L"Remote Windows loader module was not found", GetLastError()});
}

Result<bool> validate_process(const GameProcessIdentity& identity,
                              HANDLE process) {
    if (!process || !is_game_process_current(identity)) {
        return Result<bool>::failure(
            {ErrorCode::stale_data, L"KF2 process identity is stale", 0});
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user) ||
        file_time_value(creation) != identity.process_start_id ||
        WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        return Result<bool>::failure(
            {ErrorCode::stale_data, L"KF2 process changed before loading",
             GetLastError()});
    }
    USHORT local_process{}, local_native{}, remote_process{}, remote_native{};
    if (!IsWow64Process2(GetCurrentProcess(), &local_process, &local_native) ||
        !IsWow64Process2(process, &remote_process, &remote_native)) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure,
             L"KF2 process architecture cannot be inspected", GetLastError()});
    }
    if (local_process != remote_process || local_native != remote_native) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"KF2 and the optimizer do not share a compatible architecture",
             0});
    }
    return Result<bool>::success(true);
}

}  // namespace

Result<AnchorRuntimeLoadState> load_anchor_runtime(
    const GameProcessIdentity& identity,
    const std::filesystem::path& runtime_dll) {
    UniqueHandle pinned{nullptr, &CloseHandle};
    auto runtime = pin_runtime(runtime_dll, pinned);
    if (!runtime.has_value()) {
        return Result<AnchorRuntimeLoadState>::failure(runtime.error());
    }

    constexpr DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | SYNCHRONIZE;
    UniqueHandle process{OpenProcess(access, FALSE, identity.pid), &CloseHandle};
    if (!process) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::access_denied,
             L"KF2 denied the independent anchor runtime loader",
             GetLastError()});
    }
    auto valid = validate_process(identity, process.get());
    if (!valid.has_value()) {
        return Result<AnchorRuntimeLoadState>::failure(valid.error());
    }
    auto current = module_state(identity.pid, runtime.value());
    if (!current.has_value()) {
        return Result<AnchorRuntimeLoadState>::failure(current.error());
    }
    if (current.value() == ModuleState::exact) {
        return Result<AnchorRuntimeLoadState>::success(
            AnchorRuntimeLoadState::already_loaded);
    }
    if (current.value() == ModuleState::conflict) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::stale_data,
             L"Another anchor runtime with the same name is already loaded",
             0});
    }

    auto loader = remote_load_library(identity.pid);
    if (!loader.has_value()) {
        return Result<AnchorRuntimeLoadState>::failure(loader.error());
    }

    const auto path = runtime.value().native();
    if (path.size() >= 32767U) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::invalid_argument,
             L"Anchor runtime path is too long", 0});
    }
    const auto bytes = (path.size() + 1U) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process.get(), nullptr, bytes,
                                       MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE);
    if (!remote_path) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 anchor runtime path allocation failed", GetLastError()});
    }
    SIZE_T written{};
    if (!WriteProcessMemory(process.get(), remote_path, path.c_str(), bytes,
                            &written) || written != bytes) {
        const auto native = GetLastError();
        VirtualFreeEx(process.get(), remote_path, 0, MEM_RELEASE);
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 anchor runtime path transfer failed", native});
    }
    UniqueHandle thread{CreateRemoteThread(
        process.get(), nullptr, 0, loader.value(), remote_path, 0, nullptr),
        &CloseHandle};
    if (!thread) {
        const auto native = GetLastError();
        VirtualFreeEx(process.get(), remote_path, 0, MEM_RELEASE);
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::access_denied,
             L"KF2 anchor runtime thread could not be started", native});
    }
    const DWORD wait = WaitForSingleObject(thread.get(), 5000);
    if (wait != WAIT_OBJECT_0) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 anchor runtime load did not finish", wait == WAIT_FAILED
                 ? GetLastError() : WAIT_TIMEOUT});
    }
    VirtualFreeEx(process.get(), remote_path, 0, MEM_RELEASE);
    current = module_state(identity.pid, runtime.value());
    if (!current.has_value()) {
        return Result<AnchorRuntimeLoadState>::failure(current.error());
    }
    if (current.value() != ModuleState::exact) {
        return Result<AnchorRuntimeLoadState>::failure(
            {ErrorCode::platform_failure,
             L"KF2 did not load the verified anchor runtime", 0});
    }
    return Result<AnchorRuntimeLoadState>::success(
        AnchorRuntimeLoadState::loaded);
}

}  // namespace kf2::game
