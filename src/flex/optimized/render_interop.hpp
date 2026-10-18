#pragma once

#include <cstdint>

// Optional exact-image DX11/CUDA fast path. Any unsupported state keeps KF2's
// original CPU vertex upload.
void p4_install_render_interop();
void p4_render_generation(void* solver, std::uint64_t generation);
int p4_render_snapshot(void* solver, void* context, std::uint64_t generation,
                       int capacity, int prefix,
                       const void* positions, const void* normals);
void p4_render_publish(void* solver, std::uint64_t generation, int slot);
void p4_release_render_interop(void* solver);
