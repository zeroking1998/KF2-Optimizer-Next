#include <cstdlib>
#include <iostream>

#include "kf2/core/result.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    const auto ok = kf2::Result<int>::success(42);
    CHECK(ok.has_value());
    CHECK(ok.value() == 42);

    const auto failed = kf2::Result<int>::failure(
        {kf2::ErrorCode::invalid_argument, L"bad input", 87});
    CHECK(!failed.has_value());
    CHECK(failed.error().native_code == 87);

    bool invalid_value_access_threw = false;
    try {
        static_cast<void>(failed.value());
    } catch (const std::logic_error&) {
        invalid_value_access_threw = true;
    }
    CHECK(invalid_value_access_threw);

    return EXIT_SUCCESS;
}
