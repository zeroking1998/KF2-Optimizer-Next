#pragma once

#include <cstddef>
#include <cstdint>

enum class AnchorWalkCycleScope : std::uint8_t {
    none,
    owner,
    suspended,
};

bool anchor_walk_cycle_install(unsigned char* image_base,
                               std::size_t image_size);
AnchorWalkCycleScope anchor_walk_cycle_begin(void* pawn);
void anchor_walk_cycle_end(AnchorWalkCycleScope scope);
