#include "kf2/flex/flex_lab.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

#define CHECK(value) do { if (!(value)) { \
    std::cerr << "check failed line " << __LINE__ << '\n'; return false; \
} } while (false)

namespace {

namespace fs = std::filesystem;

void write(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}

std::map<fs::path, std::string> snapshot_files(const fs::path& root) {
    std::map<fs::path, std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.emplace(entry.path().lexically_relative(root), read(entry.path()));
        }
    }
    return files;
}

std::string field(std::string_view bytes, std::string_view key) {
    const auto start = bytes.find(key);
    if (start == std::string_view::npos) return {};
    const auto value = start + key.size();
    const auto end = bytes.find('\n', value);
    return end == std::string_view::npos ? std::string{} :
        std::string{bytes.substr(value, end - value)};
}

struct Fixture {
    fs::path root;
    fs::path game;
    fs::path state;
    fs::path sources;
    kf2::flex::OptimizedRuntimeTransactionOptions options;

    explicit Fixture(const fs::path& base, std::string_view name,
                     bool existing_helper = false)
        : root(base / name), game(root / L"game"), state(root / L"state"),
          sources(root / L"sources") {
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(game);
        fs::create_directories(sources);
        write(game / L"flexRelease_x64.dll", "stock-core");
        write(game / L"flexExtRelease_x64.dll", "stock-extension");
        if (existing_helper) write(game / L"kf2_p4_overlap.dll", "user-helper");
        write(sources / L"flexRelease_x64.forwarder-lab.dll", "relay");
        write(sources / L"flexRelease_x64.optimized.dll", "optimized-core");
        write(sources / L"flexExtRelease_x64.optimized.dll", "optimized-extension");
        write(sources / L"kf2_p4_overlap.dll", "optimized-helper");
        options = {
            .game_directory = game,
            .state_directory = state,
            .forwarder_dll = sources / L"flexRelease_x64.forwarder-lab.dll",
            .optimized_core_dll = sources / L"flexRelease_x64.optimized.dll",
            .optimized_extension_dll = sources / L"flexExtRelease_x64.optimized.dll",
            .optimized_helper_dll = sources / L"kf2_p4_overlap.dll",
            .game_running = false,
            .exact_runtime_verified = true};
    }
};

bool installed(const Fixture& fixture) {
    return read(fixture.game / L"flexRelease_x64.dll") == "relay" &&
        read(fixture.game / L"flexRelease_original.dll") == "optimized-core" &&
        read(fixture.game / L"flexExtRelease_x64.dll") == "optimized-extension" &&
        read(fixture.game / L"kf2_p4_overlap.dll") == "optimized-helper" &&
        fs::exists(fixture.state / L"flex-lab-transaction.marker");
}

bool restored(const Fixture& fixture, bool existing_helper) {
    return read(fixture.game / L"flexRelease_x64.dll") == "stock-core" &&
        read(fixture.game / L"flexExtRelease_x64.dll") == "stock-extension" &&
        !fs::exists(fixture.game / L"flexRelease_original.dll") &&
        !fs::exists(fixture.state / L"flex-lab-transaction.marker") &&
        (existing_helper
            ? read(fixture.game / L"kf2_p4_overlap.dll") == "user-helper"
            : !fs::exists(fixture.game / L"kf2_p4_overlap.dll"));
}

