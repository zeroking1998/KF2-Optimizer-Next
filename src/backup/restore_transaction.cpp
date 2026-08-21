#include "kf2/backup/restore_transaction.hpp"

#include <Windows.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <optional>
#include <set>
#include <sstream>

#include "kf2/config/kf2_catalog.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::backup {
namespace {

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size,
                            nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    output << "\\u00" << digits[character >> 4]
                           << digits[character & 0x0f];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool same_existing_directory(const std::filesystem::path& left,
                             const std::filesystem::path& right) {
    auto open_directory = [](const std::filesystem::path& path) {
        return CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                           nullptr);
    };
    HANDLE left_handle = open_directory(left);
    if (left_handle == INVALID_HANDLE_VALUE) return false;
    HANDLE right_handle = open_directory(right);
    if (right_handle == INVALID_HANDLE_VALUE) {
        CloseHandle(left_handle);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION left_info{}, right_info{};
    const bool same = GetFileInformationByHandle(left_handle, &left_info) &&
                      GetFileInformationByHandle(right_handle, &right_info) &&
                      (left_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                      (right_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                      (left_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                      (right_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                      left_info.dwVolumeSerialNumber == right_info.dwVolumeSerialNumber &&
                      left_info.nFileIndexHigh == right_info.nFileIndexHigh &&
                      left_info.nFileIndexLow == right_info.nFileIndexLow;
    CloseHandle(right_handle);
    CloseHandle(left_handle);
    return same;
}

Result<bool> journal_state(const BackupSet& backup, std::string_view state) {
    return platform::windows::atomic_replace_utf8(
        backup.journal_path,
        "version=1\nstate=" + std::string{state} + "\nid=" + backup.id + "\n");
}

std::string parse_state(std::string_view journal) {
    constexpr std::string_view prefix = "state=";
    const auto start = journal.find(prefix);
    if (start == std::string_view::npos) return {};
    const auto value_start = start + prefix.size();
    const auto end = journal.find_first_of("\r\n", value_start);
    return std::string{journal.substr(value_start, end - value_start)};
}

bool safe_relative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const auto& component : path) {
        if (component == L"." || component == L"..") return false;
    }
    return true;
}

Result<std::string> current_digest(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"Recovery target identity is unsafe",
             GetLastError()});
    }
    return security::sha256_hex(read_bytes(path));
}

std::optional<config::SettingId> setting_id(std::string_view name) {
    if (const auto* definition = config::find_setting_by_token(name)) {
        return definition->id;
    }
    if (name == "target_fps") return config::SettingId::target_fps;
    if (name == "smooth_frame_rate") return config::SettingId::smooth_frame_rate;
    if (name == "corpse_limit") return config::SettingId::corpse_limit;
    if (name == "gore_effect_limit") return config::SettingId::gore_effect_limit;
    if (name == "explosion_decal_limit") return config::SettingId::explosion_decal_limit;
    if (name == "impact_decal_limit") return config::SettingId::impact_decal_limit;
    if (name == "wound_decal_limit") return config::SettingId::wound_decal_limit;
    if (name == "blood_splatter_decal_limit") return config::SettingId::blood_splatter_decal_limit;
    if (name == "blood_pool_decal_limit") return config::SettingId::blood_pool_decal_limit;
    if (name == "blood_effect_limit") return config::SettingId::blood_effect_limit;
    if (name == "body_wound_decal_lifetime") return config::SettingId::body_wound_decal_lifetime;
    if (name == "blood_splatter_lifetime") return config::SettingId::blood_splatter_lifetime;
    if (name == "blood_pool_lifetime") return config::SettingId::blood_pool_lifetime;
    if (name == "giblet_lifetime") return config::SettingId::giblet_lifetime;
    if (name == "gore_lifetime_multiplier") return config::SettingId::gore_lifetime_multiplier;
    if (name == "persistent_splats_per_frame") return config::SettingId::persistent_splats_per_frame;
    if (name == "blood_splatter_decals") return config::SettingId::blood_splatter_decals;
    if (name == "secondary_blood_effects") return config::SettingId::secondary_blood_effects;
    if (name == "static_decals") return config::SettingId::static_decals;
    if (name == "dynamic_decals") return config::SettingId::dynamic_decals;
    if (name == "decal_cull_distance_scale") return config::SettingId::decal_cull_distance_scale;
    if (name == "dynamic_shadows") return config::SettingId::dynamic_shadows;
    if (name == "light_environment_shadows") return config::SettingId::light_environment_shadows;
    if (name == "ambient_occlusion") return config::SettingId::ambient_occlusion;
    if (name == "bloom") return config::SettingId::bloom;
    if (name == "distortion") return config::SettingId::distortion;
    if (name == "drop_particle_distortion") return config::SettingId::drop_particle_distortion;
    if (name == "high_quality_materials") return config::SettingId::high_quality_materials;
    if (name == "detail_mode") return config::SettingId::detail_mode;
    if (name == "max_shadow_resolution") return config::SettingId::max_shadow_resolution;
    if (name == "max_whole_scene_shadow_resolution") return config::SettingId::max_whole_scene_shadow_resolution;
    if (name == "shadow_texels_per_pixel") return config::SettingId::shadow_texels_per_pixel;
    if (name == "fracture_cull_distance_scale") return config::SettingId::fracture_cull_distance_scale;
    return std::nullopt;
}

