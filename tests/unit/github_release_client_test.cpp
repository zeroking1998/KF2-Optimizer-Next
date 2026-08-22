#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/update/github_release_client.hpp"

#define CHECK(expression) do { if (!(expression)) {                            \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #expression << '\n'; return EXIT_FAILURE; } } while (false)

int main() {
    constexpr std::string_view repository =
        "https://github.com/example/KF2-Optimizer-Next";
    const std::string releases = R"json([
      {
        "tag_name":"v0.0.2-alpha",
        "draft":false,
        "prerelease":true,
        "published_at":"2026-08-20T10:00:00Z",
        "body":"current",
        "assets":[]
      },
      {
        "tag_name":"v0.0.4-alpha",
        "draft":true,
        "published_at":"2026-08-23T10:00:00Z",
        "body":"draft must be ignored",
        "assets":[]
      },
      {
        "tag_name":"v0.0.3-beta.2",
        "draft":false,
        "prerelease":true,
        "published_at":"2026-08-22T12:00:00Z",
        "body":"## What's new\n- Updater\n## Bug fixes\n- Rollback",
        "assets":[{
          "name":"KF2OptimizerNext-v0.0.3-beta.2-win64.zip",
          "size":1048576,
          "digest":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "browser_download_url":"https://github.com/example/KF2-Optimizer-Next/releases/download/v0.0.3-beta.2/KF2OptimizerNext-v0.0.3-beta.2-win64.zip"
        }]
      }
    ])json";
    const auto newer = kf2::update::parse_github_releases(
        releases, repository, "0.0.2-alpha");
    CHECK(newer.has_value());
    CHECK(newer.value().has_value());
    CHECK(newer.value()->version == "0.0.3-beta.2");
    CHECK(newer.value()->published_at == "2026-08-22T12:00:00Z");
    CHECK(newer.value()->asset.has_value());
    CHECK(newer.value()->asset->size_bytes == 1'048'576);
    CHECK(newer.value()->asset->sha256 == std::string(64, 'a'));
    CHECK(newer.value()->changelog.find("Rollback") != std::string::npos);

    const auto current = kf2::update::parse_github_releases(
        releases, repository, "0.0.3-beta.2");
    CHECK(current.has_value());
    CHECK(!current.value().has_value());

    const std::string unverified = R"json([{
      "tag_name":"v1.0.0","draft":false,
      "published_at":"2026-08-22T12:00:00Z","body":"stable",
      "assets":[{
        "name":"KF2OptimizerNext-v1.0.0-win64.zip","size":12,
        "digest":"sha256:wrong",
        "browser_download_url":"https://github.com/example/KF2-Optimizer-Next/releases/download/v1.0.0/KF2OptimizerNext-v1.0.0-win64.zip"
      }]
    }])json";
    const auto blocked = kf2::update::parse_github_releases(
        unverified, repository, "0.0.3-beta.2");
    CHECK(blocked.has_value() && blocked.value().has_value());
    CHECK(!blocked.value()->asset.has_value());
    CHECK(!blocked.value()->install_block_reason.empty());

    CHECK(!kf2::update::parse_github_releases(
        "not-json", repository, "0.0.2-alpha").has_value());
    CHECK(!kf2::update::parse_github_releases(
        "[]", "https://evil.example/repo", "0.0.2-alpha").has_value());
    return EXIT_SUCCESS;
}
