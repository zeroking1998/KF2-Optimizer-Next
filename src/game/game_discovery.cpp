#include "kf2/game/game_discovery.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace kf2::game {
namespace {

std::wstring normalized_key(const std::filesystem::path& path) {
    std::wstring value = path.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return value;
}

bool is_within(const std::filesystem::path& child,
               const std::filesystem::path& parent) {
    auto child_iterator = child.begin();
    for (auto parent_iterator = parent.begin(); parent_iterator != parent.end();
         ++parent_iterator, ++child_iterator) {
        if (child_iterator == child.end() ||
            normalized_key(*child_iterator) != normalized_key(*parent_iterator)) {
            return false;
        }
    }
    return true;
}

bool has_reparse_attribute(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

Result<FileIdentity> read_identity(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<FileIdentity>::failure(
            {ErrorCode::io_failure, L"KFGame executable identity cannot be read",
             GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const BOOL succeeded = GetFileInformationByHandle(file, &information);
    const DWORD native_error = succeeded ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!succeeded) {
        return Result<FileIdentity>::failure(
            {ErrorCode::platform_failure, L"KFGame executable identity cannot be read",
             native_error});
    }
    return Result<FileIdentity>::success(
        {information.dwVolumeSerialNumber,
         (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
             information.nFileIndexLow});
}

bool is_x64_pe(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return false;
    input.seekg(dos.e_lfanew, std::ios::beg);
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    return input && signature == IMAGE_NT_SIGNATURE &&
           header.Machine == IMAGE_FILE_MACHINE_AMD64;
}

Result<std::wstring> utf8_path(std::string_view text) {
    if (text.empty() || text.size() > 4096 ||
        text.find('\0') != std::string_view::npos) {
        return Result<std::wstring>::failure(
            {ErrorCode::invalid_argument, L"Steam library path is invalid", 0});
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (size <= 0) {
        return Result<std::wstring>::failure(
            {ErrorCode::invalid_argument,
             L"Steam library path is not valid UTF-8", GetLastError()});
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return Result<std::wstring>::success(std::move(result));
}

Result<std::string> read_bounded_regular_file(const std::filesystem::path& path,
                                              std::uintmax_t maximum) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return Result<std::string>::failure(
            {ErrorCode::not_found, L"Steam library metadata was not found", 0});
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument,
             L"Steam library metadata exceeds the safe size limit", 0});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"Steam library metadata cannot be opened", 0});
    }
    std::string bytes{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{}};
    if (bytes.size() != size) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"Steam library metadata changed while being read", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

void add_steam_root(GameDiscoveryInput& input,
                    const std::filesystem::path& steam_root) {
    if (steam_root.empty()) return;
    input.steam_registry_candidates.push_back(
        steam_root / L"steamapps/common/KillingFloor2");
    const auto metadata = read_bounded_regular_file(
        steam_root / L"steamapps/libraryfolders.vdf", 4 * 1024 * 1024);
    if (!metadata.has_value()) return;
    const auto libraries = parse_steam_library_folders(metadata.value());
    if (!libraries.has_value()) return;
    for (const auto& library : libraries.value()) {
        input.steam_library_candidates.push_back(
            library / L"steamapps/common/KillingFloor2");
    }
}

}  // namespace

Result<std::vector<std::filesystem::path>>
parse_steam_library_folders(std::string_view document) {
    if (document.empty() || document.size() > 4 * 1024 * 1024) {
        return Result<std::vector<std::filesystem::path>>::failure(
            {ErrorCode::invalid_argument,
             L"Steam library metadata is empty or too large", 0});
    }
    std::vector<std::string> tokens;
    tokens.reserve(64);
    for (std::size_t index = 0; index < document.size();) {
        if (document[index] != '"') {
            ++index;
            continue;
        }
        ++index;
        std::string token;
        bool closed = false;
        while (index < document.size()) {
            const char character = document[index++];
            if (character == '"') {
                closed = true;
                break;
            }
            if (character == '\\') {
                if (index >= document.size()) break;
                const char escaped = document[index++];
                if (escaped != '\\' && escaped != '"') {
                    return Result<std::vector<std::filesystem::path>>::failure(
                        {ErrorCode::invalid_argument,
                         L"Steam library metadata contains an invalid escape", 0});
                }
                token.push_back(escaped);
            } else {
                if (static_cast<unsigned char>(character) < 0x20) {
                    return Result<std::vector<std::filesystem::path>>::failure(
                        {ErrorCode::invalid_argument,
                         L"Steam library metadata contains a control character", 0});
                }
                token.push_back(character);
            }
            if (token.size() > 4096) {
                return Result<std::vector<std::filesystem::path>>::failure(
                    {ErrorCode::invalid_argument,
                     L"Steam library token exceeds the safe limit", 0});
            }
        }
        if (!closed) {
            return Result<std::vector<std::filesystem::path>>::failure(
                {ErrorCode::invalid_argument,
                 L"Steam library metadata contains an unterminated string", 0});
        }
        tokens.push_back(std::move(token));
        if (tokens.size() > 4096) {
            return Result<std::vector<std::filesystem::path>>::failure(
                {ErrorCode::invalid_argument,
                 L"Steam library metadata contains too many tokens", 0});
        }
    }

    std::vector<std::filesystem::path> paths;
    std::set<std::wstring> seen;
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index] != "path") continue;
        auto wide = utf8_path(tokens[index + 1]);
        if (!wide.has_value()) {
            return Result<std::vector<std::filesystem::path>>::failure(wide.error());
        }
        std::filesystem::path path{wide.value()};
        const auto key = normalized_key(path);
        if (seen.insert(key).second) paths.push_back(std::move(path));
        if (paths.size() > 64) {
            return Result<std::vector<std::filesystem::path>>::failure(
                {ErrorCode::invalid_argument,
                 L"Steam library metadata contains too many paths", 0});
        }
    }
    return Result<std::vector<std::filesystem::path>>::success(std::move(paths));
}

