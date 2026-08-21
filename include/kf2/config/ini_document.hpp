#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/core/result.hpp"

namespace kf2::config {

enum class TextEncoding { utf8, utf8_bom };
enum class LineEnding { lf, crlf };

struct ReplaceResult {
    bool changed{false};
    std::size_t shadowed_occurrences{0};
};

class IniDocument final {
public:
    [[nodiscard]] static Result<IniDocument> parse(std::string_view bytes);
    [[nodiscard]] std::optional<std::wstring> find(
        std::wstring_view section, std::wstring_view key) const;
    [[nodiscard]] ReplaceResult replace(std::wstring_view section,
                                        std::wstring_view key,
                                        std::wstring_view value);
    [[nodiscard]] ReplaceResult upsert(std::wstring_view section,
                                       std::wstring_view key,
                                       std::wstring_view value);
    // Appends one array-style value without replacing existing values for the
    // same key. Duplicate section headers are rejected through
    // shadowed_occurrences so callers can fail closed.
    [[nodiscard]] ReplaceResult append_unique(std::wstring_view section,
                                              std::wstring_view key,
                                              std::wstring_view value);
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] TextEncoding encoding() const noexcept;
    [[nodiscard]] LineEnding line_ending() const noexcept;

private:
    struct Line {
        std::string bytes;
        std::string ending;
        std::wstring section;
        std::wstring key;
        std::size_t value_start{0};
        std::size_t value_end{0};
        bool assignment{false};
        bool section_header{false};
    };

    TextEncoding encoding_{TextEncoding::utf8};
    LineEnding line_ending_{LineEnding::lf};
    std::vector<Line> lines_;
};

}  // namespace kf2::config
