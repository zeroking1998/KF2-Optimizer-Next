#include "kf2/config/ini_document.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>

namespace kf2::config {
namespace {

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          text.data(), static_cast<int>(text.size()),
                                          nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count,
                        nullptr, nullptr);
    return result;
}

std::wstring normalized(std::wstring_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) --end;
    std::wstring result{value.substr(begin, end - begin)};
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return result;
}

std::string_view trim_ascii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
    std::size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
    return value.substr(begin, end - begin);
}

}  // namespace

Result<IniDocument> IniDocument::parse(std::string_view bytes) {
    if (bytes.find('\0') != std::string_view::npos) {
        return Result<IniDocument>::failure(
            {ErrorCode::invalid_argument, L"INI contains an embedded NUL", 0});
    }
    IniDocument document;
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        document.encoding_ = TextEncoding::utf8_bom;
        bytes.remove_prefix(3);
    }
    if (!bytes.empty() && utf8_to_wide(bytes).empty()) {
        return Result<IniDocument>::failure(
            {ErrorCode::invalid_argument, L"INI is not valid UTF-8", 0});
    }
    document.line_ending_ = bytes.find("\r\n") != std::string_view::npos
                                ? LineEnding::crlf
                                : LineEnding::lf;
    std::wstring active_section;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t newline = bytes.find('\n', offset);
        const std::size_t physical_end = newline == std::string_view::npos ? bytes.size() : newline;
        std::size_t content_end = physical_end;
        std::string ending;
        if (newline != std::string_view::npos) {
            if (content_end > offset && bytes[content_end - 1] == '\r') {
                --content_end;
                ending = "\r\n";
            } else {
                ending = "\n";
            }
        }
        Line line;
        line.bytes = std::string{bytes.substr(offset, content_end - offset)};
        line.ending = std::move(ending);
        const auto trimmed = trim_ascii(line.bytes);
        if (!trimmed.empty() && trimmed.front() == '[') {
            const auto close = trimmed.find(']');
            if (close == std::string_view::npos ||
                !trim_ascii(trimmed.substr(close + 1)).empty()) {
                return Result<IniDocument>::failure(
                    {ErrorCode::invalid_argument, L"INI section header is malformed", 0});
            }
            const auto section_text = trimmed.substr(1, close - 1);
            const auto wide = utf8_to_wide(section_text);
            if (wide.empty() && !section_text.empty()) {
                return Result<IniDocument>::failure(
                    {ErrorCode::invalid_argument, L"INI section is invalid UTF-8", 0});
            }
            active_section = normalized(wide);
            line.section = active_section;
            line.section_header = true;
        } else if (!trimmed.empty() && trimmed.front() != ';' && trimmed.front() != '#') {
            const auto equals = line.bytes.find('=');
            if (equals != std::string::npos) {
                const auto key_text = trim_ascii(std::string_view{line.bytes}.substr(0, equals));
                const auto wide_key = utf8_to_wide(key_text);
                if (!wide_key.empty()) {
                    line.section = active_section;
                    line.key = normalized(wide_key);
                    line.value_start = equals + 1;
                    while (line.value_start < line.bytes.size() &&
                           (line.bytes[line.value_start] == ' ' || line.bytes[line.value_start] == '\t')) {
                        ++line.value_start;
                    }
                    line.value_end = line.bytes.size();
                    const auto comment = line.bytes.find_first_of(";#", line.value_start);
                    if (comment != std::string::npos) line.value_end = comment;
                    while (line.value_end > line.value_start &&
                           (line.bytes[line.value_end - 1] == ' ' || line.bytes[line.value_end - 1] == '\t')) {
                        --line.value_end;
                    }
                    line.assignment = true;
                }
            }
        }
        document.lines_.push_back(std::move(line));
        offset = newline == std::string_view::npos ? bytes.size() : newline + 1;
    }
    return Result<IniDocument>::success(std::move(document));
}