Result<GameInstallation> validate_game_candidate(
    const std::filesystem::path& install_root,
    const std::filesystem::path& config_root,
    const std::filesystem::path& allowed_config_parent,
    DiscoverySource source) {
    std::error_code error;
    if (install_root.empty() || config_root.empty() || allowed_config_parent.empty()) {
        return Result<GameInstallation>::failure(
            {ErrorCode::invalid_argument, L"KF2 discovery paths are incomplete", 0});
    }
    const auto canonical_install = std::filesystem::weakly_canonical(install_root, error);
    if (error || !std::filesystem::is_directory(canonical_install)) {
        return Result<GameInstallation>::failure(
            {ErrorCode::not_found, L"KF2 installation root was not found", 0});
    }
    const auto executable = std::filesystem::weakly_canonical(
        canonical_install / L"Binaries/Win64/KFGame.exe", error);
    if (error || !std::filesystem::is_regular_file(executable) ||
        has_reparse_attribute(canonical_install) || has_reparse_attribute(executable) ||
        !is_x64_pe(executable)) {
        return Result<GameInstallation>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 candidate does not contain a trusted x64 KFGame.exe", 0});
    }
    const auto canonical_parent =
        std::filesystem::weakly_canonical(allowed_config_parent, error);
    if (error || !std::filesystem::is_directory(canonical_parent)) {
        return Result<GameInstallation>::failure(
            {ErrorCode::invalid_argument, L"Allowed config parent is invalid", 0});
    }
    const auto canonical_config = std::filesystem::weakly_canonical(config_root, error);
    if (error || !std::filesystem::is_directory(canonical_config) ||
        !is_within(canonical_config, canonical_parent) ||
        has_reparse_attribute(canonical_config)) {
        return Result<GameInstallation>::failure(
            {ErrorCode::access_denied, L"KF2 config root is outside the allowed tree", 0});
    }
    auto identity = read_identity(executable);
    if (!identity.has_value()) {
        return Result<GameInstallation>::failure(identity.error());
    }
    return Result<GameInstallation>::success(
        {canonical_install, executable, canonical_config, source, identity.value(), 0});
}

Result<GameInstallation> discover_game_installation(const GameDiscoveryInput& input) {
    struct CandidateList {
        const std::vector<std::filesystem::path>* paths;
        DiscoverySource source;
    };
    const CandidateList lists[] = {
        {&input.steam_registry_candidates, DiscoverySource::steam_registry},
        {&input.steam_library_candidates, DiscoverySource::steam_library},
        {&input.manual_candidates, DiscoverySource::manual},
    };
    std::set<std::wstring> seen;
    std::size_t duplicates = 0;
    std::optional<GameInstallation> selected;
    for (const auto& list : lists) {
        for (const auto& candidate : *list.paths) {
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            const auto key = error ? normalized_key(candidate) : normalized_key(canonical);
            if (!seen.insert(key).second) {
                ++duplicates;
                continue;
            }
            if (!selected.has_value()) {
                auto validated = validate_game_candidate(
                    candidate, input.config_root, input.allowed_config_parent,
                    list.source);
                if (validated.has_value()) {
                    selected.emplace(std::move(validated.value()));
                }
            }
        }
    }
    if (selected.has_value()) {
        selected->duplicate_candidates_ignored = duplicates;
        return Result<GameInstallation>::success(std::move(*selected));
    }
    return Result<GameInstallation>::failure(
        {ErrorCode::not_found, L"No trusted KF2 installation was found", 0});
}

Result<GameDiscoveryInput> default_game_discovery_input() {
    PWSTR documents_raw = nullptr;
    const HRESULT known = SHGetKnownFolderPath(
        FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents_raw);
    if (FAILED(known) || !documents_raw) {
        return Result<GameDiscoveryInput>::failure(
            {ErrorCode::platform_failure, L"Documents directory cannot be resolved",
             static_cast<std::uint32_t>(known)});
    }
    std::filesystem::path documents{documents_raw};
    CoTaskMemFree(documents_raw);
    GameDiscoveryInput input;
    input.allowed_config_parent = documents;
    input.config_root = documents / L"My Games/KillingFloor2/KFGame/Config";
    wchar_t steam_path[32768]{};
    DWORD bytes = sizeof(steam_path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                     RRF_RT_REG_SZ, nullptr, steam_path, &bytes) == ERROR_SUCCESS) {
        add_steam_root(input, std::filesystem::path{steam_path});
    }
    wchar_t machine_steam_path[32768]{};
    bytes = sizeof(machine_steam_path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam",
                     L"InstallPath", RRF_RT_REG_SZ, nullptr,
                     machine_steam_path, &bytes) == ERROR_SUCCESS) {
        add_steam_root(input, std::filesystem::path{machine_steam_path});
    }
    input.steam_library_candidates.push_back(
        std::filesystem::path{L"C:/Program Files (x86)/Steam/steamapps/common/KillingFloor2"});
    return Result<GameDiscoveryInput>::success(std::move(input));
}

}  // namespace kf2::game
