#include <Windows.h>

#include <cstdlib>
#include <iostream>

#include "kf2/platform/windows/process_security.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    wchar_t unsafe_directory[MAX_PATH]{};
    CHECK(SetDllDirectoryW(L"C:\\untrusted-current-directory") != FALSE);
    CHECK(GetDllDirectoryW(MAX_PATH, unsafe_directory) > 0);

    const auto hardened =
        kf2::platform::windows::harden_process_dll_search();
    CHECK(hardened.has_value());
    wchar_t directory[MAX_PATH]{};
    CHECK(GetDllDirectoryW(MAX_PATH, directory) == 0);
    CHECK(directory[0] == L'\0');
    return EXIT_SUCCESS;
}