std::string setting_name(config::SettingId id) {
    return config::setting_token(id);
}

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    bool take(char expected) {
        whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool next_is(char expected) {
        whitespace();
        return position_ < input_.size() && input_[position_] == expected;
    }

    bool finished() {
        whitespace();
        return position_ == input_.size();
    }

    std::optional<std::string> string() {
        if (!take('"')) return std::nullopt;
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return output;
            if (character < 0x20) return std::nullopt;
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) return std::nullopt;
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    const auto first = hex_quad();
                    if (!first) return std::nullopt;
                    std::uint32_t point = *first;
                    if (point >= 0xD800 && point <= 0xDBFF) {
                        if (position_ + 2 > input_.size() ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') {
                            return std::nullopt;
                        }
                        position_ += 2;
                        const auto second = hex_quad();
                        if (!second || *second < 0xDC00 || *second > 0xDFFF) {
                            return std::nullopt;
                        }
                        point = 0x10000 + ((point - 0xD800) << 10) +
                                (*second - 0xDC00);
                    } else if (point >= 0xDC00 && point <= 0xDFFF) {
                        return std::nullopt;
                    }
                    append_utf8(point, output);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> scalar() {
        whitespace();
        const std::size_t start = position_;
        if (input_.substr(position_).starts_with("true")) {
            position_ += 4;
            return std::string{"true"};
        }
        if (input_.substr(position_).starts_with("false")) {
            position_ += 5;
            return std::string{"false"};
        }
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return std::nullopt;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') return std::nullopt;
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        } else {
            return std::nullopt;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == digits) return std::nullopt;
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == digits) return std::nullopt;
        }
        return std::string{input_.substr(start, position_ - start)};
    }

private:
    void whitespace() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) {
            ++position_;
        }
    }

    std::optional<std::uint32_t> hex_quad() {
        if (position_ + 4 > input_.size()) return std::nullopt;
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = input_[position_++];
            value <<= 4;
            if (character >= '0' && character <= '9') value += character - '0';
            else if (character >= 'a' && character <= 'f') value += character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') value += character - 'A' + 10;
            else return std::nullopt;
        }
        return value;
    }

    static void append_utf8(std::uint32_t point, std::string& output) {
        if (point <= 0x7F) output.push_back(static_cast<char>(point));
        else if (point <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (point >> 6)));
            output.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else if (point <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (point >> 12)));
            output.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (point >> 18)));
            output.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
    }

    std::string_view input_;
    std::size_t position_{0};
};

std::optional<std::wstring> utf8_wide(std::string_view input) {
    if (input.empty()) return std::wstring{};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
        nullptr, 0);
    if (count <= 0) return std::nullopt;
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), count) !=
        count) {
        return std::nullopt;
    }
    return output;
}

}  // namespace

Result<RestoreResult> restore_backup(
    BackupStore& store, std::string_view id,
    const std::filesystem::path& expected_config_root,
    const config::ApplyPreconditions& preconditions) {
    if (preconditions.game_running) {
        return Result<RestoreResult>::failure(
            {ErrorCode::access_denied, L"KF2 must be stopped before restore", 0});
    }
    auto selected = store.load_backup(id);
    if (!selected.has_value()) return Result<RestoreResult>::failure(selected.error());
    if (!same_existing_directory(selected.value().config_root,
                                 expected_config_root)) {
        return Result<RestoreResult>::failure(
            {ErrorCode::access_denied,
             L"Backup does not belong to the verified KF2 configuration directory", 0});
    }
    auto verified = store.verify(selected.value());
    if (!verified.has_value()) return Result<RestoreResult>::failure(verified.error());

    config::ConfigPreview restore;
    restore.config_root = selected.value().config_root;
    for (const auto& snapshot : selected.value().snapshots) {
        if (!safe_relative(snapshot.relative_path)) {
            return Result<RestoreResult>::failure(
                {ErrorCode::access_denied, L"Restore target is outside allowlist", 0});
        }
        const auto current = read_bytes(restore.config_root / snapshot.relative_path);
        const auto original = read_bytes(snapshot.object_path);
        restore.files.push_back({snapshot.relative_path, current, original});
    }
    auto applied = config::apply_preview(restore, store, preconditions);
    if (!applied.has_value()) return Result<RestoreResult>::failure(applied.error());
    return Result<RestoreResult>::success(
        {std::move(applied.value().backup), applied.value().files_changed});
}

