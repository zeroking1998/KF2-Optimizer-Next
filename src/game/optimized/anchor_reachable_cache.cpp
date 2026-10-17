#include "anchor_reachable_cache.hpp"
#include "anchor_walk_cycle.hpp"

#include <windows.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
constexpr std::size_t AnchorCallRva = 0x47DB86;
constexpr std::array<std::size_t, 2> ReachableCallRvas{0x69D011, 0x69D3E8};
constexpr std::size_t ReachableContextPrefix = 10;
constexpr std::array<unsigned char, 7> AnchorCallBytes{
    0x41, 0xFF, 0x92, 0xD8, 0x07, 0x00, 0x00};
constexpr std::array<unsigned char, 6> ReachableCallBytes{
    0xFF, 0x90, 0xE8, 0x08, 0x00, 0x00};
constexpr std::array<std::array<unsigned char, 16>, 2> ReachableContexts{{
    {0x45, 0x33, 0xC0, 0x48, 0x8D, 0x55, 0xA0, 0x48,
     0x8B, 0xCF, 0xFF, 0x90, 0xE8, 0x08, 0x00, 0x00},
    {0x45, 0x33, 0xC0, 0x48, 0x8D, 0x55, 0xD0, 0x48,
     0x8B, 0xCF, 0xFF, 0x90, 0xE8, 0x08, 0x00, 0x00},
}};

struct Vec3 { float x, y, z; };
using GetBestAnchor = void*(*)(void*, void*, const Vec3*, int, int, float*);
using Reachable = int(*)(void*, const Vec3*, void*);

using ReachKey = std::array<std::uint32_t, 3>;

struct CacheSlot {
    ReachKey key{};
    int result{};
    std::uint32_t generation{};
};

struct CacheState {
    static constexpr std::size_t Ways = 2;
    void* pawn{};
    Reachable reachable{};
    std::uint32_t generation{};
    unsigned suspended{};
    std::array<CacheSlot, 256> slots{};
    std::array<unsigned char, 256 / Ways> next_way{};
    CacheSlot* last{};
};

thread_local CacheState g_cache{};

std::array<std::uint32_t, 3> vector_bits(const Vec3* value) {
    std::array<std::uint32_t, 3> result;
    std::memcpy(result.data(), value, sizeof result);
    return result;
}

bool same_key(const ReachKey& left, const ReachKey& right) {
    return left == right;
}

std::uint64_t hash_key(const ReachKey& key) {
    const auto xy = static_cast<std::uint64_t>(key[0]) |
        (static_cast<std::uint64_t>(key[1]) << 32U);
    const auto hash = (xy ^ std::rotl(static_cast<std::uint64_t>(key[2]), 58)) *
                      0x9E3779B97F4A7C15ULL;
    return hash ^ (hash >> 32U);
}

__declspec(noinline) int reachable_active(void* pawn, const Vec3* destination,
                                          void* goal) {
    const auto original = g_cache.reachable;
    if(!original) return 0;

    // Both patched sites use the same FindAnchor test location for this scope.
    const auto key = vector_bits(destination);
    if(g_cache.last && g_cache.last->generation == g_cache.generation &&
       same_key(g_cache.last->key, key))
        return g_cache.last->result;
    constexpr auto set_count = g_cache.slots.size() / CacheState::Ways;
    const auto set = static_cast<std::size_t>(hash_key(key)) & (set_count - 1U);
    const auto first = set * CacheState::Ways;
    CacheSlot* replacement{};
    for(std::size_t way = 0; way < CacheState::Ways; ++way) {
        auto& slot = g_cache.slots[first + way];
        if(slot.generation != g_cache.generation) {
            replacement = &slot;
            break;
        }
        if(same_key(slot.key, key)) {
            g_cache.last = &slot;
            return slot.result;
        }
    }

    const auto walk_scope = anchor_walk_cycle_begin(pawn);
    const auto result = original(pawn, destination, goal);
    anchor_walk_cycle_end(walk_scope);
    if(!replacement) {
        replacement = &g_cache.slots[first + g_cache.next_way[set]];
        g_cache.next_way[set] ^= 1U;
    }
    replacement->key = key;
    replacement->result = result;
    replacement->generation = g_cache.generation;
    g_cache.last = replacement;
    return result;
}

int reachable_hook(void* pawn, const Vec3* destination, void* goal) {
    if(g_cache.pawn == pawn && !g_cache.suspended)
        return reachable_active(pawn, destination, goal);
    auto** table = pawn ? *reinterpret_cast<void***>(pawn) : nullptr;
    const auto original = table
        ? reinterpret_cast<Reachable>(table[0x8E8 / sizeof(void*)])
        : nullptr;
    return original ? original(pawn, destination, goal) : 0;
}

void* anchor_hook(void* pawn, void* actor, const Vec3* location, int start,
                  int visible, float* distance) {
    auto** table = pawn ? *reinterpret_cast<void***>(pawn) : nullptr;
    auto original = table
        ? reinterpret_cast<GetBestAnchor>(table[0x7D8 / sizeof(void*)])
        : nullptr;
    if(!original) return nullptr;
    if(g_cache.pawn) {
        ++g_cache.suspended;
        auto* result = original(pawn, actor, location, start, visible, distance);
        --g_cache.suspended;
        return result;
    }

    if(++g_cache.generation == 0) {
        for(auto& slot : g_cache.slots) slot.generation = 0;
        ++g_cache.generation;
        g_cache.last = nullptr;
    }
    g_cache.pawn = pawn;
    g_cache.reachable = table
        ? reinterpret_cast<Reachable>(table[0x8E8 / sizeof(void*)])
        : nullptr;
    auto* result = original(pawn, actor, location, start, visible, distance);
    g_cache.pawn = nullptr;
    g_cache.reachable = nullptr;
    return result;
}

