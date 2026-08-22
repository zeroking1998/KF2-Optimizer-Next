#include "kf2/update/github_release_client.hpp"

#include "kf2/update/release_notes.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "kf2/update/semantic_version.hpp"

#ifndef KF2_RELEASE_REPOSITORY
#define KF2_RELEASE_REPOSITORY ""
#endif

namespace kf2::update {
namespace {

constexpr std::size_t kMaximumResponseBytes = 2U * 1024U * 1024U;
constexpr std::uint64_t kMaximumAssetBytes = 64ULL * 1024ULL * 1024ULL;

struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value != nullptr) WinHttpCloseHandle(value); }
};

struct ParsedAsset {
    std::string name;
    std::string url;
    std::string digest;
    std::uint64_t size{};
};

struct ParsedRelease {
    std::string tag;
    std::string body;
    std::string published_at;
    bool draft{false};
    std::vector<ParsedAsset> assets;
};

class JsonReader final {
public:
    explicit JsonReader(std::string_view text) : text_{text} {}

    bool whitespace() noexcept {
        while (offset_ < text_.size() &&
               (text_[offset_] == ' ' || text_[offset_] == '\t' ||
                text_[offset_] == '\r' || text_[offset_] == '\n')) ++offset_;
        return offset_ < text_.size();
    }

    bool consume(char expected) noexcept {
        whitespace();
        if (offset_ >= text_.size() || text_[offset_] != expected) return false;
        ++offset_;
        return true;
    }

    bool at_end() noexcept {
        whitespace();
        return offset_ == text_.size();
    }

