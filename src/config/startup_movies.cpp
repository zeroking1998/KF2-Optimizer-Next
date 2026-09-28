#include "kf2/config/startup_movies.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "kf2/config/ini_document.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::config {
namespace {

constexpr std::uintmax_t kMaximumConfigBytes = 16U * 1024U * 1024U;
constexpr std::wstring_view kEngineIni = L"KFEngine.ini";
constexpr std::wstring_view kMovieSection = L"FullScreenMovie";
constexpr std::wstring_view kStartupMovieKey = L"StartupMovies";
constexpr std::array<std::wstring_view, 4> kStartupLogos{
    L"LogoTripwire", L"LogoHardsuit", L"LogoUE3", L"LogoGA"};

Result<std::string> read_engine_ini(const std::filesystem::path& path) {
    return platform::windows::read_bounded_verified_file(
        path, kMaximumConfigBytes);
}

}  // namespace

Result<StartupLogoSkipResult> stage_startup_logo_skip(
    ConfigPreview& preview) {
    if (preview.config_root.empty()) {
        return Result<StartupLogoSkipResult>::failure(
            {ErrorCode::invalid_argument,
             L"A verified KF2 configuration root is required", 0});
    }

    const auto existing = std::find_if(
        preview.files.begin(), preview.files.end(), [](const PreviewFile& file) {
            return file.relative_path == kEngineIni;
        });
    std::string original_bytes;
    std::string proposed_bytes;
    if (existing == preview.files.end()) {
        auto read = read_engine_ini(preview.config_root / kEngineIni);
        if (!read.has_value()) {
            return Result<StartupLogoSkipResult>::failure(read.error());
        }
        original_bytes = read.value();
        proposed_bytes = original_bytes;
    } else {
        original_bytes = existing->original_bytes;
        proposed_bytes = existing->proposed_bytes;
    }

    auto parsed = IniDocument::parse(proposed_bytes);
    if (!parsed.has_value()) {
        return Result<StartupLogoSkipResult>::failure(parsed.error());
    }
    auto document = std::move(parsed.value());
    std::size_t removed = 0;
    for (const auto logo : kStartupLogos) {
        const auto result = document.remove_exact(
            kMovieSection, kStartupMovieKey, logo);
        if (result.shadowed_occurrences != 0) {
            return Result<StartupLogoSkipResult>::failure(
                {ErrorCode::stale_data,
                 L"Duplicate startup-movie entries block safe logo removal",
                 0});
        }
        removed += result.changed ? 1U : 0U;
    }
    if (removed == 0) {
        return Result<StartupLogoSkipResult>::success({0, false});
    }

    const auto staged_bytes = document.serialize();
    if (existing == preview.files.end()) {
        preview.files.push_back(
            {std::filesystem::path{kEngineIni}, std::move(original_bytes),
             staged_bytes});
    } else {
        existing->proposed_bytes = staged_bytes;
    }
    return Result<StartupLogoSkipResult>::success({removed, true});
}

}  // namespace kf2::config
