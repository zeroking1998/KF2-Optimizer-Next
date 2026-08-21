#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "kf2/game/game_discovery.hpp"

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
