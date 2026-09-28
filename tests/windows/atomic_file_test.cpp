#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"
#include "platform/windows/atomic_file_retry.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool mutation_succeeded = false;
std::filesystem::path replacement_path;
bool atomic_swap_enabled = false;
bool swap_parent = false;
kf2::platform::windows::AtomicFileMutationStage atomic_swap_stage{};
std::filesystem::path preserved_path;

void swap_validated_path(
    kf2::platform::windows::AtomicFileMutationStage stage,
    const std::filesystem::path& path) {
    if (!atomic_swap_enabled || stage != atomic_swap_stage) return;
    atomic_swap_enabled = false;
    const auto subject = swap_parent ? path.parent_path() : path;
    const DWORD flags = swap_parent ? 0 : MOVEFILE_WRITE_THROUGH;
    const bool preserved = MoveFileExW(
        subject.c_str(), preserved_path.c_str(), flags) != FALSE;
    const bool replaced = preserved && MoveFileExW(
        replacement_path.c_str(), subject.c_str(),
        flags) != FALSE;
    mutation_succeeded = preserved && replaced;
}

void arm_atomic_swap(
    kf2::platform::windows::AtomicFileMutationStage stage,
    const std::filesystem::path& replacement,
    const std::filesystem::path& preserved, bool parent = false) {
    atomic_swap_stage = stage;
    replacement_path = replacement;
    preserved_path = preserved;
    swap_parent = parent;
    mutation_succeeded = false;
    atomic_swap_enabled = true;
    kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(
        &swap_validated_path);
}

void disarm_atomic_swap() {
    atomic_swap_enabled = false;
    kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(nullptr);
}

void grow_during_read(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    constexpr char replacement[] = "expanded while reading";
    DWORD written = 0;
    mutation_succeeded = WriteFile(
        file, replacement, static_cast<DWORD>(sizeof(replacement) - 1),
        &written, nullptr) != FALSE && written == sizeof(replacement) - 1;
    CloseHandle(file);
}

void truncate_during_read(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER retained{};
    retained.QuadPart = 1;
    mutation_succeeded = SetFilePointerEx(file, retained, nullptr, FILE_BEGIN) &&
                         SetEndOfFile(file);
    CloseHandle(file);
}

