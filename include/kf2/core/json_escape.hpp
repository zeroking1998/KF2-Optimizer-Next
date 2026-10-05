#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace kf2 {

// Escape string contents only; callers supply the surrounding quotes.
inline std::string json_escape(std::string_view value) {
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

}  // namespace kf2