std::optional<std::wstring> IniDocument::find(std::wstring_view section,
                                               std::wstring_view key) const {
    const auto wanted_section = normalized(section);
    const auto wanted_key = normalized(key);
    const Line* match = nullptr;
    for (const auto& line : lines_) {
        if (line.assignment && line.section == wanted_section && line.key == wanted_key) {
            match = &line;
        }
    }
    if (!match) return std::nullopt;
    return utf8_to_wide(std::string_view{match->bytes}.substr(
        match->value_start, match->value_end - match->value_start));
}

ReplaceResult IniDocument::replace(std::wstring_view section,
                                   std::wstring_view key,
                                   std::wstring_view value) {
    const auto wanted_section = normalized(section);
    const auto wanted_key = normalized(key);
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        const auto& line = lines_[index];
        if (line.assignment && line.section == wanted_section && line.key == wanted_key) {
            matches.push_back(index);
        }
    }
    const std::string encoded_value = wide_to_utf8(value);
    if (!matches.empty()) {
        auto& line = lines_[matches.back()];
        const std::string current = line.bytes.substr(
            line.value_start, line.value_end - line.value_start);
        if (current == encoded_value) return {false, matches.size() - 1};
        line.bytes.replace(line.value_start, line.value_end - line.value_start,
                           encoded_value);
        line.value_end = line.value_start + encoded_value.size();
        return {true, matches.size() - 1};
    }

    std::optional<std::size_t> section_index;
    std::size_t insert_at = lines_.size();
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].section_header && lines_[index].section == wanted_section) {
            section_index = index;
            insert_at = index + 1;
            while (insert_at < lines_.size() && !lines_[insert_at].section_header) ++insert_at;
        }
    }
    if (!section_index) return {false, 0};
    const std::string ending = line_ending_ == LineEnding::crlf ? "\r\n" : "\n";
    if (insert_at > 0 && lines_[insert_at - 1].ending.empty()) lines_[insert_at - 1].ending = ending;
    Line added;
    added.bytes = wide_to_utf8(key) + "=" + encoded_value;
    added.ending = ending;
    added.section = wanted_section;
    added.key = wanted_key;
    added.value_start = wide_to_utf8(key).size() + 1;
    added.value_end = added.bytes.size();
    added.assignment = true;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insert_at), std::move(added));
    return {true, 0};
}

ReplaceResult IniDocument::upsert(std::wstring_view section,
                                  std::wstring_view key,
                                  std::wstring_view value) {
    const auto existing = find(section, key);
    const auto replaced = replace(section, key, value);
    if (existing || replaced.changed || replaced.shadowed_occurrences != 0) {
        return replaced;
    }

    const auto wanted_section = normalized(section);
    const auto wanted_key = normalized(key);
    const auto encoded_section = wide_to_utf8(section);
    const auto encoded_key = wide_to_utf8(key);
    const auto encoded_value = wide_to_utf8(value);
    if (wanted_section.empty() || wanted_key.empty() || encoded_section.empty() ||
        encoded_key.empty()) {
        return {false, 0};
    }

    const std::string ending = line_ending_ == LineEnding::crlf ? "\r\n" : "\n";
    if (!lines_.empty() && lines_.back().ending.empty()) {
        lines_.back().ending = ending;
    }

    Line header;
    header.bytes = "[" + encoded_section + "]";
    header.ending = ending;
    header.section = wanted_section;
    header.section_header = true;
    lines_.push_back(std::move(header));

    Line assignment;
    assignment.bytes = encoded_key + "=" + encoded_value;
    assignment.ending = ending;
    assignment.section = wanted_section;
    assignment.key = wanted_key;
    assignment.value_start = encoded_key.size() + 1;
    assignment.value_end = assignment.bytes.size();
    assignment.assignment = true;
    lines_.push_back(std::move(assignment));
    return {true, 0};
}

