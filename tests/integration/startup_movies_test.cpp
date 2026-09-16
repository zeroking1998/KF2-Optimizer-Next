#include "kf2/config/startup_movies.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

#define CHECK(condition) do { if (!(condition)) return EXIT_FAILURE; } while (false)

namespace {

void write_bytes(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

}  // namespace

int main() {
    const fs::path root{KF2_TEST_ROOT};
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root);

    const std::string original =
        "\xEF\xBB\xBF; keep this comment\r\n"
        "[FullScreenMovie]\r\n"
        "bForceNoMovies=FALSE\r\n"
        "StartupMovies=LogoTripwire\r\n"
        "StartupMovies=LogoHardsuit\r\n"
        "StartupMovies=LogoUE3\r\n"
        "StartupMovies=LogoGA\r\n"
        "StartupMovies=MainMenu\r\n"
        "SkippableMovies=LogoTripwire\r\n"
        "LoadMapMovies=Loading_001\r\n"
        "LoadMapMovies=Loading_002\r\n"
        "LoadMapMovies=Loading_003\r\n"
        "bShouldStopMovieAtEndOfLoadMap=true\r\n"
        "[Other]\r\nValue=1\r\n";
    const std::string expected =
        "\xEF\xBB\xBF; keep this comment\r\n"
        "[FullScreenMovie]\r\n"
        "bForceNoMovies=FALSE\r\n"
        "StartupMovies=MainMenu\r\n"
        "SkippableMovies=LogoTripwire\r\n"
        "LoadMapMovies=Loading_001\r\n"
        "LoadMapMovies=Loading_002\r\n"
        "LoadMapMovies=Loading_003\r\n"
        "bShouldStopMovieAtEndOfLoadMap=true\r\n"
        "[Other]\r\nValue=1\r\n";
    const auto engine = root / L"KFEngine.ini";
    write_bytes(engine, original);

    kf2::config::ConfigPreview preview;
    preview.config_root = root;
    const auto staged = kf2::config::stage_startup_logo_skip(preview);
    CHECK(staged.has_value());
    CHECK(staged.value().removed_logos == 4);
    CHECK(staged.value().file_staged);
    CHECK(preview.files.size() == 1);
    CHECK(preview.files.front().relative_path == L"KFEngine.ini");
    CHECK(preview.files.front().original_bytes == original);
    CHECK(preview.files.front().proposed_bytes == expected);
    CHECK(read_bytes(engine) == original);

    kf2::config::ConfigPreview combined;
    combined.config_root = root;
    combined.files.push_back({L"KFEngine.ini", original,
                              original + "; staged elsewhere\r\n"});
    const auto combined_result =
        kf2::config::stage_startup_logo_skip(combined);
    CHECK(combined_result.has_value());
    CHECK(combined_result.value().removed_logos == 4);
    CHECK(combined.files.size() == 1);
    CHECK(combined.files.front().original_bytes == original);
    CHECK(combined.files.front().proposed_bytes ==
          expected + "; staged elsewhere\r\n");

    write_bytes(engine, expected);
    kf2::config::ConfigPreview already_fast;
    already_fast.config_root = root;
    const auto unchanged =
        kf2::config::stage_startup_logo_skip(already_fast);
    CHECK(unchanged.has_value());
    CHECK(unchanged.value().removed_logos == 0);
    CHECK(!unchanged.value().file_staged);
    CHECK(already_fast.files.empty());

    write_bytes(engine,
        "[FullScreenMovie]\n"
        "StartupMovies=LogoTripwire\n"
        "StartupMovies=LogoTripwire\n"
        "StartupMovies=MainMenu\n"
        "LoadMapMovies=Loading_001\n");
    kf2::config::ConfigPreview duplicate;
    duplicate.config_root = root;
    const auto rejected =
        kf2::config::stage_startup_logo_skip(duplicate);
    CHECK(!rejected.has_value());
    CHECK(duplicate.files.empty());

    fs::remove_all(root, ignored);
    return EXIT_SUCCESS;
}
