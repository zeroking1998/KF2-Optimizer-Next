#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "kf2/game/game_discovery.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

void write_test_pe(const std::filesystem::path& path, WORD machine) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> bytes(512, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    const std::uint32_t pe_offset = 128;
    std::memcpy(bytes.data() + 0x3C, &pe_offset, sizeof(pe_offset));
    bytes[128] = 'P';
    bytes[129] = 'E';
    std::memcpy(bytes.data() + 132, &machine, sizeof(machine));
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

bool metadata_mutation_succeeded = false;
std::filesystem::path metadata_replacement;

void grow_metadata_during_read(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << std::string(1024, 'x');
    metadata_mutation_succeeded = output.good();
}

void replace_metadata_during_read(const std::filesystem::path& path) {
    metadata_mutation_succeeded = ReplaceFileW(
        path.c_str(), metadata_replacement.c_str(), nullptr,
        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
}

int main() {
    namespace fs = std::filesystem;
    const auto defaults = kf2::game::default_game_discovery_input();
    CHECK(defaults.has_value());
    CHECK(!defaults.value().allowed_config_parent.empty());
    CHECK(defaults.value().config_root.parent_path().filename() == L"KFGame");
    CHECK(defaults.value().config_root.filename() == L"Config");
    const auto libraries = kf2::game::parse_steam_library_folders(
        R"("libraryfolders" { "0" { "path" "C:\\Program Files (x86)\\Steam" } "1" { "path" "D:\\Games\\Steam" } })");
    CHECK(libraries.has_value());
    CHECK(libraries.value().size() == 2);
    CHECK(libraries.value()[1] == fs::path{L"D:\\Games\\Steam"});
    const auto malformed_library = kf2::game::parse_steam_library_folders(
        std::string{"\"path\" \"D:\\qbroken\""});
    CHECK(!malformed_library.has_value());
    CHECK(!kf2::game::parse_steam_library_folders("").has_value());
    const fs::path fixture{KF2_TEST_ROOT};
    fs::remove_all(fixture);
    const auto install = fixture / L"Steam/steamapps/common/KillingFloor2";
    const auto executable = install / L"Binaries/Win64/KFGame.exe";
    const auto documents = fixture / L"Documents";
    const auto config = documents / L"My Games/KillingFloor2/KFGame/Config";
    fs::create_directories(config);
    write_test_pe(executable, IMAGE_FILE_MACHINE_AMD64);

    const auto metadata = fixture / L"Steam/steamapps/libraryfolders.vdf";
    fs::create_directories(metadata.parent_path());
    {
        std::ofstream output(metadata, std::ios::binary);
        output << R"("libraryfolders" { "0" { "path" "D:\\Steam" } })";
    }
    metadata_mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &grow_metadata_during_read);
    const auto grown_metadata =
        kf2::game::read_steam_library_metadata_for_testing(metadata);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(metadata_mutation_succeeded);
    CHECK(!grown_metadata.has_value());
    CHECK(grown_metadata.error().code == kf2::ErrorCode::stale_data);

    {
        std::ofstream output(metadata, std::ios::binary | std::ios::trunc);
        output << "original";
    }
    metadata_replacement = fixture / L"Steam/steamapps/replacement.vdf";
    {
        std::ofstream output(metadata_replacement, std::ios::binary);
        output << "replacement";
    }
    metadata_mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &replace_metadata_during_read);
    const auto replaced_metadata =
        kf2::game::read_steam_library_metadata_for_testing(metadata);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(metadata_mutation_succeeded);
    CHECK(!replaced_metadata.has_value());
    CHECK(replaced_metadata.error().code == kf2::ErrorCode::stale_data);

    kf2::game::GameDiscoveryInput input{
        .manual_candidates = {install, install / L"."},
        .config_root = config,
        .allowed_config_parent = documents,
    };
    const auto found = kf2::game::discover_game_installation(input);
    CHECK(found.has_value());
    CHECK(found.value().executable == fs::weakly_canonical(executable));
    CHECK(found.value().install_root == fs::weakly_canonical(install));
    CHECK(found.value().source == kf2::game::DiscoverySource::manual);
    CHECK(found.value().duplicate_candidates_ignored == 1);
    CHECK(found.value().executable_identity.file_index != 0);

    const auto outside_binaries = fixture / L"outside-binaries";
    const auto outside_executable =
        outside_binaries / L"Win64/KFGame.exe";
    fs::remove_all(install / L"Binaries");
    write_test_pe(outside_executable, IMAGE_FILE_MACHINE_AMD64);
    std::error_code symlink_error;
    fs::create_directory_symlink(
        outside_binaries, install / L"Binaries", symlink_error);
    CHECK(!symlink_error);
    CHECK(!kf2::game::discover_game_installation(input).has_value());
    fs::remove(install / L"Binaries");
    write_test_pe(executable, IMAGE_FILE_MACHINE_AMD64);

    auto missing = input;
    missing.manual_candidates = {fixture / L"missing"};
    CHECK(!kf2::game::discover_game_installation(missing).has_value());

    write_test_pe(executable, IMAGE_FILE_MACHINE_I386);
    CHECK(!kf2::game::discover_game_installation(input).has_value());
    write_test_pe(executable, IMAGE_FILE_MACHINE_AMD64);

    auto foreign = input;
    foreign.config_root = fixture / L"Foreign/Config";
    fs::create_directories(foreign.config_root);
    CHECK(!kf2::game::discover_game_installation(foreign).has_value());

    fs::remove(executable);
    fs::create_directory(executable);
    CHECK(!kf2::game::discover_game_installation(input).has_value());
    fs::remove_all(fixture);
    return EXIT_SUCCESS;
}