ReplaceResult IniDocument::append_unique(std::wstring_view section,
                                         std::wstring_view key,
                                         std::wstring_view value) {
    const auto wanted_section = normalized(section);
    const auto wanted_key = normalized(key);
    const auto encoded_key = wide_to_utf8(key);
    const auto encoded_value = wide_to_utf8(value);
    if (wanted_section.empty() || wanted_key.empty() || encoded_key.empty() ||
        (encoded_value.empty() && !value.empty())) {
        return {false, 0};
    }

    std::vector<std::size_t> sections;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].section_header &&
            lines_[index].section == wanted_section) {
            sections.push_back(index);
        }
    }
    if (sections.size() > 1) return {false, sections.size() - 1};
    if (sections.empty()) return upsert(section, key, value);
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].assignment &&
            lines_[index].section == wanted_section &&
            lines_[index].key == wanted_key) {
            const auto current = utf8_to_wide(std::string_view{
                lines_[index].bytes}.substr(
                    lines_[index].value_start,
                    lines_[index].value_end - lines_[index].value_start));
            if (normalized(current) == normalized(value)) {
                return {false, 0};
            }
        }
    }

    std::size_t insert_at = sections.front() + 1;
    while (insert_at < lines_.size() && !lines_[insert_at].section_header) {
        ++insert_at;
    }
    const std::string ending =
        line_ending_ == LineEnding::crlf ? "\r\n" : "\n";
    if (insert_at > 0 && lines_[insert_at - 1].ending.empty()) {
        lines_[insert_at - 1].ending = ending;
    }
    Line added;
    added.bytes = encoded_key + "=" + encoded_value;
    added.ending = ending;
    added.section = wanted_section;
    added.key = wanted_key;
    added.value_start = encoded_key.size() + 1;
    added.value_end = added.bytes.size();
    added.assignment = true;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insert_at),
                  std::move(added));
    return {true, 0};
}

ReplaceResult IniDocument::remove_exact(std::wstring_view section,
                                        std::wstring_view key,
                                        std::wstring_view value) {
    const auto wanted_section = normalized(section);
    const auto wanted_key = normalized(key);
    if (wanted_section.empty() || wanted_key.empty()) return {false, 0};

    std::size_t section_count = 0;
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].section_header &&
            lines_[index].section == wanted_section) {
            ++section_count;
        }
        if (!lines_[index].assignment ||
            lines_[index].section != wanted_section ||
            lines_[index].key != wanted_key) {
            continue;
        }
        const auto current = utf8_to_wide(std::string_view{
            lines_[index].bytes}.substr(
                lines_[index].value_start,
                lines_[index].value_end - lines_[index].value_start));
        if (normalized(current) == normalized(value)) matches.push_back(index);
    }
    if (section_count > 1 || matches.size() > 1) {
        return {false, (section_count > 1 ? section_count - 1 : 0) +
                           (matches.size() > 1 ? matches.size() - 1 : 0)};
    }
    if (section_count == 0 || matches.empty()) return {false, 0};
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(matches.front()));
    return {true, 0};
}

ReplaceResult IniDocument::remove_section(std::wstring_view section) {
    const auto wanted_section = normalized(section);
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].section_header &&
            lines_[index].section == wanted_section) {
            matches.push_back(index);
        }
    }
    if (matches.size() != 1) {
        return {false, matches.size() > 1 ? matches.size() - 1 : 0};
    }

    const std::size_t begin = matches.front();
    std::size_t end = begin + 1;
    while (end < lines_.size() && !lines_[end].section_header) ++end;
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(begin),
                 lines_.begin() + static_cast<std::ptrdiff_t>(end));
    return {true, 0};
}

std::string IniDocument::serialize() const {
    std::string output = encoding_ == TextEncoding::utf8_bom ? "\xEF\xBB\xBF" : "";
    for (const auto& line : lines_) output += line.bytes + line.ending;
    return output;
}

TextEncoding IniDocument::encoding() const noexcept { return encoding_; }
LineEnding IniDocument::line_ending() const noexcept { return line_ending_; }

}  // namespace kf2::config
