#include "kf2/config/config_preview.hpp"

#include <optional>
#include <utility>

namespace kf2::config {

Result<ConfigPreview> build_preview(
    const game::GameInstallation& installation,
    const std::vector<RequestedChange>& requests,
    const std::map<std::filesystem::path, IniDocument>& documents) {
    std::map<SettingId, RequestedChange> selected;
    for (const auto& request : requests) {
        const auto existing = selected.find(request.id);
        if (existing == selected.end() ||
            request.source == ChangeSource::explicit_user ||
            existing->second.source != ChangeSource::explicit_user) {
            selected.insert_or_assign(request.id, request);
        }
    }

    // KF2's smoothing window is a coupled pair. Validate the final effective
    // values before constructing any proposed bytes so an explicit import cannot
    // create MinSmoothedFrameRate > MaxSmoothedFrameRate.
    const auto effective_integer = [&](SettingId id) -> Result<int> {
        const auto* definition = find_setting(id);
        if (!definition || definition->type != SettingType::integer) {
            return Result<int>::failure(
                {ErrorCode::internal_failure,
                 L"Frame smoothing catalog contract is invalid", 0});
        }
        if (const auto request = selected.find(id); request != selected.end()) {
            if (const auto* value = std::get_if<int>(&request->second.value)) {
                if (serialize_setting_value(*definition, request->second.value)) {
                    return Result<int>::success(*value);
                }
            }
            return Result<int>::failure(
                {ErrorCode::invalid_argument,
                 L"Requested frame smoothing value is invalid", 0});
        }
        const auto document = documents.find(definition->relative_path);
        if (document == documents.end()) {
            return Result<int>::failure(
                {ErrorCode::not_found, L"Required KF2 config file is missing", 0});
        }
        const auto existing = document->second.find(
            definition->section, definition->key);
        if (!existing) {
            return Result<int>::failure(
                {ErrorCode::not_found,
                 L"Verified KF2 frame smoothing key is missing", 0});
        }
        const auto parsed = parse_setting_value(*definition, *existing);
        if (!parsed || !std::holds_alternative<int>(*parsed)) {
            return Result<int>::failure(
                {ErrorCode::invalid_argument,
                 L"Existing KF2 frame smoothing value is invalid", 0});
        }
        return Result<int>::success(std::get<int>(*parsed));
    };
    if (selected.contains(SettingId::target_fps) ||
        selected.contains(SettingId::minimum_smooth_frame_rate)) {
        const auto maximum = effective_integer(SettingId::target_fps);
        if (!maximum.has_value()) return Result<ConfigPreview>::failure(maximum.error());
        const auto minimum = effective_integer(
            SettingId::minimum_smooth_frame_rate);
        if (!minimum.has_value()) return Result<ConfigPreview>::failure(minimum.error());
        if (minimum.value() > maximum.value()) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::invalid_argument,
                 L"Minimum smoothed FPS cannot exceed target FPS", 0});
        }
    }

    ConfigPreview preview;
    preview.config_root = installation.config_root;
    std::map<std::filesystem::path, IniDocument> proposed = documents;
    for (const auto& [id, request] : selected) {
        const auto* definition = find_setting(id);
        if (!definition) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::invalid_argument,
                 L"Requested KF2 setting is not verified", 0});
        }
        if (request.source == ChangeSource::adaptive &&
            !definition->adaptive_allowed) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::access_denied,
                 L"Sensitive KF2 setting is protected from Adaptive changes",
                 0});
        }
        const auto serialized = serialize_setting_value(*definition, request.value);
        if (!serialized) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::invalid_argument,
                 L"Requested KF2 setting value is invalid", 0});
        }
        auto document = proposed.find(definition->relative_path);
        if (document == proposed.end()) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::not_found, L"Required KF2 config file is missing", 0});
        }
        const auto existing = document->second.find(
            definition->section, definition->key);
        if (!existing && !definition->insert_if_missing) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::not_found, L"Verified KF2 config key is missing", 0});
        }
        const auto before = existing
            ? parse_setting_value(*definition, *existing) : std::nullopt;
        if (existing && !before) {
            return Result<ConfigPreview>::failure(
                {ErrorCode::invalid_argument,
                 L"Existing KF2 config value is outside the verified contract", 0});
        }
        const PreviewState state = before && *before == request.value
                                       ? PreviewState::unchanged
                                       : PreviewState::ready;
        if (state == PreviewState::ready) {
            const auto replacement = existing
                ? document->second.replace(
                      definition->section, definition->key, *serialized)
                : document->second.upsert(
                      definition->section, definition->key, *serialized);
            if (!replacement.changed) {
                return Result<ConfigPreview>::failure(
                    {ErrorCode::internal_failure, L"KF2 preview replacement failed", 0});
            }
        }
        preview.items.push_back({id, definition->relative_path,
                                 definition->section, definition->key,
                                 before.value_or(request.value), request.value,
                                 request.source, request.reason, state, true,
                                 existing.has_value()});
    }

    std::map<std::filesystem::path, bool> referenced;
    for (const auto& item : preview.items) referenced[item.relative_path] = true;
    for (const auto& [path, ignored] : referenced) {
        static_cast<void>(ignored);
        preview.files.push_back(
            {path, documents.at(path).serialize(), proposed.at(path).serialize()});
    }
    return Result<ConfigPreview>::success(std::move(preview));
}

}  // namespace kf2::config
