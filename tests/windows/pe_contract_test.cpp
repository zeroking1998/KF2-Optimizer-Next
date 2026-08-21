#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::filesystem::path executable{std::u8string{
        reinterpret_cast<const char8_t*>(argv[1]), std::strlen(argv[1])}};
    std::ifstream input(executable, std::ios::binary);
    CHECK(input.good());
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    CHECK(bytes.size() >= sizeof(IMAGE_DOS_HEADER));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    CHECK(dos->e_magic == IMAGE_DOS_SIGNATURE);
    CHECK(dos->e_lfanew > 0);
    const auto pe_offset = static_cast<std::size_t>(dos->e_lfanew);
    CHECK(pe_offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
              sizeof(IMAGE_OPTIONAL_HEADER64) <= bytes.size());
    const auto* signature = reinterpret_cast<const DWORD*>(bytes.data() + pe_offset);
    CHECK(*signature == IMAGE_NT_SIGNATURE);
    const auto* file = reinterpret_cast<const IMAGE_FILE_HEADER*>(signature + 1);
    CHECK(file->Machine == IMAGE_FILE_MACHINE_AMD64);
    const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(file + 1);
    CHECK(optional->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    CHECK(optional->Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI);
    CHECK((optional->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) != 0);
    CHECK((optional->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) != 0);
    CHECK((optional->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA) != 0);
    CHECK((optional->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0);

    const HMODULE image = LoadLibraryExW(
        executable.c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    CHECK(image != nullptr);
    const HRSRC manifest_resource =
        FindResourceW(image, MAKEINTRESOURCEW(1), RT_MANIFEST);
    CHECK(manifest_resource != nullptr);
    const DWORD manifest_size = SizeofResource(image, manifest_resource);
    const HGLOBAL manifest_handle = LoadResource(image, manifest_resource);
    CHECK(manifest_handle != nullptr && manifest_size > 0);
    const auto* manifest_data = static_cast<const char*>(LockResource(manifest_handle));
    CHECK(manifest_data != nullptr);
    const std::string manifest{manifest_data, manifest_size};
    CHECK(manifest.find("requestedExecutionLevel level=\"asInvoker\"") !=
          std::string::npos);
    CHECK(manifest.find("uiAccess=\"false\"") != std::string::npos);
    CHECK(manifest.find("PerMonitorV2") != std::string::npos);
    CHECK(manifest.find("longPathAware") != std::string::npos);
    FreeLibrary(image);
    return EXIT_SUCCESS;
}
