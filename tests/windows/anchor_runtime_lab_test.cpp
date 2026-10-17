#include "kf2/game/anchor_runtime_lab.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

void write(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, {}};
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace kf2::game;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    const auto game = root / L"game";
    const auto state = root / L"state";
    const auto source = root / L"assets" / L"kf2_anchor_runtime.dll";
    fs::create_directories(game);
    write(source, "anchor-runtime-v1");

    const AnchorRuntimeLabOptions options{
        .game_directory = game,
        .state_directory = state,
        .helper_asset = source};
    const auto target = game / L"kf2_anchor_runtime.dll";
    const auto marker = state / L"anchor-runtime-transaction.marker";

    CHECK(install_anchor_runtime_lab(options).has_value());
    CHECK(read(target) == "anchor-runtime-v1");
    CHECK(fs::exists(marker));
    const auto active = recover_anchor_runtime_lab(game, state, true);
    CHECK(active.has_value() && !active.value());
    CHECK(!restore_anchor_runtime_lab(game, state, true).has_value());
    CHECK(restore_anchor_runtime_lab(game, state, false).has_value());
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(marker));

    write(target, "user-owned-helper");
    CHECK(install_anchor_runtime_lab(options).has_value());
    CHECK(read(target) == "anchor-runtime-v1");
    CHECK(restore_anchor_runtime_lab(game, state, false).has_value());
    CHECK(read(target) == "user-owned-helper");

    fs::remove(target);
    auto failing = options;
    failing.simulate_failure_after_install = true;
    CHECK(!install_anchor_runtime_lab(failing).has_value());
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(marker));

    CHECK(install_anchor_runtime_lab(options).has_value());
    write(target, "foreign-replacement");
    const auto changed = restore_anchor_runtime_lab(game, state, false);
    CHECK(!changed.has_value());
    CHECK(read(target) == "foreign-replacement");
    CHECK(fs::exists(marker));
    write(target, "anchor-runtime-v1");
    CHECK(restore_anchor_runtime_lab(game, state, false).has_value());

    CHECK(install_anchor_runtime_lab(options).has_value());
    const auto other_game = root / L"other-game";
    fs::create_directories(other_game);
    CHECK(!restore_anchor_runtime_lab(other_game, state, false).has_value());
    CHECK(read(target) == "anchor-runtime-v1");
    CHECK(restore_anchor_runtime_lab(game, state, false).has_value());

    auto wrong_name = options;
    wrong_name.helper_asset = root / L"assets" / L"wrong.dll";
    write(wrong_name.helper_asset, "anchor-runtime-v1");
    CHECK(!install_anchor_runtime_lab(wrong_name).has_value());
    auto running = options;
    running.game_running = true;
    CHECK(!install_anchor_runtime_lab(running).has_value());

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