bool check_round_trip(const fs::path& root, bool existing_helper) {
    Fixture fixture(root, existing_helper ? "existing-helper" : "clean", existing_helper);
    const auto result =
        kf2::flex::install_optimized_runtime_transaction(fixture.options);
    CHECK(result.has_value() && result.value().installed);
    CHECK(installed(fixture));
    CHECK(!kf2::flex::recover_optimized_runtime_transaction(
        fixture.game, fixture.state, false).value());
    std::wstring details;
    CHECK(kf2::flex::restore_optimized_runtime_transaction(
        fixture.game, fixture.state, false, &details).has_value());
    CHECK(!details.empty());
    CHECK(restored(fixture, existing_helper));

    // Retained backups may be safely refreshed by a later session.
    CHECK(kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    CHECK(kf2::flex::restore_optimized_runtime_transaction(
        fixture.game, fixture.state, false).has_value());
    CHECK(restored(fixture, existing_helper));
    return true;
}

bool check_rollback(const fs::path& root) {
    Fixture fixture(root, "rollback");
    fixture.options.simulate_failure_after_install = true;
    CHECK(!kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    CHECK(restored(fixture, false));
    return true;
}

bool check_unknown_helper_preserved(const fs::path& root) {
    Fixture fixture(root, "unknown-helper");
    CHECK(kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    write(fixture.game / L"kf2_p4_overlap.dll", "unknown");
    CHECK(!kf2::flex::restore_optimized_runtime_transaction(
        fixture.game, fixture.state, false).has_value());
    CHECK(read(fixture.game / L"flexRelease_x64.dll") == "relay");
    CHECK(read(fixture.game / L"flexRelease_original.dll") == "optimized-core");
    CHECK(read(fixture.game / L"flexExtRelease_x64.dll") == "optimized-extension");
    CHECK(read(fixture.game / L"kf2_p4_overlap.dll") == "unknown");
    CHECK(fs::exists(fixture.state / L"flex-lab-transaction.marker"));
    write(fixture.game / L"kf2_p4_overlap.dll", "optimized-helper");
    CHECK(kf2::flex::restore_optimized_runtime_transaction(
        fixture.game, fixture.state, false).has_value());
    CHECK(restored(fixture, false));
    return true;
}

bool check_unknown_runtime_preserved(const fs::path& root) {
    constexpr std::array<std::pair<std::wstring_view, std::string_view>, 3> cases{{
        {L"flexRelease_x64.dll", "relay"},
        {L"flexRelease_original.dll", "optimized-core"},
        {L"flexExtRelease_x64.dll", "optimized-extension"},
    }};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        Fixture fixture(root, "unknown-runtime-" + std::to_string(index));
        CHECK(kf2::flex::install_optimized_runtime_transaction(
            fixture.options).has_value());
        const auto& [name, managed] = cases[index];
        const auto changed = fixture.game / fs::path{name};
        write(changed, "unknown");
        CHECK(!kf2::flex::restore_optimized_runtime_transaction(
            fixture.game, fixture.state, false).has_value());
        CHECK(read(changed) == "unknown");
        CHECK(fs::exists(fixture.state / L"flex-lab-transaction.marker"));
        write(changed, managed);
        CHECK(kf2::flex::restore_optimized_runtime_transaction(
            fixture.game, fixture.state, false).has_value());
        CHECK(restored(fixture, false));
    }
    return true;
}

bool check_gates(const fs::path& root) {
    Fixture fixture(root, "gates");
    fixture.options.game_running = true;
    CHECK(!kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    fixture.options.game_running = false;
    fixture.options.exact_runtime_verified = false;
    CHECK(!kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    fixture.options.exact_runtime_verified = true;
    fixture.options.optimized_helper_dll = fixture.sources / L"wrong.dll";
    write(fixture.options.optimized_helper_dll, "optimized-helper");
    CHECK(!kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    CHECK(read(fixture.game / L"flexRelease_x64.dll") == "stock-core");
    return true;
}

bool check_owner_binding(const fs::path& root) {
    Fixture owner(root, "owner");
    Fixture other(root, "other");
    CHECK(kf2::flex::install_optimized_runtime_transaction(
        owner.options).has_value());
    CHECK(!kf2::flex::restore_optimized_runtime_transaction(
        other.game, owner.state, false).has_value());
    CHECK(installed(owner));
    CHECK(read(other.game / L"flexRelease_x64.dll") == "stock-core");
    CHECK(kf2::flex::restore_optimized_runtime_transaction(
        owner.game, owner.state, false).has_value());
    return true;
}

bool prepare_legacy_recovery(Fixture& fixture, std::string_view state) {
    CHECK(kf2::flex::install_optimized_runtime_transaction(
        fixture.options).has_value());
    const auto current_marker = read(
        fixture.state / L"flex-lab-transaction.marker");
    const auto original = field(current_marker, "stock_core_sha256=");
    const auto forwarder = field(current_marker, "forwarder_sha256=");
    const auto owner = field(current_marker, "owner_sha256=");
    CHECK(original.size() == 64 && forwarder.size() == 64 && owner.size() == 64);
    CHECK(kf2::flex::restore_optimized_runtime_transaction(
        fixture.game, fixture.state, false).has_value());

    write(fixture.state / L"flexRelease_x64.pre-lab.dll", "stock-core");
    write(fixture.game / L"flexRelease_original.dll", "stock-core");
    write(fixture.game / L"flexRelease_x64.dll", "relay");
    write(fixture.state / L"flex-lab-transaction.marker",
        "schema=3\nstate=" + std::string{state} + "\noriginal_sha256=" + original +
        "\nforwarder_sha256=" + forwarder +
        "\nowner_sha256=" + owner + "\n");
    return true;
}

bool check_legacy_recovery(const fs::path& root) {
    for (unsigned index = 0; index < 4; ++index) {
        Fixture fixture(root, "legacy-recovery-" + std::to_string(index));
        CHECK(prepare_legacy_recovery(fixture,
            index % 2 == 0 ? "installed" : "installing"));
        if (index == 1) write(fixture.game / L"flexRelease_x64.dll", "stock-core");
        if (index == 2) fs::remove(fixture.game / L"flexRelease_original.dll");
        if (index == 3) {
            fs::remove(fixture.state / L"flexRelease_x64.pre-lab.dll");
            fs::remove(fixture.game / L"flexRelease_x64.dll");
        }
        std::wstring details;
        const auto recovered = kf2::flex::recover_optimized_runtime_transaction(
            fixture.game, fixture.state, false, &details);
        CHECK(recovered.has_value() && recovered.value());
        CHECK(restored(fixture, false));
        CHECK(!details.empty());
    }
    return true;
}

bool check_unknown_legacy_runtime_preserved(const fs::path& root) {
    constexpr std::array<std::pair<std::wstring_view, std::string_view>, 5> cases{{
        {L"game/flexRelease_x64.dll", "unknown"},
        {L"game/flexRelease_original.dll", "unknown"},
        {L"state/flexRelease_x64.pre-lab.dll", "unknown"},
        {L"game/flexRelease_original.dll", "relay"},
        {L"state/flexRelease_x64.pre-lab.dll", "relay"},
    }};
    for (const auto state : {"installing", "installed"}) {
        for (const bool recover : {false, true}) {
            for (std::size_t index = 0; index < cases.size(); ++index) {
                Fixture fixture(root, "unknown-legacy-" + std::string{state} +
                    (recover ? "-recover-" : "-restore-") + std::to_string(index));
                CHECK(prepare_legacy_recovery(fixture, state));
                const auto& [name, bytes] = cases[index];
                write(fixture.root / fs::path{name}, bytes);
                const auto before = snapshot_files(fixture.root);
                std::wstring details{L"previous recovery"};
                const auto result = recover
                    ? kf2::flex::recover_optimized_runtime_transaction(
                        fixture.game, fixture.state, false, &details)
                    : kf2::flex::restore_optimized_runtime_transaction(
                        fixture.game, fixture.state, false, &details);
                CHECK(!result.has_value());
                CHECK(result.error().code == kf2::ErrorCode::stale_data);
                CHECK(snapshot_files(fixture.root) == before);
                CHECK(details.empty());
            }
        }
    }
    return true;
}

}  // namespace

int main() {
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    if (error) return 2;
    const bool passed = check_round_trip(root, false) &&
        check_round_trip(root, true) && check_rollback(root) &&
        check_unknown_helper_preserved(root) &&
        check_unknown_runtime_preserved(root) && check_gates(root) &&
        check_owner_binding(root) && check_legacy_recovery(root) &&
        check_unknown_legacy_runtime_preserved(root);
    kf2::flex::set_runtime_install_test_hook(nullptr);
    kf2::flex::set_runtime_status_hook_for_testing(nullptr);
    fs::remove_all(root, error);
    return passed ? 0 : 1;
}
