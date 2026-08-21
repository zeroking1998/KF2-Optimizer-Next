#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/platform/windows/single_instance.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using kf2::platform::windows::SingleInstance;
    const std::wstring name = L"Local\\KF2OptimizerNext-Test-" +
                              std::to_wstring(GetCurrentProcessId());

    {
        auto first = SingleInstance::acquire(name);
        CHECK(first.has_value());

        auto second = SingleInstance::acquire(name);
        CHECK(!second.has_value());
        CHECK(second.error().code == kf2::ErrorCode::already_running);
    }

    auto third = SingleInstance::acquire(name);
    CHECK(third.has_value());
    return EXIT_SUCCESS;
}
