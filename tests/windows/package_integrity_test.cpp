#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "kf2/security/package_integrity.hpp"
#include "kf2/security/sha256.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

namespace {
thread_local int fail_hash_allocation_after = -1;
}

void* operator new(std::size_t size) {
    if (size >= 32 && fail_hash_allocation_after >= 0 &&
        fail_hash_allocation_after-- == 0) throw std::bad_alloc{};
    for (;;) {
        if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc{};
        handler();
    }
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

constexpr std::pair<const wchar_t*, const char*> kFiles[]{
    {L"KF2Optimizer.exe", "test executable"},
    {L"Data/Lab/flexRelease_x64.forwarder-lab.dll", "test forwarder"},
    {L"Data/Lab/KF2OptimizerTelemetry.u", "test telemetry module"},
    {L"Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", "test matrix"},
    {L"Data/Documentation/README.md", "test documentation index"},
    {L"Data/Documentation/USER_GUIDE.md", "test user guide"},
    {L"Data/Documentation/UPDATES.md", "test update guide"},
    {L"Data/Documentation/FEATURE_REFERENCE.md", "test feature reference"},
    {L"Data/Documentation/SAFETY.md", "test safety guide"},
    {L"Data/Documentation/SUPPORT.md", "test support guide"},
    {L"Data/Documentation/LICENSE", "test license"},
    {L"Data/Documentation/THIRD_PARTY_NOTICES.md", "test notices"},
    {L"Data/Documentation/issue72-feature-inventory.json", "test inventory"},
};

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_package(const std::filesystem::path& root,
                   std::string_view identity) {
    std::string manifest =
        "schema_version=1\r\nproduct=KF2OptimizerNext\r\nsource_identity=" +
        std::string{identity} + "\r\nfile_count=13\r\n";
    for (const auto& [relative, content] : kFiles) {
        const auto path = root / relative;
        write_file(path, content);
        const auto hash = kf2::security::sha256_file_hex(path);
        if (!hash.has_value()) std::abort();
        std::string narrow;
        for (const wchar_t character : std::wstring_view{relative}) {
            narrow.push_back(character == L'\\' ? '/' :
                             static_cast<char>(character));
        }
        manifest += "file=" + narrow + "|" + hash.value() + "\r\n";
    }
    write_file(root / L"Data/package-integrity.ini", manifest);
}

using PackageSnapshot = std::vector<
    std::pair<std::filesystem::path, std::optional<std::string>>>;

PackageSnapshot capture_package(const std::filesystem::path& root) {
    PackageSnapshot snapshot;
    for (const auto& [relative, content] : kFiles) {
        (void)content;
        const auto path = root / relative;
        snapshot.emplace_back(
            path, std::filesystem::exists(path)
                      ? std::optional<std::string>{read_file(path)}
                      : std::nullopt);
    }
    const auto manifest = root / L"Data/package-integrity.ini";
    snapshot.emplace_back(
        manifest, std::filesystem::exists(manifest)
                      ? std::optional<std::string>{read_file(manifest)}
                      : std::nullopt);
    return snapshot;
}

bool matches_snapshot(const PackageSnapshot& snapshot) {
    for (const auto& [path, original] : snapshot) {
        if (std::filesystem::exists(path) != original.has_value()) return false;
        if (original.has_value() && read_file(path) != original.value()) {
            return false;
        }
    }
    return true;
}

void damage_repair_target(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::remove(
        root / L"Data/Lab/KF2OptimizerTelemetry.u", error);
    write_file(root / L"Data/Documentation/SAFETY.md", "damaged safety");
    write_file(root / L"Data/Documentation/SUPPORT.md", "damaged support");
    std::filesystem::remove(root / L"Data/package-integrity.ini", error);
}

std::filesystem::path concurrent_target;
std::size_t mutation_visit{};
std::size_t mutation_on_visit{};
bool concurrent_edit_written{};

void edit_during_atomic_commit(
    kf2::platform::windows::AtomicFileMutationStage stage,
    const std::filesystem::path& path) {
    using Stage = kf2::platform::windows::AtomicFileMutationStage;
    if (path != concurrent_target ||
        (stage != Stage::atomic_after_validation &&
         stage != Stage::conditional_after_validation &&
         stage != Stage::conditional_remove_before_lock) ||
        ++mutation_visit != mutation_on_visit) return;
    write_file(path, "concurrent edit");
    concurrent_edit_written = read_file(path) == "concurrent edit";
}

void create_after_first_repair(std::size_t count) {
    if (count != 1) return;
    write_file(concurrent_target, "concurrent edit");
    concurrent_edit_written = read_file(concurrent_target) == "concurrent edit";
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    // Published SHA-256 test vectors, independent of either helper's output.
    const std::pair<std::string, std::string_view> hash_vectors[]{
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {std::string(1'000'000, 'a'),
         "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    const auto hash_input = root / L"hash-input.bin";
    for (const auto& [bytes, expected] : hash_vectors) {
        const auto memory_hash = kf2::security::sha256_hex(bytes);
        CHECK(memory_hash.has_value() && memory_hash.value() == expected);
        write_file(hash_input, bytes);
        const auto file_hash = kf2::security::sha256_file_hex(hash_input);
        CHECK(file_hash.has_value() && file_hash.value() == expected);
    }
    write_file(hash_input, "abc");
    CHECK(!kf2::security::sha256_file_hex(hash_input, 2).has_value());
    CHECK(!kf2::security::sha256_file_hex(root / L"missing.bin").has_value());

    // Sweep actual buffer allocations; tiny checked-STL proxies are excluded.
    for (bool from_file : {false, true}) {
        int injected_failures = 0;
        bool completed = false;
        for (int index = 0; index < 32; ++index) {
            DWORD handles_before = 0;
            CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
            bool threw = false;
            bool correct = false;
            fail_hash_allocation_after = index;
            try {
                const auto hash = from_file
                    ? kf2::security::sha256_file_hex(hash_input)
                    : kf2::security::sha256_hex("abc");
                correct = hash.has_value() && hash.value() == hash_vectors[1].second;
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            const bool injected = fail_hash_allocation_after < 0;
            fail_hash_allocation_after = -1;
            DWORD handles_after = 0;
            CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
            CHECK(handles_after == handles_before);  // Includes file ownership.
            if (injected) {
                CHECK(threw);
                ++injected_failures;
            } else {
                CHECK(!threw && correct);
                completed = true;
                break;
            }
        }
        CHECK(completed && injected_failures >= 2);
    }

    const auto development =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(development.has_value());
    CHECK(!development.value().managed_package);
    CHECK(development.value().verified);

    write_package(root, "test-build");
    CHECK(kf2::security::managed_package_payload_paths().size() == 13);
    const auto source_identity = kf2::security::package_source_identity(root);
    CHECK(source_identity.has_value());
    CHECK(source_identity.value() == "test-build");
    auto invalid_identity = read_file(root / L"Data/package-integrity.ini");
    const auto identity_offset = invalid_identity.find("source_identity=test-build");
    CHECK(identity_offset != std::string::npos);
    invalid_identity.replace(identity_offset,
                             std::string_view{"source_identity=test-build"}.size(),
                             "source_identity=bad identity");
    write_file(root / L"Data/package-integrity.ini", invalid_identity);
    CHECK(!kf2::security::package_source_identity(root).has_value());
    write_package(root, "test-build");
    const auto verified =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(verified.has_value());
    CHECK(verified.value().managed_package);
    CHECK(verified.value().verified);
    CHECK(verified.value().verified_files == 13);

    write_file(root / L"Data/Documentation/FEATURE_REFERENCE.md", "damaged");
    const auto damaged =
        kf2::security::audit_package_integrity(root, "test-build");
    CHECK(damaged.has_value());
    CHECK(damaged.value().managed_package);
    CHECK(!damaged.value().verified);

    const auto mixed_build =
        kf2::security::audit_package_integrity(root, "other-build");
    CHECK(mixed_build.has_value());
    CHECK(!mixed_build.value().verified);

    const auto repair_source = root / L"repair-source";
    const auto repair_target = root / L"repair-target";
    write_package(repair_source, "test-build");
    write_package(repair_target, "test-build");
    CHECK(fs::remove(repair_target /
                     L"Data/Lab/KF2OptimizerTelemetry.u"));
    write_file(repair_target / L"Data/Documentation/SAFETY.md", "damaged");
    CHECK(fs::remove(repair_target / L"Data/package-integrity.ini"));

    const auto repaired = kf2::security::repair_package_from_directory(
        repair_target, repair_source, "test-build");
    CHECK(repaired.has_value());
    CHECK(repaired.value().repaired_files == 3);
    CHECK(repaired.value().already_valid_files == 11);
    CHECK(repaired.value().restart_required);
    const auto repaired_audit =
        kf2::security::audit_package_integrity(repair_target, "test-build");
    CHECK(repaired_audit.has_value());
    CHECK(repaired_audit.value().verified);

    const auto unchanged = kf2::security::repair_package_from_directory(
        repair_target, repair_source, "test-build");
    CHECK(unchanged.has_value());
    CHECK(unchanged.value().repaired_files == 0);
    CHECK(unchanged.value().already_valid_files == 13);
    CHECK(!unchanged.value().restart_required);

    for (int scenario = 0; scenario < 4; ++scenario) {
        const bool during_rollback = (scenario & 1) != 0;
        const bool initially_missing = (scenario & 2) != 0;
        write_package(repair_target, "test-build");
        concurrent_target = repair_target / L"Data/Documentation/USER_GUIDE.md";
        write_file(concurrent_target, "damaged user guide");
        if (initially_missing) CHECK(fs::remove(concurrent_target));
        mutation_visit = 0;
        mutation_on_visit = during_rollback ? 2 : 1;
        concurrent_edit_written = false;
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(
            edit_during_atomic_commit);
        if (during_rollback) {
            kf2::security::set_package_repair_fault_for_testing(
                kf2::security::PackageRepairFaultInjection::after_replacement, 1);
        }
        const auto raced = kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
        kf2::platform::windows::set_atomic_file_mutation_hook_for_testing(nullptr);
        kf2::security::set_package_repair_fault_for_testing(
            kf2::security::PackageRepairFaultInjection::none);
        CHECK(concurrent_edit_written);
        CHECK(read_file(concurrent_target) == "concurrent edit");
        CHECK(!raced.has_value());
        if (during_rollback) {
            CHECK(raced.error().code == kf2::ErrorCode::recovery_required);
        } else if (!initially_missing) {
            CHECK(raced.error().code == kf2::ErrorCode::stale_data);
        }
    }

    // A missing planned target can appear before the atomic helper observes it.
    write_package(repair_target, "test-build");
    const auto earlier_target = repair_target / L"Data/Documentation/README.md";
    write_file(earlier_target, "original damaged readme");
    CHECK(fs::remove(concurrent_target));
    concurrent_edit_written = false;
    kf2::security::set_package_repair_progress_for_testing(create_after_first_repair);
    const auto appeared = kf2::security::repair_package_from_directory(
        repair_target, repair_source, "test-build");
    kf2::security::set_package_repair_progress_for_testing(nullptr);
    CHECK(concurrent_edit_written);
    CHECK(!appeared.has_value());
    CHECK(appeared.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_file(concurrent_target) == "concurrent edit");
    CHECK(read_file(earlier_target) == "original damaged readme");

    for (const std::size_t interruption : {1U, 2U, 4U}) {
        write_package(repair_target, "test-build");
        damage_repair_target(repair_target);
        const auto before = capture_package(repair_target);
        kf2::security::set_package_repair_fault_for_testing(
            kf2::security::PackageRepairFaultInjection::after_replacement,
            interruption);
        const auto interrupted =
            kf2::security::repair_package_from_directory(
                repair_target, repair_source, "test-build");
        kf2::security::set_package_repair_fault_for_testing(
            kf2::security::PackageRepairFaultInjection::none);
        CHECK(!interrupted.has_value());
        CHECK(matches_snapshot(before));
    }

    write_package(repair_target, "test-build");
    damage_repair_target(repair_target);
    const auto before_final_verification = capture_package(repair_target);
    kf2::security::set_package_repair_fault_for_testing(
        kf2::security::PackageRepairFaultInjection::final_verification);
    const auto final_verification_failure =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    kf2::security::set_package_repair_fault_for_testing(
        kf2::security::PackageRepairFaultInjection::none);
    CHECK(!final_verification_failure.has_value());
    CHECK(matches_snapshot(before_final_verification));

    write_package(repair_target, "test-build");
    damage_repair_target(repair_target);
    const auto before_failed_rollback = capture_package(repair_target);
    kf2::security::set_package_repair_fault_for_testing(
        kf2::security::PackageRepairFaultInjection::rollback_failure, 2U);
    const auto failed_rollback =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    kf2::security::set_package_repair_fault_for_testing(
        kf2::security::PackageRepairFaultInjection::none);
    CHECK(!failed_rollback.has_value());
    CHECK(failed_rollback.error().code == kf2::ErrorCode::recovery_required);
    CHECK(failed_rollback.error().message.find(L"Restart") !=
          std::wstring::npos);
    CHECK(!matches_snapshot(before_failed_rollback));
    write_package(repair_target, "test-build");

    write_file(repair_target / L"Data/Documentation/SAFETY.md",
               "target must remain unchanged");
    write_file(repair_source /
                   L"Data/Documentation/FEATURE_REFERENCE.md",
               "tampered source");
    const auto tampered_source =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    CHECK(!tampered_source.has_value());
    CHECK(read_file(repair_target / L"Data/Documentation/SAFETY.md") ==
          "target must remain unchanged");
    write_package(repair_source, "test-build");

    write_file(repair_target / L"KF2Optimizer.exe", "different executable");
    const auto wrong_executable =
        kf2::security::repair_package_from_directory(
            repair_target, repair_source, "test-build");
    CHECK(!wrong_executable.has_value());

    const auto wrong_build = kf2::security::repair_package_from_directory(
        repair_source, repair_source, "other-build");
    CHECK(!wrong_build.has_value());
    return EXIT_SUCCESS;
}
