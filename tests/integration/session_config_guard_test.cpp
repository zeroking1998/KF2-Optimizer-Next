#include "kf2/config/session_guard.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <new>
#include <string_view>
#include <Windows.h>

namespace {
thread_local int large_allocations_before_failure = -1;
thread_local bool allocation_failure_seen = false;
std::filesystem::path parser_fault_manifest;
unsigned parser_manifest_reads{};
std::string_view writer_fault_tail;

void fail_parser_line_allocation(const std::filesystem::path& path) {
    // Resume validates the tree before parsing the same manifest again.
    if (path == parser_fault_manifest && ++parser_manifest_reads == 2)
        large_allocations_before_failure = 2;
}

void fail_manifest_growth(std::ostream& output) {
    kf2::config::set_session_manifest_write_hook_for_testing(nullptr);
    large_allocations_before_failure = 0;
    output.write(writer_fault_tail.data(), writer_fault_tail.size());
}
}

void* operator new(std::size_t size) {
    if (large_allocations_before_failure >= 0 && size >= 512 &&
        large_allocations_before_failure-- == 0) {
        allocation_failure_seen = true;
        throw std::bad_alloc{};
    }
    for (;;) {
        if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc{};
        handler();
    }
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return __LINE__; } } while (0)

static void write(const std::filesystem::path& p, const char* text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary); f << text;
}
static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>{f}, {}};
}

enum class MutationKind { truncate, grow, rewrite, replace };

struct MutationPlan {
    MutationKind kind{MutationKind::truncate};
    std::filesystem::path target;
    std::filesystem::path replacement;
    bool attempted{};
    bool blocked{};
    bool injection_failed{};
};

static MutationPlan* mutation_plan{};
static std::filesystem::path denied_status_path;

