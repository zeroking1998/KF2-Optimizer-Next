#include "kf2/game/frame_rate_cap.hpp"
#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "kf2/platform/windows/atomic_file.hpp"

namespace fs = std::filesystem;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __LINE__ << ": " #condition << '\n';                   \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

fs::path blocked_game;
fs::path blocked_console;
HANDLE game_lock = INVALID_HANDLE_VALUE;
HANDLE console_lock = INVALID_HANDLE_VALUE;
fs::path readback_target;
int cap_read_count{};
void change_cap_readback(const fs::path& path) {
    if (path == readback_target && ++cap_read_count == 2) {
        write_bytes(path, "external-change-at-readback");
    }
}
fs::path blocked_journal;
HANDLE journal_lock = INVALID_HANDLE_VALUE;
void block_journal_completion(
    kf2::platform::windows::AtomicFileMutationStage stage, const fs::path& path) {
    if (path == blocked_journal && stage ==
        kf2::platform::windows::AtomicFileMutationStage::conditional_after_validation) {
        journal_lock = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
}
void lock_cap_transaction(
    kf2::platform::windows::AtomicFileMutationStage stage, const fs::path& path) {
    if (path != blocked_game ||
        (stage != kf2::platform::windows::AtomicFileMutationStage::atomic_after_validation &&
         stage != kf2::platform::windows::AtomicFileMutationStage::conditional_after_validation)) return;
    game_lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (!blocked_console.empty()) {
        console_lock = CreateFileW(blocked_console.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
}

}  // namespace

int main() {
    const fs::path root{KF2_TEST_ROOT};
    std::error_code ignored;
    fs::remove_all(root, ignored);

    const fs::path install_root = root / L"game";
    const fs::path config_root = root / L"user-config";
    const fs::path console_variables =
        install_root / L"Engine/Config/ConsoleVariables.ini";
    const fs::path game_ini = config_root / L"KFGame.ini";
    write_bytes(console_variables,
        "; native startup variables\r\n[Startup]\r\n\r\n");
    write_bytes(game_ini,
        "[KFGame.KFGameEngine]\r\n"
        "bSmoothFrameRate=False\r\n"
        "MinSmoothedFrameRate=5.000000\r\n"
        "MaxSmoothedFrameRate=122.000000\r\n");

    kf2::game::GameInstallation installation;
    installation.install_root = install_root;
    installation.config_root = config_root;

    const auto applied = kf2::game::persist_frame_rate_cap(
        installation, 120);
    CHECK(applied.has_value());
    CHECK(applied.value().changed);
    CHECK(applied.value().target_fps == 120);
    const auto console_after = read_bytes(console_variables);
    CHECK(console_after.find("[Startup]\r\n") != std::string::npos);
    CHECK(console_after.find("t.MaxFPS=120\r\n") != std::string::npos);
    const auto game_after = read_bytes(game_ini);
    CHECK(game_after.find("bSmoothFrameRate=True\r\n") != std::string::npos);
    CHECK(game_after.find("MinSmoothedFrameRate=22.000000\r\n") !=
          std::string::npos);
    CHECK(game_after.find("MaxSmoothedFrameRate=120.000000\r\n") !=
          std::string::npos);

    const auto unchanged = kf2::game::persist_frame_rate_cap(
        installation, 120);
    CHECK(unchanged.has_value());
    CHECK(!unchanged.value().changed);
    CHECK(read_bytes(console_variables) == console_after);
    CHECK(read_bytes(game_ini) == game_after);

    for (int target = 30; target <= 240; ++target) {
        const auto exact = kf2::game::persist_frame_rate_cap(
            installation, target);
        CHECK(exact.has_value());
        CHECK(exact.value().target_fps == target);
        CHECK(read_bytes(console_variables).find(
                  "t.MaxFPS=" + std::to_string(target) + "\r\n") !=
              std::string::npos);
        CHECK(read_bytes(game_ini).find(
                  "MaxSmoothedFrameRate=" + std::to_string(target) +
                  ".000000\r\n") != std::string::npos);
    }
    const auto all_values_console = read_bytes(console_variables);
    const auto all_values_game = read_bytes(game_ini);

    for (bool fail_rollback : {false, true}) {
        write_bytes(console_variables, all_values_console);
        write_bytes(game_ini, all_values_game);
        blocked_game = game_ini;
        blocked_console = fail_rollback ? console_variables : fs::path{};
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(lock_cap_transaction);
        const auto failed = kf2::game::persist_frame_rate_cap(installation, 90);
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(nullptr);
        const bool locked = game_lock != INVALID_HANDLE_VALUE &&
            (!fail_rollback || console_lock != INVALID_HANDLE_VALUE);
        if (game_lock != INVALID_HANDLE_VALUE) CloseHandle(game_lock);
        if (console_lock != INVALID_HANDLE_VALUE) CloseHandle(console_lock);
        game_lock = console_lock = INVALID_HANDLE_VALUE;
        CHECK(locked);
        CHECK(!failed.has_value());
        CHECK(failed.error().code == (fail_rollback ? kf2::ErrorCode::recovery_required : kf2::ErrorCode::io_failure));
        CHECK(read_bytes(game_ini) == all_values_game);
        CHECK(fail_rollback || read_bytes(console_variables) == all_values_console);
        const auto journal = config_root / L"frame-rate-cap.recovery";
        CHECK(read_bytes(journal).empty() != fail_rollback);
        if (fail_rollback) {
            const auto debt = read_bytes(journal);
            const auto partial_console = read_bytes(console_variables);
            CHECK(partial_console.find("t.MaxFPS=90") != std::string::npos);
            const auto running = kf2::game::recover_frame_rate_cap(installation, {}, true);
            CHECK(!running.has_value());
            CHECK(read_bytes(console_variables) == partial_console);
            CHECK(read_bytes(journal) == debt);
            write_bytes(console_variables, "concurrent-user-change");
            const auto conflict = kf2::game::persist_frame_rate_cap(installation, 119);
            CHECK(!conflict.has_value() && conflict.error().code == kf2::ErrorCode::recovery_required);
            CHECK(read_bytes(console_variables) == "concurrent-user-change");
            CHECK(read_bytes(journal) == debt);
            write_bytes(console_variables, partial_console);
            auto corrupt = debt;
            corrupt.back() ^= 1;
            write_bytes(journal, corrupt);
            CHECK(!kf2::game::recover_frame_rate_cap(installation).has_value());
            CHECK(read_bytes(console_variables) == partial_console);
            CHECK(read_bytes(journal) == corrupt);
            write_bytes(journal, debt);
            auto foreign = installation;
            foreign.install_root = root / L"another-game";
            CHECK(!kf2::game::recover_frame_rate_cap(foreign).has_value());
            CHECK(read_bytes(journal) == debt);
            const auto recovered = kf2::game::recover_frame_rate_cap(installation);
            CHECK(recovered.has_value() && recovered.value());
            CHECK(read_bytes(console_variables) == all_values_console);
            CHECK(read_bytes(game_ini) == all_values_game);
            CHECK(read_bytes(journal).empty());
            const auto again = kf2::game::recover_frame_rate_cap(installation);
            CHECK(again.has_value() && !again.value());
        }
    }
    write_bytes(console_variables, all_values_console);
    write_bytes(game_ini, all_values_game);

    // A readback conflict preserves the other writer and still rolls back the
    // other cap file. The unresolved record cannot be replaced by a new request.
    readback_target = console_variables;
    cap_read_count = 0;
    kf2::platform::windows::set_bounded_read_hook_for_testing(change_cap_readback);
    const auto mismatch = kf2::game::persist_frame_rate_cap(installation, 90);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(!mismatch.has_value() && mismatch.error().code == kf2::ErrorCode::recovery_required);
    CHECK(read_bytes(console_variables) == "external-change-at-readback");
    CHECK(read_bytes(game_ini) == all_values_game);
    const auto journal = config_root / L"frame-rate-cap.recovery";
    CHECK(!read_bytes(journal).empty());
    write_bytes(console_variables, all_values_console);
    CHECK(kf2::game::recover_frame_rate_cap(installation).has_value());

    // Failure to retire a verified transaction must also leave durable intent.
    blocked_journal = journal;
    kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(block_journal_completion);
    const auto finalization = kf2::game::persist_frame_rate_cap(installation, 90);
    kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(nullptr);
    const bool completion_blocked = journal_lock != INVALID_HANDLE_VALUE;
    if (completion_blocked) CloseHandle(journal_lock);
    journal_lock = INVALID_HANDLE_VALUE;
    CHECK(completion_blocked);
    CHECK(!finalization.has_value() && finalization.error().code == kf2::ErrorCode::recovery_required);
    CHECK(read_bytes(console_variables).find("t.MaxFPS=90") != std::string::npos);
    CHECK(read_bytes(game_ini).find("MaxSmoothedFrameRate=90.000000") != std::string::npos);
    CHECK(!read_bytes(journal).empty());
    CHECK(kf2::game::recover_frame_rate_cap(installation).has_value());
    CHECK(read_bytes(console_variables) == all_values_console);
    CHECK(read_bytes(game_ini) == all_values_game);
    CHECK(read_bytes(journal).empty());

    const auto invalid = kf2::game::persist_frame_rate_cap(
        installation, 241);
    CHECK(!invalid.has_value());
    CHECK(read_bytes(console_variables) == all_values_console);
    CHECK(read_bytes(game_ini) == all_values_game);

    write_bytes(console_variables,
        "[Startup]\r\nt.MaxFPS=120\r\nt.MaxFPS=144\r\n");
    const auto ambiguous = kf2::game::persist_frame_rate_cap(
        installation, 90);
    CHECK(!ambiguous.has_value());
    CHECK(read_bytes(console_variables).find("t.MaxFPS=90") ==
          std::string::npos);

    write_bytes(console_variables, "[Startup]\r\nt.MaxFPS=120\r\n");
    const auto game_before_oversized_read = read_bytes(game_ini);
    fs::resize_file(console_variables, 4U * 1024U * 1024U + 1U);
    const auto oversized = kf2::game::persist_frame_rate_cap(
        installation, 90);
    CHECK(!oversized.has_value());
    CHECK(oversized.error().code == kf2::ErrorCode::access_denied);
    CHECK(read_bytes(game_ini) == game_before_oversized_read);

    fs::remove_all(root, ignored);
    return EXIT_SUCCESS;
}
