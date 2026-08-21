#pragma once

#include <exception>
#include <string>
#include <utility>

namespace pmon::util {

class Exception : public std::exception {
public:
    explicit Exception(std::string message = {}) : message_{std::move(message)} {}
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

template <class Error, typename... Args>
Error Except(Args&&... args) {
    return Error{std::forward<Args>(args)...};
}

inline std::string ReportException() noexcept { return "PresentMon exception"; }

namespace str {
inline std::string ToNarrow(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        result.push_back(character >= 0 && character <= 0x7f
                             ? static_cast<char>(character)
                             : '?');
    }
    return result;
}
}  // namespace str

}  // namespace pmon::util

#define PM_DEFINE_EX(name)                                                      \
    class name : public ::pmon::util::Exception {                              \
    public:                                                                     \
        using Exception::Exception;                                             \
    }
