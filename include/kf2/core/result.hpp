#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace kf2 {

enum class ErrorCode {
    none,
    invalid_argument,
    not_found,
    access_denied,
    io_failure,
    platform_failure,
    stale_data,
    already_running,
    internal_failure,
};

struct Error {
    ErrorCode code{ErrorCode::none};
    std::wstring message;
    std::uint32_t native_code{0};
};

template <typename T>
class Result final {
public:
    [[nodiscard]] static Result success(T value) {
        return Result{std::in_place_index<0>, std::move(value)};
    }

    [[nodiscard]] static Result failure(Error error) {
        return Result{std::in_place_index<1>, std::move(error)};
    }

    [[nodiscard]] bool has_value() const noexcept {
        return value_.index() == 0;
    }

    [[nodiscard]] T& value() {
        if (!has_value()) {
            throw std::logic_error{"Result does not contain a value"};
        }
        return std::get<0>(value_);
    }

    [[nodiscard]] const T& value() const {
        if (!has_value()) {
            throw std::logic_error{"Result does not contain a value"};
        }
        return std::get<0>(value_);
    }

    [[nodiscard]] Error& error() {
        if (has_value()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<1>(value_);
    }

    [[nodiscard]] const Error& error() const {
        if (has_value()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<1>(value_);
    }

private:
    template <std::size_t Index, typename U>
    explicit Result(std::in_place_index_t<Index> index, U&& value)
        : value_{index, std::forward<U>(value)} {}

    std::variant<T, Error> value_;
};

}  // namespace kf2
