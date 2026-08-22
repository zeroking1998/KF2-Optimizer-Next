#include "kf2/app/build_identity.hpp"

#ifndef KF2_VERSION
#define KF2_VERSION "0.0.4-alpha"
#endif

#ifndef KF2_BUILD_COMMIT
#define KF2_BUILD_COMMIT "unknown"
#endif

#ifndef KF2_BUILD_CHANNEL
#define KF2_BUILD_CHANNEL "dev"
#endif

namespace kf2::app {

BuildIdentity current_build_identity() {
    return {KF2_VERSION, KF2_BUILD_COMMIT, KF2_BUILD_CHANNEL};
}

std::string format_build_identity(const BuildIdentity& identity) {
    return identity.version + "+" + identity.commit + " (" + identity.channel +
           ")";
}

}  // namespace kf2::app
