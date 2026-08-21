#include <Windows.h>
#include <filesystem>
#include <iostream>
#include "kf2/flex/flex_observation_shared.hpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto sandbox = std::filesystem::absolute(std::filesystem::path{argv[1]});
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
    std::filesystem::create_directories(sandbox, ec);
    if (ec) return 3;
    auto source_runtime = std::filesystem::path{KF2_FLEX_RUNTIME};
    const auto preserved = source_runtime.parent_path() / L"flexRelease_original.dll";
    if (std::filesystem::exists(preserved)) source_runtime = preserved;
    std::filesystem::copy_file(source_runtime, sandbox / L"flexRelease_original.dll",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return 4;
    const auto runtime_dir = source_runtime.parent_path();
    std::filesystem::copy_file(runtime_dir / L"cudart64_75.dll",
                               sandbox / L"cudart64_75.dll",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return 9;
    std::filesystem::copy_file(std::filesystem::absolute(std::filesystem::path{argv[2]}),
                               sandbox / L"flexRelease_x64.dll",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return 5;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        const auto cookie = AddDllDirectory(sandbox.c_str());
        HMODULE module = LoadLibraryExW((sandbox / L"flexRelease_x64.dll").c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (cookie) RemoveDllDirectory(cookie);
        if (!module) { std::wcerr << L"Load failed: " << GetLastError() << L'\n'; return 6; }
        for (WORD ordinal = 1; ordinal <= 52; ++ordinal) {
            if (!GetProcAddress(module, MAKEINTRESOURCEA(ordinal))) {
                std::cerr << "Missing ordinal: " << ordinal << '\n';
                return 7;
            }
        }
        constexpr const char* required[]{"flexInit", "flexShutdown", "flexGetVersion",
            "flexCreateSolver", "flexDestroySolver", "flexUpdateSolver", "flexWaitFence"};
        for (const auto* name : required) if (!GetProcAddress(module, name)) return 7;
        using Version = int(*)();
        auto version = reinterpret_cast<Version>(GetProcAddress(module, "flexGetVersion"));
        if (!version || version() != 31) return 8;
        const auto mapping_name = L"Local\\KF2OptimizerNext_FlexObservation_v1_" +
            std::to_wstring(GetCurrentProcessId());
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
        if (!mapping) return 11;
        auto* shared = static_cast<const kf2::flex::ObservationShared*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                          sizeof(kf2::flex::ObservationShared)));
        if (!shared || shared->magic != kf2::flex::observation_magic ||
            shared->version != kf2::flex::observation_version ||
            shared->pid != GetCurrentProcessId()) return 11;
        UnmapViewOfFile(shared); CloseHandle(mapping);
        if (!FreeLibrary(module)) return 10;
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
        if (mapping) { CloseHandle(mapping); return 12; }
    }
    return 0;
}
