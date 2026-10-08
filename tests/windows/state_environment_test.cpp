#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "kf2/platform/windows/state_environment.hpp"

namespace {

int token_queries = 0;

DWORD WINAPI denied_temp_path(DWORD, LPWSTR) {
    SetLastError(ERROR_ACCESS_DENIED);
    return 0;
}

DWORD WINAPI selected_temp_path(DWORD capacity, LPWSTR buffer) {
    constexpr wchar_t path[] = L"C:\\selected-temp\\";
    if (capacity < std::size(path)) return static_cast<DWORD>(std::size(path));
    std::copy(std::begin(path), std::end(path), buffer);
    return static_cast<DWORD>(std::size(path) - 1);
}

BOOL WINAPI denied_token(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD) {
    ++token_queries;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL token_identity(TOKEN_INFORMATION_CLASS kind, LPVOID buffer,
                    DWORD capacity, PDWORD required, WELL_KNOWN_SID_TYPE sid) {
    ++token_queries;
    if (kind != TokenUser || capacity < sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    auto* user = static_cast<TOKEN_USER*>(buffer);
    user->User.Sid = static_cast<unsigned char*>(buffer) + sizeof(TOKEN_USER);
    DWORD sid_size = capacity - sizeof(TOKEN_USER);
    *required = capacity;
    return CreateWellKnownSid(sid, nullptr, user->User.Sid, &sid_size);
}

BOOL WINAPI system_token(HANDLE, TOKEN_INFORMATION_CLASS kind, LPVOID buffer,
                         DWORD capacity, PDWORD required) {
    return token_identity(kind, buffer, capacity, required, WinLocalSystemSid);
}

BOOL WINAPI non_system_token(HANDLE, TOKEN_INFORMATION_CLASS kind, LPVOID buffer,
                             DWORD capacity, PDWORD required) {
    return token_identity(kind, buffer, capacity, required, WinLocalServiceSid);
}

bool imports_temp_path2() {
    const auto* image = reinterpret_cast<const unsigned char*>(
        GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0) return false;
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        image + imports.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        if (descriptor->OriginalFirstThunk == 0) return true;
        const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA*>(
            image + descriptor->OriginalFirstThunk);
        for (; thunk->u1.AddressOfData != 0; ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                image + thunk->u1.AddressOfData);
            if (std::strcmp(import->Name, "GetTempPath2W") == 0) return true;
        }
    }
    return false;
}

}  // namespace

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    namespace fs = std::filesystem;
    using namespace kf2::platform::windows;

    CHECK(!imports_temp_path2());

    const auto executable_file = executable_path();
    CHECK(executable_file.has_value());
    CHECK(executable_file.value().is_absolute());
    const auto executable = executable_directory();
    CHECK(executable.has_value());
    CHECK(executable.value() == executable_file.value().parent_path());

    const auto temporary = temporary_directory();
    CHECK(temporary.has_value());
    CHECK(temporary.value().is_absolute());

    TemporaryDirectoryTestHooks hooks;
    hooks.query_token = non_system_token;
    set_temporary_directory_test_hooks(&hooks);
    const auto legacy_temporary = temporary_directory();
    CHECK(legacy_temporary.has_value());
    wchar_t native_temporary[32768]{};
    const DWORD native_length = GetTempPathW(32768, native_temporary);
    CHECK(native_length > 0 && native_length < 32768);
    CHECK(legacy_temporary.value() == fs::path{native_temporary});

    hooks.query_token = system_token;
    token_queries = 0;
    const auto system_temporary = temporary_directory();
    CHECK(!system_temporary.has_value());
    CHECK(system_temporary.error().native_code == ERROR_NOT_SUPPORTED);
    CHECK(token_queries == 1);

    hooks.query_token = denied_token;
    token_queries = 0;
    const auto unknown_identity = temporary_directory();
    CHECK(!unknown_identity.has_value());
    CHECK(unknown_identity.error().native_code == ERROR_ACCESS_DENIED);
    CHECK(token_queries == 1);

    hooks.temp_path2 = denied_temp_path;
    token_queries = 0;
    const auto failed_modern = temporary_directory();
    CHECK(!failed_modern.has_value());
    CHECK(failed_modern.error().native_code == ERROR_ACCESS_DENIED);
    CHECK(token_queries == 0);

    hooks.temp_path2 = selected_temp_path;
    const auto selected = temporary_directory();
    CHECK(selected.has_value());
    CHECK(selected.value() == fs::path{L"C:\\selected-temp\\"});
    CHECK(token_queries == 0);
    set_temporary_directory_test_hooks(nullptr);

    const auto app_data = local_app_data_directory();
    CHECK(app_data.has_value());
    CHECK(app_data.value().is_absolute());

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    CHECK(probe_writable_directory(root / L"Data"));
    CHECK(fs::is_directory(root / L"Data"));
    CHECK(fs::is_empty(root / L"Data"));

    fs::create_directories(root);
    const auto file_parent = root / L"not-a-directory";
    std::ofstream{file_parent} << "file";
    CHECK(!probe_writable_directory(file_parent / L"Data"));

    auto long_directory = root;
    while (long_directory.wstring().size() < MAX_PATH + 32) {
        long_directory /= L"long-path-segment";
    }
    CHECK(probe_writable_directory(long_directory));
    std::error_code long_error;
    CHECK(fs::is_directory(extended_length_path(long_directory), long_error));
    CHECK(!long_error);
    fs::remove_all(extended_length_path(root));
    return EXIT_SUCCESS;
}
