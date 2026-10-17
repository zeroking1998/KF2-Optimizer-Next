#include "anchor_fast_fallback.hpp"

#include <windows.h>

#include <array>
#include <cstring>

namespace {
constexpr std::size_t FastFallbackRva = 0x69E653;
constexpr std::array<unsigned char, 8> OriginalBytes{
    0x4D, 0x85, 0xE4, 0x75, 0x03, 0x4C, 0x8B, 0x27};
constexpr std::array<unsigned char, 8> ReplacementBytes{
    0x4C, 0x8B, 0x27, 0xEB, 0x19, 0x90, 0x90, 0x90};
}

bool anchor_fast_fallback_install(unsigned char* image_base,
                                  std::size_t image_size) {
    if(!image_base || image_size < FastFallbackRva + OriginalBytes.size())
        return false;
    auto* site = image_base + FastFallbackRva;
    if(std::memcmp(site, OriginalBytes.data(), OriginalBytes.size()) != 0)
        return false;

    DWORD old_protection{};
    if(!VirtualProtect(site, ReplacementBytes.size(), PAGE_EXECUTE_READWRITE,
                       &old_protection))
        return false;
    std::memcpy(site, ReplacementBytes.data(), ReplacementBytes.size());
    FlushInstructionCache(GetCurrentProcess(), site, ReplacementBytes.size());
    DWORD ignored{};
    return VirtualProtect(site, ReplacementBytes.size(), old_protection,
                          &ignored) != FALSE;
}