Result<RecoveryResult> recover_transactions(
    BackupStore& store,
    const std::optional<std::filesystem::path>& expected_config_root) {
    RecoveryResult result;
    const auto journals = store.state_root() / L"backups/journals";
    std::error_code error;
    if (!std::filesystem::exists(journals, error)) {
        if (!error) return Result<RecoveryResult>::success(result);
        return Result<RecoveryResult>::failure(
            {ErrorCode::io_failure, L"Recovery journals cannot be inspected",
             static_cast<std::uint32_t>(error.value())});
    }
    for (const auto& entry : std::filesystem::directory_iterator(journals, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != L".journal") continue;
        const auto state = parse_state(read_bytes(entry.path()));
        if (state == "complete") continue;
        if (!expected_config_root) {
            return Result<RecoveryResult>::failure(
                {ErrorCode::access_denied,
                 L"Interrupted configuration recovery requires a verified KF2 configuration directory",
                 0});
        }
        auto backup = store.load_backup(entry.path().stem().string());
        if (!backup.has_value()) return Result<RecoveryResult>::failure(backup.error());
        if (!same_existing_directory(backup.value().config_root,
                                     *expected_config_root)) {
            return Result<RecoveryResult>::failure(
                {ErrorCode::access_denied,
                 L"Recovery journal does not belong to the verified KF2 configuration directory",
                 0});
        }
        auto verified = store.verify(backup.value());
        if (!verified.has_value()) return Result<RecoveryResult>::failure(verified.error());
        if (state == "backup_complete") {
            auto finalized = journal_state(backup.value(), "complete");
            if (!finalized.has_value()) return Result<RecoveryResult>::failure(finalized.error());
            continue;
        }
        if (state != "replacement_started" && state != "verification_complete") {
            return Result<RecoveryResult>::failure(
                {ErrorCode::io_failure, L"Recovery journal state is invalid", 0});
        }
        for (const auto& snapshot : backup.value().snapshots) {
            if (!safe_relative(snapshot.relative_path)) return Result<RecoveryResult>::failure(
                {ErrorCode::access_denied, L"Recovery target is outside allowlist", 0});
            const auto target = backup.value().config_root / snapshot.relative_path;
            auto digest = current_digest(target);
            if (!digest.has_value()) return Result<RecoveryResult>::failure(digest.error());
            if (state == "verification_complete") {
                if (digest.value() != snapshot.desired_sha256) {
                    return Result<RecoveryResult>::failure(
                        {ErrorCode::stale_data, L"Verified target changed before recovery", 0});
                }
            } else {
                if (digest.value() != snapshot.sha256 &&
                    digest.value() != snapshot.desired_sha256) {
                    return Result<RecoveryResult>::failure(
                        {ErrorCode::stale_data, L"Recovery target has conflicting changes", 0});
                }
                if (digest.value() == snapshot.desired_sha256) {
                    auto restored = platform::windows::atomic_replace_utf8(
                        target, read_bytes(snapshot.object_path));
                    if (!restored.has_value()) {
                        return Result<RecoveryResult>::failure(restored.error());
                    }
                }
            }
        }
        auto finalized = journal_state(backup.value(), "complete");
        if (!finalized.has_value()) return Result<RecoveryResult>::failure(finalized.error());
        ++result.transactions_recovered;
        result.outcome = state == "verification_complete"
            ? RecoveryOutcome::rolled_forward : RecoveryOutcome::rolled_back;
    }
    if (error) return Result<RecoveryResult>::failure(
        {ErrorCode::io_failure, L"Recovery journal enumeration failed",
         static_cast<std::uint32_t>(error.value())});
    return Result<RecoveryResult>::success(result);
}

