#include <cstdlib>

#include "kf2/update/semantic_version.hpp"

#define CHECK(expression) do { if (!(expression)) return EXIT_FAILURE; } while (false)

namespace {
int compare(const char* left, const char* right) {
    const auto a = kf2::update::parse_semantic_version(left);
    const auto b = kf2::update::parse_semantic_version(right);
    if (!a.has_value() || !b.has_value()) return 99;
    return kf2::update::compare_semantic_versions(a.value(), b.value());
}
}

int main() {
    CHECK(compare("0.0.2-alpha", "0.0.3-alpha") < 0);
    CHECK(compare("v1.2.3", "1.2.3") == 0);
    CHECK(compare("1.2.3-alpha", "1.2.3-beta") < 0);
    CHECK(compare("1.2.3-beta", "1.2.3") < 0);
    CHECK(compare("1.2.3-alpha.9", "1.2.3-alpha.10") < 0);
    CHECK(compare("1.2.3+build.7", "1.2.3+build.8") == 0);
    CHECK(compare("2.0.0-alpha", "1.99.99") > 0);
    CHECK(!kf2::update::parse_semantic_version("1.2").has_value());
    CHECK(!kf2::update::parse_semantic_version("01.2.3").has_value());
    CHECK(!kf2::update::parse_semantic_version("1.2.3-alpha..1").has_value());
    CHECK(!kf2::update::parse_semantic_version("../1.2.3").has_value());
    return EXIT_SUCCESS;
}
