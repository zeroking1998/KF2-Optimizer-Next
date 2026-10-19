#include "kf2/flex/binary_delta.hpp"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}

template <class T>
void write_at(void* base, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof value);
}

template <class T>
T read_at(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset,
                sizeof value);
    return value;
}

bool close_to(float left, float right) {
    return std::abs(left - right) < 1.0e-6F;
}

bool verify_embedded_p1_cadence(const kf2::flex::BinaryDeltaRecipe& core) {
    constexpr std::size_t code_offset = 4203;
    constexpr std::size_t code_bytes = 0x102;
    constexpr std::size_t capture_offset = 0x180;
    if (core.literals.size() < code_offset + code_bytes) return false;

    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code) return false;
    std::memcpy(code, core.literals.data() + code_offset, code_bytes);

    // Replace the native-solver continuation with a local capture sink.
    code[0xFE] = 0xE9;
    const auto displacement = static_cast<std::int32_t>(
        capture_offset - (0xFE + 5));
    std::memcpy(code + 0xFF, &displacement, sizeof displacement);
    const std::uint8_t capture[]{
        0xF3, 0x0F, 0x11, 0x89, 0xB8, 0x05, 0x00, 0x00,
        0x44, 0x89, 0x81, 0xBC, 0x05, 0x00, 0x00, 0xC3};
    std::memcpy(code + capture_offset, capture, sizeof capture);
    DWORD old_protection{};
    if (!VirtualProtect(code, 0x1000, PAGE_EXECUTE_READ, &old_protection)) {
        VirtualFree(code, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 0x1000);

    using Cadence = void (*)(void*, float, int, void*);
    const auto cadence = reinterpret_cast<Cadence>(code);
    std::array<std::byte, 0x5C0> solver{};
    write_at(solver.data(), 0x198, 1);

    cadence(solver.data(), 0.01F, 1, nullptr);
    bool valid = read_at<unsigned>(solver.data(), 0x53C) == 0x80000000U &&
        close_to(read_at<float>(solver.data(), 0x5B8), 0.01F) &&
        read_at<int>(solver.data(), 0x5BC) == 1;

    write_at(solver.data(), 0x5B8, -1.0F);
    write_at(solver.data(), 0x5BC, -1);
    cadence(solver.data(), 0.02F, 1, nullptr);
    valid = valid && read_at<unsigned>(solver.data(), 0x53C) == 0x80000000U &&
        close_to(read_at<float>(solver.data(), 0x5B8), 0.02F) &&
        read_at<int>(solver.data(), 0x5BC) == 1 &&
        read_at<std::uint64_t>(solver.data(), 0x5B0) == 0;

    cadence(solver.data(), 0.03F, 3, nullptr);
    valid = valid && read_at<unsigned>(solver.data(), 0x53C) == 0x80000000U &&
        close_to(read_at<float>(solver.data(), 0x5B8), 0.03F) &&
        read_at<int>(solver.data(), 0x5BC) == 2 &&
        read_at<std::uint64_t>(solver.data(), 0x5B0) == 0;

    cadence(solver.data(), 0.04F, 1, nullptr);
    cadence(solver.data(), 0.05F, 1, nullptr);
    valid = valid && close_to(read_at<float>(solver.data(), 0x5B8), 0.05F) &&
        read_at<int>(solver.data(), 0x5BC) == 1;

    cadence(solver.data(), 0.06F, 1, nullptr);
    cadence(solver.data(), 0.0F, 4, nullptr);
    valid = valid && read_at<unsigned>(solver.data(), 0x53C) == 0 &&
        read_at<std::uint64_t>(solver.data(), 0x5B0) == 0 &&
        close_to(read_at<float>(solver.data(), 0x5B8), 0.0F) &&
        read_at<int>(solver.data(), 0x5BC) == 4;

    VirtualFree(code, 0, MEM_RELEASE);
    return valid;
}

}  // namespace

int main(int argc, char** argv) {
    using kf2::flex::BinaryDeltaOperation;
    using kf2::flex::BinaryDeltaSource;
    constexpr std::array<std::uint8_t, 6> original{1, 2, 3, 4, 5, 6};
    constexpr std::array<std::uint8_t, 3> literals{9, 8, 7};
    constexpr std::array operations{
        BinaryDeltaOperation{BinaryDeltaSource::original, 1, 3},
        BinaryDeltaOperation{BinaryDeltaSource::literal, 0, 2},
        BinaryDeltaOperation{BinaryDeltaSource::original, 5, 1},
    };
    const auto rebuilt = kf2::flex::apply_binary_delta(
        original, literals, operations, 6);
    CHECK(rebuilt.has_value());
    CHECK((rebuilt.value() ==
        std::vector<std::uint8_t>{2, 3, 4, 9, 8, 6}));

    constexpr std::array bad_source{
        BinaryDeltaOperation{BinaryDeltaSource::original, 5, 2},
    };
    CHECK(!kf2::flex::apply_binary_delta(
        original, literals, bad_source, 2).has_value());

    constexpr std::array bad_literal{
        BinaryDeltaOperation{BinaryDeltaSource::literal, 3, 1},
    };
    CHECK(!kf2::flex::apply_binary_delta(
        original, literals, bad_literal, 1).has_value());
    CHECK(!kf2::flex::apply_binary_delta(
        original, literals, operations, 5).has_value());
    CHECK(!kf2::flex::apply_binary_delta(
        original, literals, {}, 0).has_value());

    const auto& core = kf2::flex::optimized_core_recipe();
    const auto& extension = kf2::flex::optimized_extension_recipe();
    CHECK(core.original_sha256 ==
        "bc5bdb62250281455cf753ff9b7fff599e3e4ee2cfc4c406f4e7c9e99f21172f");
    CHECK(core.target_sha256 ==
        "78cea5b65c1cad623080fe75ca7feac13bd3aa646b0abf6cd09d1700beb0ffe0");
    CHECK(extension.original_sha256 ==
        "ca586d0fb5c85cbdea06b45e9f3e85354145520c5ecbc83036c77e759f4d20a0");
    CHECK(extension.target_sha256 ==
        "5eb6eb6828fb7c8e1ac24ca9a9986f5ebdfffa4e0eb90a325b0da7ddc32342f6");
    CHECK(core.literals.size() > 4414 && core.literals[4414] == 0x00);
    CHECK(extension.literals.size() > 320 && extension.literals[320] == 0x00);
    CHECK(verify_embedded_p1_cadence(core));
    CHECK(!core.operations.empty() && !core.literals.empty());
    CHECK(!extension.operations.empty() && !extension.literals.empty());

    if (argc == 5) {
        const auto verify = [](const kf2::flex::BinaryDeltaRecipe& recipe,
                               const char* original_path,
                               const char* expected_path) {
            const auto source = read_file(original_path);
            const auto expected = read_file(expected_path);
            const auto result = kf2::flex::apply_binary_delta(
                source, recipe.literals, recipe.operations, recipe.target_size);
            return !source.empty() && !expected.empty() && result.has_value() &&
                result.value() == expected;
        };
        CHECK(verify(core, argv[1], argv[2]));
        CHECK(verify(extension, argv[3], argv[4]));
    } else {
        CHECK(argc == 1);
    }
    return 0;
}