void replace_during_read(const std::filesystem::path& path) {
    mutation_succeeded = ReplaceFileW(
        path.c_str(), replacement_path.c_str(), nullptr,
        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
}

int main() {
    namespace fs = std::filesystem;

    std::vector<unsigned> attempts;
    std::vector<std::uint32_t> backoffs;
    for (unsigned attempt = 0;
         attempt != kf2::platform::windows::detail::atomic_replace_attempt_count;
         ++attempt) {
        attempts.push_back(attempt);
        const auto backoff =
            kf2::platform::windows::detail::atomic_replace_backoff_after(
                attempt, true);
        if (!backoff.has_value()) break;
        backoffs.push_back(backoff.value());
    }
    CHECK(attempts.size() == 8);
    CHECK((backoffs == std::vector<std::uint32_t>{10, 20, 40, 80, 160,
                                                  320, 640}));
    CHECK(kf2::platform::windows::detail::atomic_replace_backoff_after(
              0, false) == std::nullopt);

    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    fs::create_directories(root);

    const auto target = root / L"settings.ini";
    {
        std::ofstream old_file(target, std::ios::binary);
        old_file << "old";
    }

    const auto bounded =
        kf2::platform::windows::read_bounded_verified_file(target, 3);
    CHECK(bounded.has_value());
    CHECK(bounded.value() == "old");

    const auto empty = root / L"empty.ini";
    std::ofstream(empty, std::ios::binary);
    const auto empty_read =
        kf2::platform::windows::read_bounded_verified_file(empty, 0);
    CHECK(empty_read.has_value());
    CHECK(empty_read.value().empty());

    const auto missing_read =
        kf2::platform::windows::read_bounded_verified_file(
            root / L"missing.ini", 64);
    CHECK(!missing_read.has_value());
    CHECK(missing_read.error().code == kf2::ErrorCode::not_found);

    const auto oversized = root / L"oversized.ini";
    {
        std::ofstream output(oversized, std::ios::binary);
        output << std::string(65, 'x');
    }
    const auto oversized_read =
        kf2::platform::windows::read_bounded_verified_file(oversized, 64);
    CHECK(!oversized_read.has_value());
    CHECK(oversized_read.error().code == kf2::ErrorCode::access_denied);

    const auto changing_size = root / L"changing-size.ini";
    {
        std::ofstream output(changing_size, std::ios::binary);
        output << "small";
    }
    mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &grow_during_read);
    const auto changed_size =
        kf2::platform::windows::read_bounded_verified_file(changing_size, 64);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(mutation_succeeded);
    CHECK(!changed_size.has_value());
    CHECK(changed_size.error().code == kf2::ErrorCode::stale_data);

    const auto shortened = root / L"shortened.ini";
    {
        std::ofstream output(shortened, std::ios::binary);
        output << "original";
    }
    mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &truncate_during_read);
    const auto short_read =
        kf2::platform::windows::read_bounded_verified_file(shortened, 64);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(mutation_succeeded);
    CHECK(!short_read.has_value());
    CHECK(short_read.error().code == kf2::ErrorCode::stale_data);

    const auto changing_identity = root / L"changing-identity.ini";
    replacement_path = root / L"changing-identity-replacement.ini";
    {
        std::ofstream output(changing_identity, std::ios::binary);
        output << "original";
    }
    {
        std::ofstream output(replacement_path, std::ios::binary);
        output << "replaced";
    }
    mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &replace_during_read);
    const auto changed_identity =
        kf2::platform::windows::read_bounded_verified_file(
            changing_identity, 64);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(mutation_succeeded);
    CHECK(!changed_identity.has_value());
    CHECK(changed_identity.error().code == kf2::ErrorCode::stale_data);

    const auto replaced =
        kf2::platform::windows::atomic_replace_utf8(target, "new settings\n");
    CHECK(replaced.has_value());
    CHECK(read_bytes(target) == "new settings\n");
    CHECK(!fs::exists(fs::path{target.wstring() + L".tmp"}));

    const auto conditional = root / L"conditional.ini";
    {
        std::ofstream output(conditional, std::ios::binary);
        output << "conditional old";
    }
    const auto conditional_replaced =
        kf2::platform::windows::atomic_replace_utf8_if_unchanged(
            conditional, "conditional old", "conditional new");
    CHECK(conditional_replaced.has_value());
    CHECK(read_bytes(conditional) == "conditional new");

    const auto conflict =
        kf2::platform::windows::atomic_replace_utf8_if_unchanged(
            conditional, "stale preview", "must not survive");
    CHECK(!conflict.has_value());
    CHECK(conflict.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(conditional) == "conditional new");
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = entry.path().filename().wstring();
        CHECK(!name.starts_with(L"conditional.ini.tmp."));
        CHECK(!name.starts_with(L"conditional.ini.rollback."));
    }

    const auto missing_parent = root / L"missing" / L"settings.ini";
    const auto failed = kf2::platform::windows::atomic_replace_utf8(
        missing_parent, "must not appear");
    CHECK(!failed.has_value());
    CHECK(!fs::exists(missing_parent));
    CHECK(!fs::exists(fs::path{missing_parent.wstring() + L".tmp"}));

    const auto linked = root / L"linked.ini";
    const auto alias = root / L"linked-alias.ini";
    {
        std::ofstream output(linked, std::ios::binary);
        output << "linked old";
    }
    CHECK(CreateHardLinkW(alias.c_str(), linked.c_str(), nullptr) != FALSE);
    const auto hardlink_blocked =
        kf2::platform::windows::atomic_replace_utf8(linked, "must be blocked");
    CHECK(!hardlink_blocked.has_value());
    CHECK(hardlink_blocked.error().code == kf2::ErrorCode::access_denied);
    CHECK(read_bytes(linked) == "linked old");
    CHECK(read_bytes(alias) == "linked old");

    // A real process may hold an INI without delete sharing. The bounded
    // retry must fail closed, preserve the original bytes and remove its
    // unique temporary file instead of leaving transaction residue.
    const auto locked = root / L"locked.ini";
    {
        std::ofstream output(locked, std::ios::binary);
        output << "locked old";
    }
    HANDLE lock = CreateFileW(locked.c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    const auto lock_blocked =
        kf2::platform::windows::atomic_replace_utf8(locked, "must be blocked");
    CHECK(!lock_blocked.has_value());
    CHECK(lock_blocked.error().code == kf2::ErrorCode::io_failure);
    CHECK(lock_blocked.error().native_code == ERROR_SHARING_VIOLATION ||
          lock_blocked.error().native_code == ERROR_ACCESS_DENIED ||
          lock_blocked.error().native_code == ERROR_UNABLE_TO_REMOVE_REPLACED);
    CHECK(CloseHandle(lock) != FALSE);
    CHECK(read_bytes(locked) == "locked old");
    std::size_t locked_temporaries = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().wstring().starts_with(L"locked.ini.tmp."))
            ++locked_temporaries;
    }
    CHECK(locked_temporaries == 0);

    const auto trap_target = root / L"trap.ini";
    const auto legacy_temporary = fs::path{trap_target.wstring() + L".tmp"};
    {
        std::ofstream output(legacy_temporary, std::ios::binary);
        output << "unrelated legacy file";
    }
    const auto trap_replaced =
        kf2::platform::windows::atomic_replace_utf8(trap_target, "safe");
    CHECK(trap_replaced.has_value());
    CHECK(read_bytes(trap_target) == "safe");
    CHECK(read_bytes(legacy_temporary) == "unrelated legacy file");
    auto long_parent = root;
    while (long_parent.wstring().size() < 220) {
        long_parent /= L"long-path-segment";
    }
    fs::create_directories(long_parent);
    const auto long_target = long_parent / L"settings.ini";
    CHECK(long_target.wstring().size() < MAX_PATH);
    CHECK(long_target.wstring().size() + 32 >= MAX_PATH);
    const auto long_replaced =
        kf2::platform::windows::atomic_replace_utf8(
            long_target, "long path settings\n");
    CHECK(long_replaced.has_value());
    CHECK(read_bytes(long_target) == "long path settings\n");

    const auto parent_live = root / L"parent-live";
    const auto parent_original = root / L"parent-original";
    const auto parent_replacement = root / L"parent-replacement";
    fs::create_directories(parent_live);
    fs::create_directories(parent_replacement);
    const auto parent_target = parent_live / L"settings.ini";
    {
        std::ofstream output(parent_replacement / L"settings.ini",
                             std::ios::binary);
        output << "replacement parent content";
    }
    arm_atomic_swap(
        kf2::platform::windows::AtomicFileMutationStage::
            atomic_after_validation,
        parent_replacement, parent_original, true);
    const auto parent_swapped =
        kf2::platform::windows::atomic_replace_utf8(
            parent_target, "must not survive");
    disarm_atomic_swap();
    CHECK(mutation_succeeded);
    CHECK(!parent_swapped.has_value());
    CHECK(parent_swapped.error().code == kf2::ErrorCode::stale_data);
    CHECK(!fs::exists(parent_original / L"settings.ini"));
    CHECK(read_bytes(parent_live / L"settings.ini") ==
          "replacement parent content");

    const auto swapped_target = root / L"swapped-target.ini";
    const auto swapped_target_original = root / L"swapped-target-original.ini";
    const auto swapped_target_replacement =
        root / L"swapped-target-replacement.ini";
    {
        std::ofstream output(swapped_target, std::ios::binary);
        output << "original target content";
    }
    {
        std::ofstream output(swapped_target_replacement, std::ios::binary);
        output << "replacement target content";
    }
    arm_atomic_swap(
        kf2::platform::windows::AtomicFileMutationStage::
            atomic_after_validation,
        swapped_target_replacement, swapped_target_original);
    const auto target_swapped =
        kf2::platform::windows::atomic_replace_utf8(
            swapped_target, "must not survive");
    disarm_atomic_swap();
    CHECK(mutation_succeeded);
    CHECK(!target_swapped.has_value());
    CHECK(target_swapped.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(swapped_target_original) == "original target content");
    CHECK(read_bytes(swapped_target) == "replacement target content");

    const auto swapped_source = root / L"swapped-source.ini";
    const auto swapped_source_original = root / L"swapped-source-original.ini";
    const auto swapped_source_replacement =
        root / L"swapped-source-replacement.ini";
    {
        std::ofstream output(swapped_source, std::ios::binary);
        output << "original source content";
    }
    {
        std::ofstream output(swapped_source_replacement, std::ios::binary);
        output << "replacement source content";
    }
    arm_atomic_swap(
        kf2::platform::windows::AtomicFileMutationStage::
            quarantine_after_source_validation,
        swapped_source_replacement, swapped_source_original);
    const auto source_swapped =
        kf2::platform::windows::quarantine_regular_file(swapped_source);
    disarm_atomic_swap();
    CHECK(mutation_succeeded);
    CHECK(!source_swapped.has_value());
    CHECK(source_swapped.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(swapped_source_original) == "original source content");
    CHECK(read_bytes(swapped_source) == "replacement source content");

    const auto candidate_source = root / L"candidate-source.ini";
    const auto candidate = root / L"candidate-source.ini.corrupt";
    const auto candidate_original = root / L"candidate-original.ini";
    const auto candidate_replacement = root / L"candidate-replacement.ini";
    {
        std::ofstream output(candidate_source, std::ios::binary);
        output << "source content";
    }
    {
        std::ofstream output(candidate, std::ios::binary);
        output << "original candidate content";
    }
    {
        std::ofstream output(candidate_replacement, std::ios::binary);
        output << "replacement candidate content";
    }
    arm_atomic_swap(
        kf2::platform::windows::AtomicFileMutationStage::
            quarantine_after_candidate_validation,
        candidate_replacement, candidate_original);
    const auto candidate_swapped =
        kf2::platform::windows::quarantine_regular_file(
            candidate_source, L".corrupt", 1);
    disarm_atomic_swap();
    CHECK(mutation_succeeded);
    CHECK(!candidate_swapped.has_value());
    CHECK(candidate_swapped.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(candidate_original) == "original candidate content");
    CHECK(read_bytes(candidate) == "replacement candidate content");
    CHECK(read_bytes(candidate_source) == "source content");

    const auto corrupt = root / L"corrupt.ini";
    for (int iteration = 0; iteration < 7; ++iteration) {
        {
            std::ofstream output(corrupt, std::ios::binary);
            output << "broken " << iteration;
        }
        const auto quarantined =
            kf2::platform::windows::quarantine_regular_file(corrupt);
        CHECK(quarantined.has_value());
        CHECK(fs::exists(quarantined.value()));
        CHECK(!fs::exists(corrupt));
    }
    std::size_t quarantined_count = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().wstring().starts_with(L"corrupt.ini.corrupt"))
            ++quarantined_count;
    }
    CHECK(quarantined_count == 4);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
