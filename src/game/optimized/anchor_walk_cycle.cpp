#include "anchor_walk_cycle.hpp"

#include <windows.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
constexpr std::size_t WalkMoveRva = 0x6720D0;
constexpr std::array<std::size_t, 2> WalkMoveCallRvas{0x66FCF4, 0x66FE09};
constexpr std::size_t ResultDispatchRva = 0x66FE19;
constexpr std::size_t CycleExitRva = 0x670100;
constexpr std::size_t HitGoalRva = 0x66FE1E;
constexpr std::size_t OtherResultRva = 0x66FE2B;
constexpr std::array<std::array<unsigned char, 5>, 2> WalkMoveCallBytes{{
    {0xE8, 0xD7, 0x23, 0x00, 0x00},
    {0xE8, 0xC2, 0x22, 0x00, 0x00},
}};
constexpr std::array<unsigned char, 5> ResultDispatchBytes{
    0x83, 0xF8, 0x05, 0x75, 0x0D};
constexpr int TestMoveMoved = 1;
constexpr int TestMoveCycle = 3;

struct Vec3 { float x, y, z; };
using WalkMove = int(*)(void*, const Vec3*, Vec3*, const Vec3*, void*,
                        void*, float);

struct WalkKey {
    std::array<std::uint32_t, 3> start{};
    std::array<std::uint32_t, 2> delta{};
    std::uint32_t threshold{};
    bool operator==(const WalkKey&) const = default;
};
static_assert(sizeof(WalkKey) == 24);

struct WalkState {
    static constexpr std::size_t Ways = 2;
    void* pawn{};
    std::uint32_t generation{};
    unsigned suspended{};
    struct Slot {
        WalkKey key{};
        std::uint32_t fingerprint{};
        std::uint32_t generation{};
    };
    std::array<Slot, 512> slots{};
    std::array<unsigned char, 512 / Ways> next_way{};
};
static_assert(sizeof(WalkState::Slot) == 32);

WalkMove g_original{};
bool g_installed{};
thread_local WalkState g_walk{};

std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

std::array<std::uint32_t, 3> vector_bits(const Vec3* value) {
    std::array<std::uint32_t, 3> result;
    std::memcpy(result.data(), value, sizeof result);
    return result;
}

std::array<std::uint32_t, 2> vector_xy_bits(const Vec3* value) {
    std::array<std::uint32_t, 2> result;
    std::memcpy(result.data(), value, sizeof result);
    return result;
}

bool same_key(const WalkKey& left, const WalkKey& right) {
    return left == right;
}

std::uint64_t hash_key(const WalkKey& key) {
    const auto first = static_cast<std::uint64_t>(key.start[0]) |
        (static_cast<std::uint64_t>(key.start[1]) << 32U);
    const auto second = static_cast<std::uint64_t>(key.start[2]) |
        (static_cast<std::uint64_t>(key.delta[0]) << 32U);
    const auto third = static_cast<std::uint64_t>(key.delta[1]) |
        (static_cast<std::uint64_t>(key.threshold) << 32U);
    const auto hash = (first ^ std::rotl(second, 21) ^ third) *
                      0x9E3779B97F4A7C15ULL;
    return hash ^ (hash >> 32U);
}

