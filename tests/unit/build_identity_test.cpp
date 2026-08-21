#include <cstdlib>
#include <iostream>

#include "kf2/app/build_identity.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    const kf2::app::BuildIdentity identity{"0.0.1-alpha", "abcdef12", "dev"};
    CHECK(kf2::app::format_build_identity(identity) ==
          "0.0.1-alpha+abcdef12 (dev)");

    const auto current = kf2::app::current_build_identity();
    CHECK(current.version == "0.0.1-alpha");
    CHECK(current.commit == "unknown");
    CHECK(current.channel == "dev");
    return EXIT_SUCCESS;
}
