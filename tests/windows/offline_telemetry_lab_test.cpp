#include <Windows.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
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

void throw_cleanup_allocation_failure() {
    throw std::bad_alloc{};
}

std::filesystem::path removal_replacement;
HANDLE removal_blocker{INVALID_HANDLE_VALUE};
bool removal_mutated{false};
kf2::game::OfflineTelemetryRemovalTestStage removal_mutation_stage{
    kf2::game::OfflineTelemetryRemovalTestStage::before_delete};

void replace_module_during_removal(
    kf2::game::OfflineTelemetryRemovalTestStage stage,
    const std::filesystem::path& path,
    std::uint32_t) {
    if (stage != removal_mutation_stage || removal_mutated) return;
    if (removal_blocker != INVALID_HANDLE_VALUE) {
        CloseHandle(removal_blocker);
        removal_blocker = INVALID_HANDLE_VALUE;
    }
    removal_mutated = ReplaceFileW(
        path.c_str(), removal_replacement.c_str(), nullptr,
        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
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

    // A replacement after authentication must survive cleanup and force a
    // recovery-required result instead of being deleted by its reused path.
    CHECK(install_offline_telemetry_lab(options).has_value());
    removal_replacement = root / L"replacement-before-delete.u";
    write_bytes(removal_replacement, "foreign replacement before delete");
    removal_mutation_stage =
        OfflineTelemetryRemovalTestStage::before_delete;
    removal_mutated = false;
    set_offline_telemetry_removal_test_hook(&replace_module_during_removal);
    const auto replacement_before_delete =
        restore_offline_telemetry_lab(config, state, false);
    set_offline_telemetry_removal_test_hook(nullptr);
    CHECK(removal_mutated);
    CHECK(!replacement_before_delete.has_value());
    CHECK(replacement_before_delete.error().code ==
          kf2::ErrorCode::stale_data);
    CHECK(read_bytes(target) == "foreign replacement before delete");
    CHECK(fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));
    write_bytes(target, asset_bytes);
    CHECK(restore_offline_telemetry_lab(config, state, false).has_value());

    // A transient sharing failure must not let the next retry delete a new
    // occupant without authenticating it again.
    CHECK(install_offline_telemetry_lab(options).has_value());
    removal_blocker = CreateFileW(
        target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(removal_blocker != INVALID_HANDLE_VALUE);
    removal_replacement = root / L"replacement-between-retries.u";
    write_bytes(removal_replacement, "foreign replacement between retries");
    removal_mutation_stage =
        OfflineTelemetryRemovalTestStage::retryable_failure;
    removal_mutated = false;
    set_offline_telemetry_removal_test_hook(&replace_module_during_removal);
    const auto replacement_between_retries =
        restore_offline_telemetry_lab(config, state, false);
    set_offline_telemetry_removal_test_hook(nullptr);
    if (removal_blocker != INVALID_HANDLE_VALUE) {
        CloseHandle(removal_blocker);
        removal_blocker = INVALID_HANDLE_VALUE;
    }
    CHECK(removal_mutated);
    CHECK(!replacement_between_retries.has_value());
    CHECK(replacement_between_retries.error().code ==
          kf2::ErrorCode::stale_data);
    CHECK(read_bytes(target) == "foreign replacement between retries");
    CHECK(fs::exists(state / L"offline-telemetry-lab" / L"module.marker"));
    write_bytes(target, asset_bytes);
    CHECK(restore_offline_telemetry_lab(config, state, false).has_value());

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
    write_bytes(oversized_asset, std::string(1024U * 1024U + 1U, '\0'));
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

    const auto rollback_root = root / L"rollback";
    const auto rollback_config =
        rollback_root / L"profile" / L"KFGame" / L"Config";
    const auto rollback_state = rollback_root / L"portable" / L"Data";
    fs::create_directories(rollback_config);
    fs::create_directories(rollback_state);
    write_bytes(rollback_state / L"offline-telemetry-lab", "blocked");
    const OfflineTelemetryLabOptions rollback_options{
        .config_root = rollback_config,
        .state_root = rollback_state,
        .module_asset = asset,
        .game_running = false};
    set_offline_telemetry_cleanup_test_hook(
        throw_cleanup_allocation_failure);
    const auto rollback_failure =
        install_offline_telemetry_lab(rollback_options);
    set_offline_telemetry_cleanup_test_hook(nullptr);
    CHECK(!rollback_failure.has_value());
    CHECK(rollback_failure.error().code == kf2::ErrorCode::access_denied);
    CHECK(rollback_failure.error().message !=
          L"Offline telemetry directory cleanup is incomplete");
    CHECK(fs::exists(rollback_config.parent_path() / L"Published" /
                     L"BrewedPC"));

    const auto restore_root = root / L"restore-cleanup";
    const auto restore_config =
        restore_root / L"profile" / L"KFGame" / L"Config";
    const auto restore_state = restore_root / L"portable" / L"Data";
    fs::create_directories(restore_config);
    fs::create_directories(restore_state);
    const OfflineTelemetryLabOptions restore_options{
        .config_root = restore_config,
        .state_root = restore_state,
        .module_asset = asset,
        .game_running = false};
    CHECK(install_offline_telemetry_lab(restore_options).has_value());
    const auto restore_target = restore_config.parent_path() / L"Published" /
        L"BrewedPC" / L"KF2OptimizerTelemetry.u";
    const auto restore_marker = restore_state / L"offline-telemetry-lab" /
        L"module.marker";
    set_offline_telemetry_cleanup_test_hook(
        throw_cleanup_allocation_failure);
    const auto restore_cleanup_failure = restore_offline_telemetry_lab(
        restore_config, restore_state, false);
    set_offline_telemetry_cleanup_test_hook(nullptr);
    CHECK(!restore_cleanup_failure.has_value());
    CHECK(restore_cleanup_failure.error().message ==
          L"Offline telemetry directory cleanup is incomplete");
    CHECK(!fs::exists(restore_target));
    CHECK(fs::exists(restore_marker));
    const auto restored_after_cleanup_failure = restore_offline_telemetry_lab(
        restore_config, restore_state, false);
    CHECK(restored_after_cleanup_failure.has_value());
    CHECK(restored_after_cleanup_failure.value());
    CHECK(!fs::exists(restore_marker));

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
