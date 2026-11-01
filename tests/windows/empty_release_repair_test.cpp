#include "kf2/security/release_package_repair.hpp"
#include "kf2/update/github_release_client.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    if (!kf2::update::official_release_repository().empty()) {
        std::cerr << "Expected an explicitly empty release repository\n";
        return EXIT_FAILURE;
    }
    const auto plan = kf2::security::exact_release_repair_plan("0.0.4-alpha");
    if (plan.has_value() || plan.error().code != kf2::ErrorCode::invalid_argument) {
        std::cerr << "Unsupported builds must reject exact-version repair\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
