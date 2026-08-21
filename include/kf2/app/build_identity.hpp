#pragma once

#include <string>

namespace kf2::app {

struct BuildIdentity {
    std::string version;
    std::string commit;
    std::string channel;
};

[[nodiscard]] BuildIdentity current_build_identity();
[[nodiscard]] std::string format_build_identity(const BuildIdentity& identity);

}  // namespace kf2::app