static bool fail_selected_status(const std::filesystem::path& path,
                                 std::error_code& error) {
    if (path == denied_status_path) {
        error = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    return std::filesystem::exists(path, error);
}

static void mutate_during_read(const std::filesystem::path& path) {
    if (!mutation_plan || mutation_plan->attempted ||
        path != mutation_plan->target) return;
    mutation_plan->attempted = true;
    if (mutation_plan->kind == MutationKind::replace) {
        mutation_plan->blocked = ReplaceFileW(
            mutation_plan->target.c_str(),
            mutation_plan->replacement.c_str(), nullptr,
            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) == FALSE;
        return;
    }
    const HANDLE output = CreateFileW(
        mutation_plan->target.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        mutation_plan->injection_failed = true;
        return;
    }
    LARGE_INTEGER position{};
    DWORD written{};
    FILETIME original_write_time{};
    if (mutation_plan->kind == MutationKind::rewrite &&
        !GetFileTime(output, nullptr, nullptr, &original_write_time)) {
        mutation_plan->injection_failed = true;
    }
    if (mutation_plan->kind == MutationKind::truncate) {
        position.QuadPart = 1;
        mutation_plan->injection_failed =
            !SetFilePointerEx(output, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(output);
    } else if (mutation_plan->kind == MutationKind::grow) {
        position.QuadPart = 8;
        mutation_plan->injection_failed =
            !SetFilePointerEx(output, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(output);
    } else {
        mutation_plan->injection_failed = mutation_plan->injection_failed ||
            !WriteFile(output, "BBBB", 4, &written, nullptr) || written != 4;
    }
    mutation_plan->injection_failed =
        !FlushFileBuffers(output) || mutation_plan->injection_failed;
    if (mutation_plan->kind == MutationKind::rewrite &&
        !mutation_plan->injection_failed) {
        ULARGE_INTEGER changed{};
        changed.LowPart = original_write_time.dwLowDateTime;
        changed.HighPart = original_write_time.dwHighDateTime;
        changed.QuadPart += 10'000'000ULL;
        const FILETIME changed_write_time{
            changed.LowPart, changed.HighPart};
        mutation_plan->injection_failed =
            !SetFileTime(output, nullptr, nullptr, &changed_write_time);
    }
    CloseHandle(output);
}

static bool rejects_or_blocks_mutation(
    MutationPlan& plan, const std::filesystem::path& config,
    const std::filesystem::path& state) {
    mutation_plan = &plan;
    kf2::config::set_session_read_hook_for_testing(&mutate_during_read);
    const auto captured = kf2::config::capture_session_config(config, state);
    kf2::config::set_session_read_hook_for_testing(nullptr);
    mutation_plan = nullptr;
    if (!plan.attempted || plan.injection_failed) return false;
    if (plan.blocked) return captured.has_value();
    return !captured.has_value() &&
        !std::filesystem::exists(
            state / L"session-config/active/manifest.txt");
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code ec; fs::remove_all(root, ec);
    for (const auto kind : {MutationKind::truncate, MutationKind::grow,
                            MutationKind::rewrite, MutationKind::replace}) {
        const auto suffix = std::to_wstring(static_cast<int>(kind));
        const auto mutation_config = root / (L"MutationConfig" + suffix);
        const auto mutation_state = root / (L"MutationState" + suffix);
        MutationPlan plan;
        plan.kind = kind;
        plan.target = mutation_config / L"KFEngine.ini";
        plan.replacement = mutation_config / L"replacement.tmp";
        write(plan.target, "AAAA");
        if (kind == MutationKind::replace) {
            write(plan.replacement, "CCCC");
        }
        CHECK(rejects_or_blocks_mutation(
            plan, mutation_config, mutation_state));
    }

    const auto config = root / L"Config";
    write(config / L"KFEngine.ini", "engine-original");
    write(config / L"Nested-\u00e4\u4e2d/KFGame.ini", "game-original");
    write(config / L"KFSystemSettings.ini",
          "[SystemSettings]\r\nbAllowTemporalAA=True\r\n");
    write(config / L"ignored.txt", "not-protected");
    const auto inaccessible_state = root / L"InaccessibleState";
    denied_status_path =
        inaccessible_state / L"session-config" / L"active";
    kf2::config::set_session_status_hook_for_testing(&fail_selected_status);
    const auto inaccessible = kf2::config::resume_session_config(
        config, inaccessible_state);
    kf2::config::set_session_status_hook_for_testing(nullptr);
    denied_status_path.clear();
    CHECK(!inaccessible.has_value());
    CHECK(inaccessible.error().code == kf2::ErrorCode::io_failure);
    CHECK(inaccessible.error().native_code != 0);
    auto snapshot = kf2::config::capture_session_config(config, root / L"State");
    if (!snapshot.has_value()) std::wcerr << snapshot.error().message << L" native=" << snapshot.error().native_code << L'\n';
    CHECK(snapshot.has_value()); CHECK(snapshot.value().file_count == 3);

    const auto manifest_path = snapshot.value().snapshot_root / L"manifest.txt";
    const auto original_manifest = read(manifest_path);
    const auto record_marker = original_manifest.find("\nfile=");
    CHECK(record_marker != std::string::npos);
    const auto path_begin = record_marker + 1;
    const auto first_record_end = original_manifest.find('\n', path_begin);
    CHECK(first_record_end != std::string::npos);
    const auto valid_prefix = original_manifest.substr(0, first_record_end + 1);
    const std::string invalid_tail(64 * 1024, 'x');
    write(manifest_path, (valid_prefix + invalid_tail).c_str());
    parser_fault_manifest = manifest_path;
    parser_manifest_reads = 0;
    allocation_failure_seen = false;
    kf2::config::set_session_read_hook_for_testing(&fail_parser_line_allocation);
    const auto rejected_tail = kf2::config::resume_session_config(config, root / L"State");
    kf2::config::set_session_read_hook_for_testing(nullptr);
    const auto parser_large_allocations = 2 - large_allocations_before_failure;
    large_allocations_before_failure = -1;
    if (rejected_tail.has_value()) {
        std::cerr << "Parser accepted incomplete manifest; allocation fault="
                  << allocation_failure_seen << '\n';
    }
    CHECK(parser_manifest_reads == 2);
    CHECK(!allocation_failure_seen && parser_large_allocations == 1);
    CHECK(fs::exists(manifest_path));
    write(manifest_path, original_manifest.c_str());

    writer_fault_tail = invalid_tail;
    allocation_failure_seen = false;
    kf2::config::set_session_manifest_write_hook_for_testing(&fail_manifest_growth);
    const auto failed_capture = kf2::config::capture_session_config(config, root / L"WriterFault");
    kf2::config::set_session_manifest_write_hook_for_testing(nullptr);
    large_allocations_before_failure = -1;
    CHECK(allocation_failure_seen);
    if (failed_capture.has_value())
        std::cerr << "Capture committed a manifest after stream allocation failure\n";
    CHECK(!rejected_tail.has_value() && !failed_capture.has_value());
    CHECK(!fs::exists(root / L"WriterFault/session-config/active/manifest.txt"));
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    CHECK(read(config / L"Nested-\u00e4\u4e2d/KFGame.ini") == "game-original");
    const auto capture_retry = kf2::config::capture_session_config(config, root / L"WriterFault");
    CHECK(capture_retry.has_value() && capture_retry.value().file_count == 3);
    CHECK(kf2::config::complete_session_config(capture_retry.value()).has_value());

    // A known snapshot count must reject even a syntactically valid prefix.
    write(manifest_path, valid_prefix.c_str());
    write(config / L"KFEngine.ini", "must-not-change");
    CHECK(!kf2::config::restore_session_config(snapshot.value(), true).has_value());
    CHECK(read(config / L"KFEngine.ini") == "must-not-change");
    CHECK(!kf2::config::complete_session_config(snapshot.value()).has_value());
    CHECK(fs::exists(manifest_path));
    CHECK(fs::exists(snapshot.value().snapshot_root / L"files/KFEngine.ini"));
    CHECK(fs::exists(snapshot.value().snapshot_root / L"files/Nested-\u00e4\u4e2d/KFGame.ini"));
    CHECK(fs::exists(snapshot.value().snapshot_root / L"files/KFSystemSettings.ini"));
    write(config / L"KFEngine.ini", "engine-original");
    write(manifest_path, original_manifest.c_str());

    std::string crlf_manifest;
    for (const char character : original_manifest) {
        if (character == '\n') crlf_manifest += '\r';
        crlf_manifest += character;
    }
    crlf_manifest.resize(crlf_manifest.size() - 2);
    write(manifest_path, ("\r\n" + crlf_manifest).c_str());
    const auto compatible = kf2::config::resume_session_config(config, root / L"State");
    CHECK(compatible.has_value() && compatible.value()->file_count == 3);
    write(manifest_path, original_manifest.c_str());

    const auto path_end = original_manifest.find('|', path_begin + 5);
    CHECK(path_end != std::string::npos);
    for (const std::string malformed_path :
         {std::string{}, std::string{"0"}, std::string{"0G"},
          std::string{"4B46456e67696e652e696e69"}, std::string{"00"},
          std::string(8194, '0')}) {
        auto malformed = original_manifest;
        malformed.replace(path_begin + 5, path_end - path_begin - 5, malformed_path);
        write(manifest_path, malformed.c_str());
        CHECK(!kf2::config::resume_session_config(config, root / L"State").has_value());
    }
    write(manifest_path, original_manifest.c_str());
    const auto verified_manifest = kf2::config::resume_session_config(config, root / L"State");
    CHECK(verified_manifest.has_value() && verified_manifest.value().has_value());
    write(config / L"KFEngine.ini", "changed");
    write(config / L"Nested-\u00e4\u4e2d/KFGame.ini", "changed");
    write(config / L"KFSystemSettings.ini",
          "[SystemSettings]\r\nbAllowTemporalAA=False\r\n");
    write(config / L"New.ini", "created-by-game");
    write(config / L"ignored.txt", "changed-and-kept");
    auto restored = kf2::config::restore_session_config(snapshot.value());
    CHECK(restored.has_value() && restored.value() == 3);
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    CHECK(read(config / L"Nested-\u00e4\u4e2d/KFGame.ini") == "game-original");
    CHECK(read(config / L"KFSystemSettings.ini") ==
          "[SystemSettings]\r\nbAllowTemporalAA=False\r\n");
    CHECK(read(config / L"New.ini") == "created-by-game");
    CHECK(read(config / L"ignored.txt") == "changed-and-kept");
    auto retained = kf2::config::capture_session_config(config, root / L"State");
    CHECK(retained.has_value());
    write(retained.value().snapshot_root / L"graphics-replay.txt", "pending");
    write(config / L"KFEngine.ini", "temporary-session");
    CHECK(!kf2::config::recover_session_config(config, root / L"State", false).has_value());
    CHECK(read(config / L"KFEngine.ini") == "temporary-session");
    CHECK(kf2::config::restore_session_config(retained.value(), true).has_value());
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    CHECK(fs::exists(retained.value().snapshot_root / L"manifest.txt"));
    auto wrong_root = retained.value();
    wrong_root.root_file ^= 1;
    CHECK(!kf2::config::complete_session_config(wrong_root).has_value());
    CHECK(fs::exists(retained.value().snapshot_root / L"graphics-replay.txt"));
    CHECK(kf2::config::complete_session_config(retained.value()).has_value());
    auto second = kf2::config::capture_session_config(config, root / L"State");
    CHECK(second.has_value());
    write(config / L"KFEngine.ini", "changed-again");
    auto resumed = kf2::config::resume_session_config(config, root / L"State");
    CHECK(resumed.has_value() && resumed.value().has_value());
    CHECK(resumed.value()->file_count == 4);
    CHECK(read(config / L"KFEngine.ini") == "changed-again");
    const auto foreign = root / L"ForeignConfig";
    fs::create_directories(foreign);
    CHECK(!kf2::config::resume_session_config(
        foreign, root / L"State").has_value());
    CHECK(!kf2::config::recover_session_config(config, root / L"State", true).has_value());
    auto recovered = kf2::config::recover_session_config(config, root / L"State", false);
    CHECK(recovered.has_value() && recovered.value() == 4);
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    auto absent = kf2::config::resume_session_config(config, root / L"State");
    CHECK(absent.has_value() && !absent.value().has_value());

    auto linked = kf2::config::capture_session_config(config, root / L"State");
    CHECK(linked.has_value());
    const auto snapshot_file = linked.value().snapshot_root /
        L"files/KFEngine.ini";
    const auto snapshot_alias = linked.value().snapshot_root /
        L"files/KFEngine-alias.ini";
    CHECK(CreateHardLinkW(snapshot_alias.c_str(), snapshot_file.c_str(), nullptr) != FALSE);
    CHECK(!kf2::config::resume_session_config(
        config, root / L"State").has_value());
    fs::remove(snapshot_alias);
    CHECK(kf2::config::recover_session_config(
        config, root / L"State", false).has_value());
    return 0;
}
