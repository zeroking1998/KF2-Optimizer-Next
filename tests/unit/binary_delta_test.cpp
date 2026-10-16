#include "kf2/flex/binary_delta.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
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
        "dde8343cf0226d25cd10610f41e2fc3e0c4795b8ecb98399668fe834aee340e5");
    CHECK(extension.original_sha256 ==
        "ca586d0fb5c85cbdea06b45e9f3e85354145520c5ecbc83036c77e759f4d20a0");
    CHECK(extension.target_sha256 ==
        "5adddfbd8fdbb7f98413936cab6ecc9ef631bb39021144a277400c1a3987fc8a");
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