Result<std::string> export_preview_json(const config::ConfigPreview& preview) {
    std::ostringstream output;
    output << "{\"version\":1,\"changes\":[";
    bool first = true;
    for (const auto& item : preview.items) {
        const auto name = setting_name(item.id);
        if (name.empty()) continue;
        if (!first) output << ',';
        first = false;
        output << "{\"id\":\"" << name << "\",\"value\":";
        if (std::holds_alternative<int>(item.after)) {
            output << std::get<int>(item.after);
        } else if (std::holds_alternative<bool>(item.after)) {
            output << (std::get<bool>(item.after) ? "true" : "false");
        } else {
            output.imbue(std::locale::classic());
            output << std::setprecision(12) << std::get<double>(item.after);
        }
        output << ",\"source\":\""
               << (item.source == config::ChangeSource::explicit_user
                       ? "explicit" : "adaptive")
               << "\",\"reason\":\"" << json_escape(utf8(item.reason)) << "\"}";
    }
    output << "]}";
    return Result<std::string>::success(output.str());
}

Result<std::vector<config::RequestedChange>>
import_requested_changes_json(std::string_view json) {
    using Imported = std::vector<config::RequestedChange>;
    const auto malformed = [](std::wstring message) {
        return Result<Imported>::failure(
            {ErrorCode::invalid_argument, std::move(message), 0});
    };
    if (json.empty() || json.size() > 1024 * 1024) {
        return malformed(L"Change document size is invalid");
    }

    JsonReader reader{json};
    const auto version_key = reader.take('{') ? reader.string() : std::nullopt;
    if (!version_key || *version_key != "version" || !reader.take(':')) {
        return malformed(L"Change document is malformed");
    }
    const auto version = reader.scalar();
    if (!version || *version != "1" || !reader.take(',')) {
        return malformed(L"Change document version is invalid");
    }
    const auto changes_key = reader.string();
    if (!changes_key || *changes_key != "changes" || !reader.take(':') ||
        !reader.take('[')) {
        return malformed(L"Change document is malformed");
    }

    std::vector<config::RequestedChange> requests;
    std::set<config::SettingId> seen_ids;
    while (!reader.next_is(']')) {
        if (!reader.take('{')) return malformed(L"Change entry is malformed");
        std::optional<std::string> raw_id;
        std::optional<std::string> raw_value;
        std::optional<std::string> raw_source;
        std::optional<std::string> raw_reason;
        std::set<std::string> fields;
        while (!reader.next_is('}')) {
            const auto field = reader.string();
            if (!field || !fields.insert(*field).second || !reader.take(':')) {
                return malformed(L"Change entry contains an invalid field");
            }
            if (*field == "id") raw_id = reader.string();
            else if (*field == "value") raw_value = reader.scalar();
            else if (*field == "source") raw_source = reader.string();
            else if (*field == "reason") raw_reason = reader.string();
            else return malformed(L"Change entry contains an unknown field");
            if ((*field == "id" && !raw_id) ||
                (*field == "value" && !raw_value) ||
                (*field == "source" && !raw_source) ||
                (*field == "reason" && !raw_reason)) {
                return malformed(L"Change entry value is malformed");
            }
            if (!reader.next_is('}') && !reader.take(',')) {
                return malformed(L"Change entry is malformed");
            }
        }
        if (!reader.take('}') || !raw_id || !raw_value) {
            return malformed(L"Change entry is incomplete");
        }
        const auto id = setting_id(*raw_id);
        if (!id) return malformed(L"Unknown configuration setting");
        if (!seen_ids.insert(*id).second) {
            return malformed(L"Configuration setting is duplicated");
        }
        const auto* definition = config::find_setting(*id);
        const std::wstring wide_value{raw_value->begin(), raw_value->end()};
        const auto value = definition
            ? config::parse_setting_value(*definition, wide_value) : std::nullopt;
        if (!definition || !value) {
            return malformed(L"Configuration value is invalid or out of range");
        }
        config::ChangeSource source = config::ChangeSource::explicit_user;
        if (raw_source) {
            if (*raw_source == "adaptive" || *raw_source == "smart") {
                source = config::ChangeSource::adaptive;
            } else if (*raw_source != "explicit" && *raw_source != "manual") {
                return malformed(L"Configuration source is invalid");
            }
        }
        std::wstring reason;
        if (raw_reason) {
            if (raw_reason->size() > 4096) {
                return malformed(L"Configuration reason is too long");
            }
            const auto decoded = utf8_wide(*raw_reason);
            if (!decoded) return malformed(L"Configuration reason is not valid UTF-8");
            reason = *decoded;
        }
        requests.push_back({*id, *value, source, std::move(reason)});
        if (requests.size() > config::all_settings().size()) {
            return malformed(L"Change document contains too many settings");
        }
        if (!reader.next_is(']') && !reader.take(',')) {
            return malformed(L"Change list is malformed");
        }
    }
    if (!reader.take(']') || !reader.take('}') || !reader.finished()) {
        return malformed(L"Change document has trailing or malformed data");
    }
    return Result<Imported>::success(std::move(requests));
}

}  // namespace kf2::backup