    Result<std::string> string() {
        whitespace();
        if (offset_ >= text_.size() || text_[offset_] != '"') return invalid();
        ++offset_;
        std::string value;
        while (offset_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[offset_++]);
            if (ch == '"') return Result<std::string>::success(std::move(value));
            if (ch < 0x20) return invalid();
            if (ch != '\\') {
                value.push_back(static_cast<char>(ch));
            } else {
                if (offset_ >= text_.size()) return invalid();
                const char escaped = text_[offset_++];
                switch (escaped) {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case 'u': {
                        const auto scalar = unicode_escape();
                        if (!scalar.has_value()) return invalid();
                        append_utf8(value, scalar.value());
                        break;
                    }
                    default: return invalid();
                }
            }
            if (value.size() > 128U * 1024U) return invalid();
        }
        return invalid();
    }

    Result<bool> boolean() {
        whitespace();
        if (text_.substr(offset_).starts_with("true")) {
            offset_ += 4;
            return Result<bool>::success(true);
        }
        if (text_.substr(offset_).starts_with("false")) {
            offset_ += 5;
            return Result<bool>::success(false);
        }
        return Result<bool>::failure(
            {ErrorCode::invalid_argument, L"GitHub JSON boolean is invalid", 0});
    }

    Result<std::uint64_t> unsigned_number() {
        whitespace();
        if (offset_ >= text_.size() || !std::isdigit(
                static_cast<unsigned char>(text_[offset_]))) {
            return Result<std::uint64_t>::failure(
                {ErrorCode::invalid_argument, L"GitHub JSON number is invalid", 0});
        }
        std::uint64_t value = 0;
        while (offset_ < text_.size() && std::isdigit(
                   static_cast<unsigned char>(text_[offset_]))) {
            const unsigned digit = static_cast<unsigned>(text_[offset_] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                return Result<std::uint64_t>::failure(
                    {ErrorCode::invalid_argument, L"GitHub JSON number is too large", 0});
            }
            value = value * 10 + digit;
            ++offset_;
        }
        return Result<std::uint64_t>::success(value);
    }

    bool null_value() noexcept {
        whitespace();
        if (!text_.substr(offset_).starts_with("null")) return false;
        offset_ += 4;
        return true;
    }

    bool skip_value(unsigned depth = 0) {
        if (depth > 32) return false;
        whitespace();
        if (offset_ >= text_.size()) return false;
        if (text_[offset_] == '"') return string().has_value();
        if (text_[offset_] == '{') {
            ++offset_;
            whitespace();
            if (consume('}')) return true;
            for (;;) {
                if (!string().has_value() || !consume(':') ||
                    !skip_value(depth + 1)) return false;
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (text_[offset_] == '[') {
            ++offset_;
            whitespace();
            if (consume(']')) return true;
            for (;;) {
                if (!skip_value(depth + 1)) return false;
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (text_.substr(offset_).starts_with("true")) { offset_ += 4; return true; }
        if (text_.substr(offset_).starts_with("false")) { offset_ += 5; return true; }
        if (text_.substr(offset_).starts_with("null")) { offset_ += 4; return true; }
        const std::size_t start = offset_;
        if (text_[offset_] == '-') ++offset_;
        while (offset_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[offset_])) ||
                text_[offset_] == '.' || text_[offset_] == 'e' ||
                text_[offset_] == 'E' || text_[offset_] == '+' ||
                text_[offset_] == '-')) ++offset_;
        return offset_ != start;
    }

private:
    Result<std::string> invalid() const {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"GitHub returned malformed JSON", 0});
    }

    Result<std::uint32_t> unicode_escape() {
        const auto first = hex_quad();
        if (!first.has_value()) return first;
        std::uint32_t scalar = first.value();
        if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            if (offset_ + 2 > text_.size() || text_[offset_] != '\\' ||
                text_[offset_ + 1] != 'u') {
                return Result<std::uint32_t>::failure(
                    {ErrorCode::invalid_argument, L"GitHub JSON Unicode is invalid", 0});
            }
            offset_ += 2;
            const auto second = hex_quad();
            if (!second.has_value() || second.value() < 0xDC00 ||
                second.value() > 0xDFFF) {
                return Result<std::uint32_t>::failure(
                    {ErrorCode::invalid_argument, L"GitHub JSON Unicode is invalid", 0});
            }
            scalar = 0x10000 + ((scalar - 0xD800) << 10U) +
                     (second.value() - 0xDC00);
        } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) {
            return Result<std::uint32_t>::failure(
                {ErrorCode::invalid_argument, L"GitHub JSON Unicode is invalid", 0});
        }
        return Result<std::uint32_t>::success(scalar);
    }

    Result<std::uint32_t> hex_quad() {
        if (offset_ + 4 > text_.size()) {
            return Result<std::uint32_t>::failure(
                {ErrorCode::invalid_argument, L"GitHub JSON Unicode is incomplete", 0});
        }
        std::uint32_t value = 0;
        for (unsigned index = 0; index < 4; ++index) {
            const unsigned char ch = static_cast<unsigned char>(text_[offset_++]);
            value <<= 4U;
            if (ch >= '0' && ch <= '9') value += ch - '0';
            else if (ch >= 'a' && ch <= 'f') value += 10 + ch - 'a';
            else if (ch >= 'A' && ch <= 'F') value += 10 + ch - 'A';
            else return Result<std::uint32_t>::failure(
                {ErrorCode::invalid_argument, L"GitHub JSON Unicode is invalid", 0});
        }
        return Result<std::uint32_t>::success(value);
    }

    static void append_utf8(std::string& value, std::uint32_t scalar) {
        if (scalar <= 0x7F) value.push_back(static_cast<char>(scalar));
        else if (scalar <= 0x7FF) {
            value.push_back(static_cast<char>(0xC0 | (scalar >> 6U)));
            value.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else if (scalar <= 0xFFFF) {
            value.push_back(static_cast<char>(0xE0 | (scalar >> 12U)));
            value.push_back(static_cast<char>(0x80 | ((scalar >> 6U) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else {
            value.push_back(static_cast<char>(0xF0 | (scalar >> 18U)));
            value.push_back(static_cast<char>(0x80 | ((scalar >> 12U) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | ((scalar >> 6U) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        }
    }

    std::string_view text_;
    std::size_t offset_{};
};

Result<ParsedAsset> parse_asset(JsonReader& reader) {
    if (!reader.consume('{')) return Result<ParsedAsset>::failure(
        {ErrorCode::invalid_argument, L"GitHub release asset is malformed", 0});
    ParsedAsset asset;
    if (reader.consume('}')) return Result<ParsedAsset>::success(std::move(asset));
    for (;;) {
        const auto key = reader.string();
        if (!key.has_value() || !reader.consume(':')) return Result<ParsedAsset>::failure(
            {ErrorCode::invalid_argument, L"GitHub release asset is malformed", 0});
        if (key.value() == "name" || key.value() == "browser_download_url" ||
            key.value() == "digest") {
            if (reader.null_value()) {
                if (key.value() != "digest") return Result<ParsedAsset>::failure(
                    {ErrorCode::invalid_argument, L"GitHub release asset is incomplete", 0});
            } else {
                const auto value = reader.string();
                if (!value.has_value()) return Result<ParsedAsset>::failure(value.error());
                if (key.value() == "name") asset.name = value.value();
                else if (key.value() == "browser_download_url") asset.url = value.value();
                else asset.digest = value.value();
            }
        } else if (key.value() == "size") {
            const auto value = reader.unsigned_number();
            if (!value.has_value()) return Result<ParsedAsset>::failure(value.error());
            asset.size = value.value();
        } else if (!reader.skip_value()) return Result<ParsedAsset>::failure(
            {ErrorCode::invalid_argument, L"GitHub release asset is malformed", 0});
        if (reader.consume('}')) break;
        if (!reader.consume(',')) return Result<ParsedAsset>::failure(
            {ErrorCode::invalid_argument, L"GitHub release asset is malformed", 0});
    }
    return Result<ParsedAsset>::success(std::move(asset));
}

Result<std::vector<ParsedAsset>> parse_assets(JsonReader& reader) {
    if (!reader.consume('[')) return Result<std::vector<ParsedAsset>>::failure(
        {ErrorCode::invalid_argument, L"GitHub release assets are malformed", 0});
    std::vector<ParsedAsset> assets;
    if (reader.consume(']')) return Result<std::vector<ParsedAsset>>::success(std::move(assets));
    for (;;) {
        if (assets.size() >= 128) return Result<std::vector<ParsedAsset>>::failure(
            {ErrorCode::access_denied, L"GitHub release has too many assets", 0});
        auto asset = parse_asset(reader);
        if (!asset.has_value()) return Result<std::vector<ParsedAsset>>::failure(asset.error());
        assets.push_back(std::move(asset.value()));
        if (reader.consume(']')) break;
        if (!reader.consume(',')) return Result<std::vector<ParsedAsset>>::failure(
            {ErrorCode::invalid_argument, L"GitHub release assets are malformed", 0});
    }
    return Result<std::vector<ParsedAsset>>::success(std::move(assets));
}

Result<ParsedRelease> parse_release(JsonReader& reader) {
    if (!reader.consume('{')) return Result<ParsedRelease>::failure(
        {ErrorCode::invalid_argument, L"GitHub release is malformed", 0});
    ParsedRelease release;
    if (reader.consume('}')) return Result<ParsedRelease>::success(std::move(release));
    for (;;) {
        const auto key = reader.string();
        if (!key.has_value() || !reader.consume(':')) return Result<ParsedRelease>::failure(
            {ErrorCode::invalid_argument, L"GitHub release is malformed", 0});
        if (key.value() == "tag_name" || key.value() == "body" ||
            key.value() == "published_at") {
            if (reader.null_value()) {
                if (key.value() != "body") return Result<ParsedRelease>::failure(
                    {ErrorCode::invalid_argument, L"GitHub release is incomplete", 0});
            } else {
                const auto value = reader.string();
                if (!value.has_value()) return Result<ParsedRelease>::failure(value.error());
                if (key.value() == "tag_name") release.tag = value.value();
                else if (key.value() == "body") release.body = value.value();
                else release.published_at = value.value();
            }
        } else if (key.value() == "draft") {
            const auto value = reader.boolean();
            if (!value.has_value()) return Result<ParsedRelease>::failure(value.error());
            release.draft = value.value();
        } else if (key.value() == "assets") {
            auto assets = parse_assets(reader);
            if (!assets.has_value()) return Result<ParsedRelease>::failure(assets.error());
            release.assets = std::move(assets.value());
        } else if (!reader.skip_value()) return Result<ParsedRelease>::failure(
            {ErrorCode::invalid_argument, L"GitHub release is malformed", 0});
        if (reader.consume('}')) break;
        if (!reader.consume(',')) return Result<ParsedRelease>::failure(
            {ErrorCode::invalid_argument, L"GitHub release is malformed", 0});
    }
    return Result<ParsedRelease>::success(std::move(release));
}

Result<std::vector<ParsedRelease>> parse_release_array(std::string_view json) {
    if (json.empty() || json.size() > kMaximumResponseBytes) return Result<std::vector<ParsedRelease>>::failure(
        {ErrorCode::access_denied, L"GitHub release response size is invalid", 0});
    JsonReader reader{json};
    if (!reader.consume('[')) return Result<std::vector<ParsedRelease>>::failure(
        {ErrorCode::invalid_argument, L"GitHub release response is malformed", 0});
    std::vector<ParsedRelease> releases;
    if (reader.consume(']')) return Result<std::vector<ParsedRelease>>::success(std::move(releases));
    for (;;) {
        if (releases.size() >= 100) return Result<std::vector<ParsedRelease>>::failure(
            {ErrorCode::access_denied, L"GitHub returned too many releases", 0});
        auto release = parse_release(reader);
        if (!release.has_value()) return Result<std::vector<ParsedRelease>>::failure(release.error());
        releases.push_back(std::move(release.value()));
        if (reader.consume(']')) break;
        if (!reader.consume(',')) return Result<std::vector<ParsedRelease>>::failure(
            {ErrorCode::invalid_argument, L"GitHub release response is malformed", 0});
    }
    if (!reader.at_end()) return Result<std::vector<ParsedRelease>>::failure(
        {ErrorCode::invalid_argument, L"GitHub release response has trailing data", 0});
    return Result<std::vector<ParsedRelease>>::success(std::move(releases));
}

bool safe_repository(std::string_view value) noexcept {
    constexpr std::string_view prefix{"https://github.com/"};
    if (!value.starts_with(prefix) || value.size() > 240 ||
        value.find_first_of("?#\\") != std::string_view::npos ||
        value.find("..") != std::string_view::npos) return false;
    const auto repository = value.substr(prefix.size());
    const auto slash = repository.find('/');
    return slash != std::string_view::npos && slash != 0 &&
        slash + 1 < repository.size() &&
        repository.find('/', slash + 1) == std::string_view::npos;
}

bool safe_token(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 128 &&
        std::ranges::all_of(value, [](unsigned char ch) {
            return std::isalnum(ch) != 0 || ch == '.' || ch == '-';
        });
}

bool valid_sha256(std::string_view value) noexcept {
    return value.size() == 64 && std::ranges::all_of(value, [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

Result<std::string> fetch_releases_json(std::string_view repository) {
    if (!safe_repository(repository)) return Result<std::string>::failure(
        {ErrorCode::invalid_argument, L"Official update repository is invalid", 0});
    constexpr std::string_view prefix{"https://github.com/"};
    const auto slug = repository.substr(prefix.size());
    const std::wstring object = L"/repos/" +
        std::wstring{slug.begin(), slug.end()} + L"/releases?per_page=100";
    InternetHandle session{WinHttpOpen(
        L"KF2OptimizerNext-UpdateCheck/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.value == nullptr) return Result<std::string>::failure(
        {ErrorCode::platform_failure, L"Update check could not initialize HTTPS", GetLastError()});
    WinHttpSetTimeouts(session.value, 10'000, 10'000, 15'000, 30'000);
    InternetHandle connection{WinHttpConnect(session.value, L"api.github.com",
                                             INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (connection.value == nullptr) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"GitHub could not be reached", GetLastError()});
    InternetHandle request{WinHttpOpenRequest(
        connection.value, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH)};
    if (request.value == nullptr) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"GitHub update request could not be created", GetLastError()});
    const wchar_t* headers =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
        WinHttpReceiveResponse(request.value, nullptr) == FALSE) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"GitHub update check failed", GetLastError()});
    }
    DWORD status = 0;
    DWORD bytes = sizeof(status);
    if (WinHttpQueryHeaders(request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes,
            WINHTTP_NO_HEADER_INDEX) == FALSE || status != 200) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"GitHub did not return published releases", status});
    }
    std::string body;
    std::array<char, 32 * 1024> buffer{};
    for (;;) {
        DWORD read = 0;
        if (WinHttpReadData(request.value, buffer.data(),
                            static_cast<DWORD>(buffer.size()), &read) == FALSE) {
            return Result<std::string>::failure(
                {ErrorCode::io_failure, L"GitHub update response was interrupted", GetLastError()});
        }
        if (read == 0) break;
        if (body.size() + read > kMaximumResponseBytes) return Result<std::string>::failure(
            {ErrorCode::access_denied, L"GitHub update response is too large", 0});
        body.append(buffer.data(), read);
    }
    if (body.empty()) return Result<std::string>::failure(
        {ErrorCode::io_failure, L"GitHub returned an empty update response", 0});
    return Result<std::string>::success(std::move(body));
}

}  // namespace

std::string_view official_release_repository() noexcept {
    return KF2_RELEASE_REPOSITORY;
}

Result<std::optional<ReleaseInfo>> parse_github_releases(
    std::string_view json, std::string_view repository,
    std::string_view installed_version) {
    if (!safe_repository(repository)) return Result<std::optional<ReleaseInfo>>::failure(
        {ErrorCode::invalid_argument, L"Official update repository is invalid", 0});
    const auto installed = parse_semantic_version(installed_version);
    if (!installed.has_value()) return Result<std::optional<ReleaseInfo>>::failure(
        {ErrorCode::invalid_argument, L"Installed version is invalid", 0});
    const auto parsed = parse_release_array(json);
    if (!parsed.has_value()) return Result<std::optional<ReleaseInfo>>::failure(parsed.error());

    const ParsedRelease* best = nullptr;
    std::optional<SemanticVersion> best_version;
    for (const auto& release : parsed.value()) {
        if (release.draft || release.tag.empty() || release.published_at.empty()) continue;
        const auto candidate = parse_semantic_version(release.tag);
        if (!candidate.has_value() || !safe_token(candidate.value().canonical) ||
            release.tag != "v" + candidate.value().canonical ||
            compare_semantic_versions(candidate.value(), installed.value()) <= 0) continue;
        if (!best_version || compare_semantic_versions(
                candidate.value(), *best_version) > 0) {
            best = &release;
            best_version = candidate.value();
        }
    }
    if (best == nullptr || !best_version) return Result<std::optional<ReleaseInfo>>::success(std::nullopt);

    ReleaseInfo result{
        .repository = std::string{repository},
        .tag = best->tag,
        .version = best_version->canonical,
        .published_at = best->published_at,
        .changelog = concise_release_notes(best->body)};
    const std::string expected_name = "KF2OptimizerNext-v" +
        result.version + "-win64.zip";
    const std::string expected_url = result.repository + "/releases/download/" +
        result.tag + "/" + expected_name;
    for (const auto& asset : best->assets) {
        if (asset.name != expected_name) continue;
        const std::string_view digest = asset.digest;
        if (asset.url == expected_url && asset.size > 0 &&
            asset.size <= kMaximumAssetBytes && digest.starts_with("sha256:") &&
            valid_sha256(digest.substr(7))) {
            result.asset = ReleaseAsset{asset.name, asset.url, asset.size,
                                        std::string{digest.substr(7)}};
            break;
        }
    }
    if (!result.asset) {
        result.install_block_reason =
            L"The release has no verified portable Windows-x64 package.";
    }
    return Result<std::optional<ReleaseInfo>>::success(
        std::optional<ReleaseInfo>{std::move(result)});
}

Result<std::optional<ReleaseInfo>> query_official_github_releases(
    std::string_view installed_version) {
    const auto repository = official_release_repository();
    const auto json = fetch_releases_json(repository);
    if (!json.has_value()) return Result<std::optional<ReleaseInfo>>::failure(json.error());
    return parse_github_releases(json.value(), repository, installed_version);
}

}  // namespace kf2::update