void* relay_near(unsigned char* base, std::size_t image_size,
                 const void* target) {
    constexpr std::uintptr_t granularity = 0x10000U;
    constexpr std::uintptr_t limit = 0x70000000U;
    auto start = (reinterpret_cast<std::uintptr_t>(base) + image_size +
                  granularity - 1U) & ~(granularity - 1U);
    for(std::uintptr_t distance = 0; distance < limit; distance += granularity) {
        auto* memory = VirtualAlloc(reinterpret_cast<void*>(start + distance),
            0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if(!memory) continue;
        unsigned char code[]{0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
        const auto address = reinterpret_cast<std::uint64_t>(target);
        std::memcpy(code + 2, &address, sizeof address);
        std::memcpy(memory, code, sizeof code);
        DWORD old{};
        if(VirtualProtect(memory, 0x1000, PAGE_EXECUTE_READ, &old)) {
            FlushInstructionCache(GetCurrentProcess(), memory, sizeof code);
            return memory;
        }
        VirtualFree(memory, 0, MEM_RELEASE);
    }
    return nullptr;
}

bool relative_call(unsigned char* site, const void* target,
                   std::array<unsigned char, 5>& replacement) {
    const auto next = reinterpret_cast<std::intptr_t>(site + replacement.size());
    const auto distance = reinterpret_cast<std::intptr_t>(target) - next;
    if(distance < (std::numeric_limits<std::int32_t>::min)() ||
       distance > (std::numeric_limits<std::int32_t>::max)()) return false;
    replacement = {0xE8, 0, 0, 0, 0};
    const auto displacement = static_cast<std::int32_t>(distance);
    std::memcpy(replacement.data() + 1, &displacement, sizeof displacement);
    return true;
}
}

bool anchor_reachable_cache_install(unsigned char* image_base,
                                    std::size_t image_size) {
    if(!image_base || image_size <= ReachableCallRvas.back() +
                                    ReachableCallBytes.size()) return false;
    if(std::memcmp(image_base + AnchorCallRva, AnchorCallBytes.data(),
                   AnchorCallBytes.size()) != 0) return false;
    for(std::size_t i = 0; i < ReachableCallRvas.size(); ++i)
        if(std::memcmp(image_base + ReachableCallRvas[i] -
                          ReachableContextPrefix,
                       ReachableContexts[i].data(),
                       ReachableContexts[i].size()) != 0) return false;

    auto* anchor_relay = relay_near(image_base, image_size,
                                    reinterpret_cast<void*>(&anchor_hook));
    auto* reachable_relay = relay_near(image_base, image_size,
                                       reinterpret_cast<void*>(&reachable_hook));
    const auto abandon = [&] {
        if(anchor_relay) VirtualFree(anchor_relay, 0, MEM_RELEASE);
        if(reachable_relay) VirtualFree(reachable_relay, 0, MEM_RELEASE);
    };
    if(!anchor_relay || !reachable_relay) {
        abandon();
        return false;
    }

    std::array<unsigned char, 5> anchor_call{};
    std::array<std::array<unsigned char, 5>, ReachableCallRvas.size()>
        reachable_calls{};
    if(!relative_call(image_base + AnchorCallRva, anchor_relay, anchor_call)) {
        abandon();
        return false;
    }
    for(std::size_t i = 0; i < ReachableCallRvas.size(); ++i)
        if(!relative_call(image_base + ReachableCallRvas[i], reachable_relay,
                          reachable_calls[i])) {
            abandon();
            return false;
        }

    std::array<unsigned char, 7> anchor_replacement{};
    std::memcpy(anchor_replacement.data(), anchor_call.data(), anchor_call.size());
    anchor_replacement[5] = 0x90;
    anchor_replacement[6] = 0x90;
    std::array<std::array<unsigned char, 6>, ReachableCallRvas.size()>
        reachable_replacements{};
    for(std::size_t i = 0; i < ReachableCallRvas.size(); ++i) {
        std::memcpy(reachable_replacements[i].data(), reachable_calls[i].data(),
                    reachable_calls[i].size());
        reachable_replacements[i][5] = 0x90;
    }

    void* pages[]{image_base + (AnchorCallRva & ~std::size_t{0xFFF}),
                  image_base + (ReachableCallRvas.front() &
                                ~std::size_t{0xFFF})};
    DWORD old[2]{};
    if(!VirtualProtect(pages[0], 0x1000, PAGE_EXECUTE_READWRITE, &old[0])) {
        abandon();
        return false;
    }
    if(!VirtualProtect(pages[1], 0x1000, PAGE_EXECUTE_READWRITE, &old[1])) {
        DWORD ignored{};
        VirtualProtect(pages[0], 0x1000, old[0], &ignored);
        abandon();
        return false;
    }

    std::memcpy(image_base + AnchorCallRva, anchor_replacement.data(),
                anchor_replacement.size());
    for(std::size_t i = 0; i < ReachableCallRvas.size(); ++i)
        std::memcpy(image_base + ReachableCallRvas[i],
                    reachable_replacements[i].data(),
                    reachable_replacements[i].size());
    FlushInstructionCache(GetCurrentProcess(), image_base + AnchorCallRva,
                          anchor_replacement.size());
    FlushInstructionCache(GetCurrentProcess(),
                          image_base + ReachableCallRvas.front(),
                          ReachableCallRvas.back() + ReachableCallBytes.size() -
                              ReachableCallRvas.front());
    DWORD ignored{};
    const bool restored_second =
        VirtualProtect(pages[1], 0x1000, old[1], &ignored) != FALSE;
    const bool restored_first =
        VirtualProtect(pages[0], 0x1000, old[0], &ignored) != FALSE;
    return restored_first && restored_second;
}