__declspec(noinline) int walk_move_active(void* pawn, const Vec3* delta,
                                          Vec3* current, const Vec3* extent,
                                          void* hit, void* goal,
                                          float threshold) {
    // Pawn, extent and goal are invariant within this Reachable scope.
    const WalkKey key{vector_bits(current), vector_xy_bits(delta),
                      bits(threshold)};
    const auto hash = hash_key(key);
    const auto fingerprint = static_cast<std::uint32_t>(hash >> 32U);
    constexpr auto set_count = g_walk.slots.size() / WalkState::Ways;
    const auto set = static_cast<std::size_t>(hash) & (set_count - 1U);
    const auto first = set * WalkState::Ways;
    WalkState::Slot* replacement{};
    for(std::size_t way = 0; way < WalkState::Ways; ++way) {
        auto& slot = g_walk.slots[first + way];
        if(slot.generation != g_walk.generation) {
            replacement = &slot;
            break;
        }
        if(slot.fingerprint == fingerprint && same_key(slot.key, key))
            return TestMoveCycle;
    }

    const auto result =
        g_original(pawn, delta, current, extent, hit, goal, threshold);
    if(result == TestMoveMoved) {
        if(!replacement) {
            replacement = &g_walk.slots[first + g_walk.next_way[set]];
            g_walk.next_way[set] ^= 1U;
        }
        replacement->key = key;
        replacement->fingerprint = fingerprint;
        replacement->generation = g_walk.generation;
    }
    return result;
}

int walk_move_hook(void* pawn, const Vec3* delta, Vec3* current,
                   const Vec3* extent, void* hit, void* goal,
                   float threshold) {
    if(g_walk.pawn != pawn || g_walk.suspended)
        return g_original(pawn, delta, current, extent, hit, goal, threshold);
    return walk_move_active(pawn, delta, current, extent, hit, goal, threshold);
}

struct Relays {
    unsigned char* page{};
    void* walk_move{};
    void* result_dispatch{};
};

