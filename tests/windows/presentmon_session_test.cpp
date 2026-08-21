#include <Windows.h>

#include <cstdlib>
#include <iostream>

#include "kf2/platform/windows/presentmon_session.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::telemetry;
    const SampleIdentity identity{GetCurrentProcessId(), 1};
    PresentSource source{identity, 120};
    CHECK(source.start().has_value());
    auto session =
        kf2::platform::windows::PresentMonSession::start(identity, source);
    if (!session.has_value()) {
        std::wcerr << L"PresentMon unavailable: " << session.error().message
                   << L" (" << session.error().native_code << L")\n";
        return EXIT_FAILURE;
    }
    Sleep(20);
    CHECK(session.value()->stop().has_value());
    CHECK(session.value()->stop().has_value());
    CHECK(source.stop().has_value());
    return EXIT_SUCCESS;
}
