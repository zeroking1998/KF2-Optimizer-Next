#pragma once

#include <utility>

namespace pmon::util::log {

struct NullLogEntry {
    template <typename T>
    NullLogEntry& hr(T&&) noexcept {
        return *this;
    }

    explicit operator bool() const noexcept { return true; }
};

template <typename... Args>
NullLogEntry make_log(Args&&...) noexcept {
    return {};
}

}  // namespace pmon::util::log

#define pmlog_fatal(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_error(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_warn(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_info(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_dbg(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_verb(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_verb2(...) ::pmon::util::log::make_log(__VA_ARGS__)
#define pmlog_perf(...) ::pmon::util::log::make_log(__VA_ARGS__)
