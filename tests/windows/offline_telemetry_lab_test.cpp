#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "kf2/game/offline_telemetry_lab.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace kf2::game;
    const fs::path root{KF2_TEST_ROOT};
    const fs::path asset{KF2_TELEMETRY_ASSET};
    if (!fs::exists(asset)) {
        std::cout << "SKIP: locally SDK-compiled telemetry asset is absent\n";
        return 77;
    }
    std::error_code error;
    fs::remove_all(root, error);
    const auto config = root / L"profile" / L"KFGame" / L"Config";
    const auto state = root / L"portable" / L"Data";
    fs::create_directories(config);
    fs::create_directories(state);

    const OfflineTelemetryLabOptions options{
        .config_root = config,
        .state_root = state,
        .module_asset = asset,
        .game_running = false};
    const auto installed = install_offline_telemetry_lab(options);
    if (!installed.has_value()) {
        std::wcerr << L"install error: " << installed.error().message
                   << L" native=" << installed.error().native_code << L'\n';
    }
    CHECK(installed.has_value());
    CHECK(installed.value());
    const auto target = root / L"profile" / L"KFGame" / L"Unpublished" /
        L"BrewedPC" / L"Script" / L"KF2OptimizerTelemetry.u";
    CHECK(fs::exists(target));
    CHECK(read_bytes(target) == read_bytes(asset));
    CHECK(fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));
    CHECK(!install_offline_telemetry_lab(options).has_value());

    const auto retained = recover_offline_telemetry_lab(config, state, true);
    CHECK(retained.has_value());
    CHECK(retained.value().active);
    CHECK(!retained.value().cleaned);
    CHECK(!restore_offline_telemetry_lab(config, state, true).has_value());

    const auto restored = restore_offline_telemetry_lab(config, state, false);
    CHECK(restored.has_value());
    CHECK(restored.value());
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));

    CHECK(install_offline_telemetry_lab(options).has_value());
    const auto recovered = recover_offline_telemetry_lab(config, state, false);
    CHECK(recovered.has_value());
    CHECK(!recovered.value().active);
    CHECK(recovered.value().cleaned);
    CHECK(!fs::exists(target));

    write_bytes(target, "foreign user package");
    CHECK(!install_offline_telemetry_lab(options).has_value());
    CHECK(read_bytes(target) == "foreign user package");
    fs::remove(target, error);

    const auto invalid_asset = root / L"wrong" / L"KF2OptimizerTelemetry.u";
    write_bytes(invalid_asset, "not the compiled telemetry package");
    auto invalid_options = options;
    invalid_options.module_asset = invalid_asset;
    CHECK(!install_offline_telemetry_lab(invalid_options).has_value());
    auto running_options = options;
    running_options.game_running = true;
    CHECK(!install_offline_telemetry_lab(running_options).has_value());

    CHECK(install_offline_telemetry_lab(options).has_value());
    write_bytes(target, "changed after installation");
    CHECK(!restore_offline_telemetry_lab(config, state, false).has_value());
    CHECK(read_bytes(target) == "changed after installation");

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
