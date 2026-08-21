#include <Windows.h>

#include <cstdint>

void InitializeTimestampInfo(LARGE_INTEGER*, LARGE_INTEGER const&) {}

int PrintTime(std::uint64_t) { return 0; }

int PrintTimeDelta(std::uint64_t) { return 0; }
