#include "kf2/security/release_package_repair.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#define CHECK(expression) \
    do { if (!(expression)) return EXIT_FAILURE; } while (false)

int main(int argc, char** argv) {
    if (argc == 5) {
        const auto repaired =
            kf2::security::download_and_repair_release_package(
                std::filesystem::path{argv[1]},
                std::filesystem::path{argv[2]}, argv[3], argv[4]);
        if (!repaired.has_value()) {
            std::wcerr << repaired.error().message << L'\n';
            return EXIT_FAILURE;
        }
        if (repaired.value().repaired_files == 0 ||
            !repaired.value().restart_required) {
            return EXIT_FAILURE;
        }
        std::cout << "PASS: exact-version GitHub Auto Repair restored "
                  << repaired.value().repaired_files << " files\n";
        return EXIT_SUCCESS;
    }
    if (argc != 1) return EXIT_FAILURE;
    const auto alpha =
        kf2::security::exact_release_repair_plan("0.0.2-alpha");
    CHECK(alpha.has_value());
    CHECK(alpha.value().tag == L"v0.0.2-alpha");
    CHECK(alpha.value().asset_name ==
          L"KF2OptimizerNext-v0.0.2-alpha-win64.zip");
    CHECK(alpha.value().url.starts_with(L"https://github.com/"));
    CHECK(alpha.value().url.ends_with(
          L"/releases/download/v0.0.2-alpha/KF2OptimizerNext-v0.0.2-alpha-win64.zip"));
    CHECK(alpha.value().url.find(L"/latest/") == std::wstring::npos);

    const auto exact_intermediate =
        kf2::security::exact_release_repair_plan("0.0.27-alpha");
    CHECK(exact_intermediate.has_value());
    CHECK(exact_intermediate.value().tag == L"v0.0.27-alpha");

    CHECK(!kf2::security::exact_release_repair_plan("").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("../0.0.2-alpha").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("0.0.2 alpha").has_value());
    CHECK(!kf2::security::exact_release_repair_plan("0.0.2-alpha/other").has_value());
    return EXIT_SUCCESS;
}
