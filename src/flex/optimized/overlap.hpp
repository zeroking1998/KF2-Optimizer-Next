#pragma once
#include <cstddef>
#include <cstdint>

// Private raw31 ABI. State belongs to the solver, never to a global cache.
struct P4State {
    unsigned initialized, thread;
    void *context, *stream, *ready_event, *tail_event;
    unsigned flags, depth;
    std::uint64_t generation, ready_generation, tail_generation;
    const int* native_error;
};
static_assert(sizeof(P4State) == 80);
static_assert(offsetof(P4State, flags) == 40);
constexpr std::size_t P4StateOffset = 0x560;
constexpr std::size_t P4SolverBytes = 0x5B0;
constexpr std::size_t P4PresentationOffset = 0x5B8;
constexpr std::size_t P4ExtendedSolverBytes = 0x5C0;
enum : unsigned {
    P4Ready = 1, P4ComputePending = 2, P4TailRecorded = 4,
    P4CopyPending = 8, P4Joining = 16, P4ReadbackLinked = 32,
    P4StepEligible = 64, P4PullScope = 128, P4PullContextStable = 256,
    P4PullEligible = 512
};
using P4Unary = void(*)(void*);
using P4Density = void(*)(void*, void*, int);
#ifdef P4_CPU_MODEL
#define P4_API extern "C"
#else
#define P4_API extern "C" __declspec(dllexport)
#endif
P4_API void p4_before(void* solver, const int* error);
P4_API void p4_ready(void* solver, int status);
P4_API void p4_tail(void* solver);
P4_API void p4_invalidate(void* solver);
P4_API int p4_copy(void* out, const void* in, std::size_t bytes, int kind, void* solver, unsigned asynchronous);
P4_API void p4_pull(void* container, P4Unary original);
P4_API void p4_density(void* solver, void* target, P4Density original);
P4_API void p4_destroy_solver(void* solver, P4Unary original);
P4_API void p4_destroy_container(void* container, P4Unary original);


