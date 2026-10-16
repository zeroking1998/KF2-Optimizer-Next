#include "kf2/flex/optimized_runtime.hpp"

#include <cstdint>
#include <span>
#include <string_view>

#include "kf2/flex/binary_delta.hpp"
#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::flex {
namespace {

constexpr std::uintmax_t maximum_runtime_bytes = 16ULL * 1024ULL * 1024ULL;

Result<std::filesystem::path> prepare_one(
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const BinaryDeltaRecipe& recipe) {
    const auto bytes = platform::windows::read_bounded_verified_file(
        source, maximum_runtime_bytes);
    if (!bytes.has_value()) {
        return Result<std::filesystem::path>::failure(bytes.error());
    }
    const auto source_hash = security::sha256_hex(bytes.value());
    if (!source_hash.has_value()) {
        return Result<std::filesystem::path>::failure(source_hash.error());
    }
    if (source_hash.value() != recipe.original_sha256) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::stale_data,
             L"The installed FleX component does not match the supported KF2 runtime",
             0});
    }

    const auto cached_hash = security::sha256_file_hex(
        target, maximum_runtime_bytes);
    if (cached_hash.has_value() &&
        cached_hash.value() == recipe.target_sha256) {
        return Result<std::filesystem::path>::success(target);
    }

    const auto* begin = reinterpret_cast<const std::uint8_t*>(
        bytes.value().data());
    const auto rebuilt = apply_binary_delta(
        std::span<const std::uint8_t>{begin, bytes.value().size()},
        recipe.literals, recipe.operations, recipe.target_size);
    if (!rebuilt.has_value()) {
        return Result<std::filesystem::path>::failure(rebuilt.error());
    }
    const std::string_view rebuilt_bytes{
        reinterpret_cast<const char*>(rebuilt.value().data()),
        rebuilt.value().size()};
    const auto rebuilt_hash = security::sha256_hex(rebuilt_bytes);
    if (!rebuilt_hash.has_value()) {
        return Result<std::filesystem::path>::failure(rebuilt_hash.error());
    }
    if (rebuilt_hash.value() != recipe.target_sha256) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::stale_data,
             L"The optimized FleX component did not match its verified identity",
             0});
    }
    const auto written = platform::windows::atomic_replace_utf8(
        target, rebuilt_bytes);
    if (!written.has_value()) {
        return Result<std::filesystem::path>::failure(written.error());
    }
    const auto file_hash = security::sha256_file_hex(
        target, maximum_runtime_bytes);
    if (!file_hash.has_value() || file_hash.value() != recipe.target_sha256) {
        return Result<std::filesystem::path>::failure(
            {ErrorCode::io_failure,
             L"The prepared FleX component failed final verification", 0});
    }
    return Result<std::filesystem::path>::success(target);
}

}  // namespace

Result<PreparedOptimizedRuntime> prepare_optimized_runtime(
    const std::filesystem::path& stock_core,
    const std::filesystem::path& stock_extension,
    const std::filesystem::path& state_directory) {
    if (stock_core.filename() != L"flexRelease_x64.dll" ||
        stock_extension.filename() != L"flexExtRelease_x64.dll") {
        return Result<PreparedOptimizedRuntime>::failure(
            {ErrorCode::invalid_argument,
             L"Unexpected FleX runtime component name", 0});
    }
    std::error_code error;
    std::filesystem::create_directories(state_directory, error);
    if (error) {
        return Result<PreparedOptimizedRuntime>::failure(
            {ErrorCode::io_failure,
             L"The optimized FleX staging directory could not be created",
             static_cast<std::uint32_t>(error.value())});
    }
    const auto core_target = state_directory /
        L"flexRelease_x64.optimized.dll";
    const auto extension_target = state_directory /
        L"flexExtRelease_x64.optimized.dll";
    auto core = prepare_one(
        stock_core, core_target, optimized_core_recipe());
    if (!core.has_value()) {
        return Result<PreparedOptimizedRuntime>::failure(core.error());
    }
    auto extension = prepare_one(
        stock_extension, extension_target, optimized_extension_recipe());
    if (!extension.has_value()) {
        return Result<PreparedOptimizedRuntime>::failure(extension.error());
    }
    return Result<PreparedOptimizedRuntime>::success({
        std::move(core.value()), std::move(extension.value()),
        std::string{optimized_core_recipe().target_sha256},
        std::string{optimized_extension_recipe().target_sha256}});
}

}  // namespace kf2::flex