Relays relays_near(unsigned char* base, std::size_t image_size,
                   const void* target) {
    constexpr std::uintptr_t granularity = 0x10000U;
    constexpr std::uintptr_t limit = 0x70000000U;
    constexpr std::size_t dispatch_offset = 0x20;
    const auto start = (reinterpret_cast<std::uintptr_t>(base) + image_size +
                        granularity - 1U) & ~(granularity - 1U);
    for(std::uintptr_t distance = 0; distance < limit; distance += granularity) {
        auto* memory = static_cast<unsigned char*>(VirtualAlloc(
            reinterpret_cast<void*>(start + distance), 0x1000,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if(!memory) continue;

        unsigned char walk_code[]{
            0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
        const auto address = reinterpret_cast<std::uint64_t>(target);
        std::memcpy(walk_code + 2, &address, sizeof address);
        std::memcpy(memory, walk_code, sizeof walk_code);

        std::array<unsigned char, 23> dispatch{
            0x83, 0xF8, TestMoveCycle,
            0x0F, 0x84, 0, 0, 0, 0,
            0x83, 0xF8, 0x05,
            0x0F, 0x85, 0, 0, 0, 0,
            0xE9, 0, 0, 0, 0};
        auto* dispatch_address = memory + dispatch_offset;
        const auto set_branch = [&](std::size_t field, std::size_t next,
                                    std::size_t target_rva) {
            const auto delta = reinterpret_cast<std::intptr_t>(
                base + target_rva) - reinterpret_cast<std::intptr_t>(
                    dispatch_address + next);
            if(delta < (std::numeric_limits<std::int32_t>::min)() ||
               delta > (std::numeric_limits<std::int32_t>::max)())
                return false;
            const auto relative = static_cast<std::int32_t>(delta);
            std::memcpy(dispatch.data() + field, &relative, sizeof relative);
            return true;
        };
        if(!set_branch(5, 9, CycleExitRva) ||
           !set_branch(14, 18, OtherResultRva) ||
           !set_branch(19, 23, HitGoalRva)) {
            VirtualFree(memory, 0, MEM_RELEASE);
            continue;
        }
        std::memcpy(dispatch_address, dispatch.data(), dispatch.size());

        DWORD old{};
        if(VirtualProtect(memory, 0x1000, PAGE_EXECUTE_READ, &old)) {
            FlushInstructionCache(GetCurrentProcess(), memory,
                                  dispatch_offset + dispatch.size());
            return {memory, memory, dispatch_address};
        }
        VirtualFree(memory, 0, MEM_RELEASE);
    }
    return {};
}

bool relative_branch(unsigned char* site, const void* target,
                     unsigned char opcode,
                     std::array<unsigned char, 5>& replacement) {
    const auto next = reinterpret_cast<std::intptr_t>(site + replacement.size());
    const auto distance = reinterpret_cast<std::intptr_t>(target) - next;
    if(distance < (std::numeric_limits<std::int32_t>::min)() ||
       distance > (std::numeric_limits<std::int32_t>::max)()) return false;
    replacement = {opcode, 0, 0, 0, 0};
    const auto displacement = static_cast<std::int32_t>(distance);
    std::memcpy(replacement.data() + 1, &displacement, sizeof displacement);
    return true;
}
}

bool anchor_walk_cycle_install(unsigned char* image_base,
                               std::size_t image_size) {
    if(!image_base || image_size <= WalkMoveRva ||
       image_size < WalkMoveCallRvas.back() +
                        WalkMoveCallBytes.back().size()) return false;
    for(std::size_t index = 0; index < WalkMoveCallRvas.size(); ++index)
        if(std::memcmp(image_base + WalkMoveCallRvas[index],
                       WalkMoveCallBytes[index].data(),
                       WalkMoveCallBytes[index].size()) != 0) return false;
    if(std::memcmp(image_base + ResultDispatchRva,
                   ResultDispatchBytes.data(),
                   ResultDispatchBytes.size()) != 0) return false;

    const auto relays = relays_near(
        image_base, image_size, reinterpret_cast<void*>(&walk_move_hook));
    if(!relays.page) return false;
    std::array<std::array<unsigned char, 5>, WalkMoveCallRvas.size()>
        replacements{};
    for(std::size_t index = 0; index < WalkMoveCallRvas.size(); ++index)
        if(!relative_branch(image_base + WalkMoveCallRvas[index],
                            relays.walk_move, 0xE8,
                            replacements[index])) {
            VirtualFree(relays.page, 0, MEM_RELEASE);
            return false;
        }
    std::array<unsigned char, 5> result_replacement{};
    if(!relative_branch(image_base + ResultDispatchRva,
                        relays.result_dispatch, 0xE9,
                        result_replacement)) {
        VirtualFree(relays.page, 0, MEM_RELEASE);
        return false;
    }

    auto* page = image_base + (WalkMoveCallRvas.front() & ~std::size_t{0xFFF});
    DWORD old{};
    if(!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(relays.page, 0, MEM_RELEASE);
        return false;
    }

    g_original = reinterpret_cast<WalkMove>(image_base + WalkMoveRva);
    for(std::size_t index = 0; index < WalkMoveCallRvas.size(); ++index)
        std::memcpy(image_base + WalkMoveCallRvas[index],
                    replacements[index].data(), replacements[index].size());
    std::memcpy(image_base + ResultDispatchRva, result_replacement.data(),
                result_replacement.size());
    FlushInstructionCache(GetCurrentProcess(),
                          image_base + WalkMoveCallRvas.front(),
                          ResultDispatchRva + ResultDispatchBytes.size() -
                              WalkMoveCallRvas.front());
    DWORD ignored{};
    const auto restored =
        VirtualProtect(page, 0x1000, old, &ignored) != FALSE;
    g_installed = restored;
    return restored;
}

AnchorWalkCycleScope anchor_walk_cycle_begin(void* pawn) {
    if(!g_installed || !pawn) return AnchorWalkCycleScope::none;
    if(g_walk.pawn) {
        ++g_walk.suspended;
        return AnchorWalkCycleScope::suspended;
    }
    if(++g_walk.generation == 0) {
        for(auto& slot : g_walk.slots) slot.generation = 0;
        ++g_walk.generation;
    }
    g_walk.pawn = pawn;
    return AnchorWalkCycleScope::owner;
}

void anchor_walk_cycle_end(AnchorWalkCycleScope scope) {
    if(scope == AnchorWalkCycleScope::owner) {
        g_walk.pawn = nullptr;
        return;
    }
    if(scope == AnchorWalkCycleScope::suspended && g_walk.suspended)
        --g_walk.suspended;
}
