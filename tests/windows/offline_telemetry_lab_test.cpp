#include <Windows.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "kf2/game/offline_telemetry_lab.hpp"
#include "kf2/security/sha256.hpp"

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

std::string legacy_optimizer_module_bytes() {
    std::string bytes(64U * 1024U, '\0');
    bytes[0] = static_cast<char>(0xC1);
    bytes[1] = static_cast<char>(0x83);
    bytes[2] = static_cast<char>(0x2A);
    bytes[3] = static_cast<char>(0x9E);
    constexpr std::string_view names[] = {
        "KF2OptimizerTelemetryProbe",
        "KF2OptimizerTelemetryMutator",
        "KF2OptimizerTelemetryInteraction",
        "KF2OptimizerAdaptiveControlListener",
        "KF2OptimizerAdaptiveGraphics"};
    std::size_t offset = 256;
    for (const auto name : names) {
        bytes.replace(offset, name.size(), name);
        offset += name.size() + 64;
    }
    return bytes;
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
    const auto asset_bytes = read_bytes(asset);
    CHECK(asset_bytes.find("KF2OPT_MUTATOR") != std::string::npos);
    CHECK(asset_bytes.find("KF2OPT_INTERACTION") != std::string::npos);
    CHECK(asset_bytes.find("KF2OPT_TELEMETRY") != std::string::npos);
    CHECK(asset_bytes.find("KF2OPT_ADAPTIVE_BRIDGE") != std::string::npos);
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
    const auto target = config.parent_path() / L"Published" /
        L"BrewedPC" / L"KF2OptimizerTelemetry.u";
    CHECK(fs::exists(target));
    CHECK(read_bytes(target) == read_bytes(asset));
    CHECK(fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));
    CHECK(read_bytes(state / L"offline-telemetry-lab" / L"module.marker")
              .starts_with("schema=2\n"));
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
    const auto helper_while_owner_alive =
        run_offline_telemetry_cleanup_helper({
            .wait_process_id = GetCurrentProcessId(),
            .config_root = config,
            .state_root = state,
            .wait_timeout_ms = 1});
    CHECK(!helper_while_owner_alive.has_value());
    CHECK(fs::exists(target));
    const auto helper_after_owner_exit =
        run_offline_telemetry_cleanup_helper({
            .wait_process_id = UINT32_MAX,
            .config_root = config,
            .state_root = state,
            .wait_timeout_ms = 1});
    CHECK(helper_after_owner_exit.has_value());
    CHECK(helper_after_owner_exit.value());
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));

    CHECK(install_offline_telemetry_lab(options).has_value());
    HANDLE busy_target = CreateFileW(
        target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(busy_target != INVALID_HANDLE_VALUE);
    std::thread release_busy_target([busy_target]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CloseHandle(busy_target);
    });
    const auto restored_after_handle_release =
        restore_offline_telemetry_lab(config, state, false);
    release_busy_target.join();
    CHECK(restored_after_handle_release.has_value());
    CHECK(restored_after_handle_release.value());
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));

    CHECK(install_offline_telemetry_lab(options).has_value());
    const auto previous_module = legacy_optimizer_module_bytes();
    const auto previous_hash = kf2::security::sha256_hex(previous_module);
    CHECK(previous_hash.has_value());
    auto previous_marker = read_bytes(
        state / L"offline-telemetry-lab" / L"module.marker");
    const auto current_hash_offset = previous_marker.find(
        kOfflineTelemetryModuleSha256);
    CHECK(current_hash_offset != std::string::npos);
    previous_marker.replace(current_hash_offset, 64, previous_hash.value());
    write_bytes(target, previous_module);
    write_bytes(state / L"offline-telemetry-lab" / L"module.marker",
                previous_marker);
    const auto previous_version_recovered =
        recover_offline_telemetry_lab(config, state, false);
    CHECK(previous_version_recovered.has_value());
    CHECK(previous_version_recovered.value().cleaned);
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));

    CHECK(install_offline_telemetry_lab(options).has_value());
    const auto recovered = recover_offline_telemetry_lab(config, state, false);
    CHECK(recovered.has_value());
    CHECK(!recovered.value().active);
    CHECK(recovered.value().cleaned);
    CHECK(!fs::exists(target));

    const auto legacy_module = legacy_optimizer_module_bytes();
    write_bytes(target, legacy_module);
    const auto running_legacy = recover_offline_telemetry_lab(
        config, state, true);
    CHECK(running_legacy.has_value());
    CHECK(!running_legacy.value().active);
    CHECK(!running_legacy.value().cleaned);
    CHECK(read_bytes(target) == legacy_module);
    const auto recovered_legacy = recover_offline_telemetry_lab(
        config, state, false);
    CHECK(recovered_legacy.has_value());
    CHECK(recovered_legacy.value().cleaned);
    CHECK(!fs::exists(target));

    write_bytes(target, legacy_module);
    const auto replaced_legacy = install_offline_telemetry_lab(options);
    CHECK(replaced_legacy.has_value());
    CHECK(replaced_legacy.value());
    CHECK(read_bytes(target) == read_bytes(asset));
    CHECK(restore_offline_telemetry_lab(config, state, false).has_value());
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
    const auto oversized_asset =
        root / L"oversized" / L"KF2OptimizerTelemetry.u";
    write_bytes(oversized_asset, std::string(512U * 1024U + 1U, '\0'));
    auto oversized_options = options;
    oversized_options.module_asset = oversized_asset;
    CHECK(!install_offline_telemetry_lab(oversized_options).has_value());
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
