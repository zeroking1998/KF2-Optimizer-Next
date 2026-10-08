#include <array>
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
    for (const auto* json : {"[]junk", "[][]", "[] {}", "[{}]junk"}) {
        CHECK(!kf2::update::parse_github_releases(
            json, repository, "0.0.2-alpha").has_value());
    }
    for (const auto* json : {"[]", "[] \r\n\t"}) {
        const auto empty = kf2::update::parse_github_releases(
            json, repository, "0.0.2-alpha");
        CHECK(empty.has_value() && !empty.value().has_value());
    }
    CHECK(!kf2::update::parse_github_releases(
        "[]", "https://evil.example/repo", "0.0.2-alpha").has_value());

    const auto release_with_unknown_number = [](
        std::string_view value, bool asset_scope) {
        const std::string unknown = "\"unknown\":" + std::string{value} + ',';
        return std::string{"[{\"tag_name\":\"v1.0.0\",\"draft\":false,"} +
            (asset_scope ? "" : unknown) +
            "\"published_at\":\"2026-08-22T12:00:00Z\",\"body\":\"ok\"," +
            "\"assets\":[{" + (asset_scope ? unknown : "") +
            "\"name\":\"KF2OptimizerNext-v1.0.0-win64.zip\"," +
            "\"size\":12,\"digest\":\"sha256:" + std::string(64, 'a') +
            "\",\"browser_download_url\":" +
            "\"https://github.com/example/KF2-Optimizer-Next/releases/download/" +
            "v1.0.0/KF2OptimizerNext-v1.0.0-win64.zip\"}]}]";
    };
    constexpr std::array malformed_numbers{
        "-", "1+2", "1..2", "01", "-01", "1e", "1e+", ".1"};
    for (const auto* value : malformed_numbers) {
        CHECK(!kf2::update::parse_github_releases(
            release_with_unknown_number(value, false), repository,
            "0.0.2-alpha").has_value());
        CHECK(!kf2::update::parse_github_releases(
            release_with_unknown_number(value, true), repository,
            "0.0.2-alpha").has_value());
    }
    constexpr std::array valid_numbers{
        "0", "-1", "12", "1.25", "1e2", "-1.25E-2"};
    for (const auto* value : valid_numbers) {
        CHECK(kf2::update::parse_github_releases(
            release_with_unknown_number(value, false), repository,
            "0.0.2-alpha").has_value());
        CHECK(kf2::update::parse_github_releases(
            release_with_unknown_number(value, true), repository,
            "0.0.2-alpha").has_value());
    }

    // Exact-version repair must not select a newer release from a list.
    const auto array_json = release_with_unknown_number("1", false);
    const auto exact_json = array_json.substr(1, array_json.size() - 2);
    const auto exact = kf2::update::parse_exact_github_release(
        exact_json, repository, "1.0.0");
    CHECK(exact.has_value() && exact.value().asset.has_value());
    CHECK(exact.value().version == "1.0.0");
    CHECK(exact.value().asset->sha256 == std::string(64, 'a'));
    CHECK(!kf2::update::parse_exact_github_release(
        exact_json, repository, "0.0.2-alpha").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        array_json, repository, "1.0.0").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        exact_json + "{}", repository, "1.0.0").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        exact_json, "https://evil.example/repo", "1.0.0").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        exact_json, repository, "../1.0.0").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        exact_json, repository, "v1.0.0").has_value());
    CHECK(!kf2::update::parse_exact_github_release(
        std::string(2U * 1024U * 1024U + 1, ' '), repository,
        "1.0.0").has_value());
    for (const auto replacement : {"null", "\"sha256:wrong\"",
                                  "\"md5:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""}) {
        auto invalid_digest = exact_json;
        const auto start = invalid_digest.find("\"sha256:");
        const auto end = invalid_digest.find('"', start + 1);
        invalid_digest.replace(start, end + 1 - start, replacement);
        CHECK(!kf2::update::parse_exact_github_release(
            invalid_digest, repository, "1.0.0").has_value());
    }
    auto absent_digest = exact_json;
    const auto digest_start = absent_digest.find("\"digest\":");
    const auto digest_end = absent_digest.find(
        ",\"browser_download_url\"", digest_start);
    absent_digest.erase(digest_start, digest_end + 1 - digest_start);
    CHECK(!kf2::update::parse_exact_github_release(
        absent_digest, repository, "1.0.0").has_value());
    auto draft = exact_json;
    draft.replace(draft.find("\"draft\":false"), 13, "\"draft\":true");
    CHECK(!kf2::update::parse_exact_github_release(
        draft, repository, "1.0.0").has_value());
    return EXIT_SUCCESS;
}
