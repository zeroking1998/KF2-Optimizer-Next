#include "anchor_fast_fallback.hpp"
#include "anchor_reachable_cache.hpp"
#include "anchor_walk_cycle.hpp"

#include <windows.h>

#include "kf2/game/anchor_build_identity.hpp"

namespace {
constexpr unsigned KfGameTimestamp = 0x65B94562U;
constexpr unsigned KfGameImageSize = 0x2481000U;
volatile LONG g_status{};

void install_anchor_runtime() noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp != KfGameTimestamp ||
        nt->OptionalHeader.SizeOfImage != KfGameImageSize) {
        return;
    }

    LONG status = 0;
    if (anchor_fast_fallback_install(base, nt->OptionalHeader.SizeOfImage)) {
        status |= 1;
    }
    if (anchor_reachable_cache_install(base,
                                       nt->OptionalHeader.SizeOfImage)) {
        status |= 2;
        if (anchor_walk_cycle_install(base,
                                      nt->OptionalHeader.SizeOfImage)) {
            status |= 4;
        }
    }
    InterlockedExchange(&g_status, status);
}
}  // namespace

extern "C" __declspec(dllexport) LONG
kf2_anchor_runtime_status() noexcept {
    return InterlockedCompareExchange(&g_status, 0, 0);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_ATTACH) install_anchor_runtime();
    return TRUE;
}
