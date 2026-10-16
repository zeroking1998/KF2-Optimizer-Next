#include "overlap.hpp"
#include "presentation.hpp"
#include <windows.h>
#include <cstring>
#include <limits>

// Link to this game's CUDA75 ABI, not a newer SDK. Tests replace these genuine
// boundaries; they do not load cudart/nvcuda or execute DllMain.
#ifdef P4_CPU_MODEL
#define CUDA_API extern "C"
void p4_fail_closed();
#else
#define CUDA_API extern "C" __declspec(dllimport)
[[noreturn]] static void p4_fail_closed() { RaiseFailFastException(nullptr, nullptr, 0); __assume(0); }
#endif
CUDA_API int cudaStreamCreateWithFlags(void**, unsigned);
CUDA_API int cudaStreamDestroy(void*);
CUDA_API int cudaStreamSynchronize_ptsz(void*);
CUDA_API int cudaStreamWaitEvent_ptsz(void*, void*, unsigned);
CUDA_API int cudaEventCreateWithFlags(void**, unsigned);
CUDA_API int cudaEventDestroy(void*);
CUDA_API int cudaEventRecord_ptsz(void*, void*);
CUDA_API int cudaEventSynchronize(void*);
CUDA_API int cudaPeekAtLastError();
CUDA_API int cudaMemcpy_ptds(void*, const void*, std::size_t, int);
CUDA_API int cudaMemcpyAsync_ptsz(void*, const void*, std::size_t, int, void*);
CUDA_API int cuCtxGetCurrent(void**);
CUDA_API int cuCtxPushCurrent_v2(void*);
CUDA_API int cuCtxPopCurrent_v2(void**);
CUDA_API int cuCtxSynchronize();

namespace {
template<class T> T field(const void* solver, std::size_t offset) {
    T value; std::memcpy(&value, static_cast<const char*>(solver)+offset, sizeof value); return value;
}
template<class T> void put(void* solver, std::size_t offset, T value) {
    std::memcpy(static_cast<char*>(solver)+offset,&value,sizeof value);
}
P4State& state(void* solver) { return *reinterpret_cast<P4State*>(static_cast<char*>(solver)+P4StateOffset); }
void must(int error) { if(error) p4_fail_closed(); }
bool native_ok(const P4State& s) { return s.native_error && !*s.native_error; }
void clear_ready(P4State& s) { s.flags &= ~P4Ready; s.ready_generation = 0; }

void finish_copies(P4State& s) {
    if(s.flags & P4CopyPending) { must(cudaStreamSynchronize_ptsz(s.stream)); s.flags &= ~P4CopyPending; }
}
void drain(P4State& s) {
    if(!s.context) return;
    if(s.flags & P4Joining) { p4_fail_closed(); return; }
    s.flags |= P4Joining;
    must(cuCtxPushCurrent_v2(s.context));
    // No reliance on another thread's default stream or on implicit cudaFree.
    if(s.flags & P4ComputePending) {
        if((s.flags & P4TailRecorded) && s.tail_generation == s.generation)
            must(cudaEventSynchronize(s.tail_event));
        else must(cuCtxSynchronize()); // Aborted step: no valid final event.
    }
    finish_copies(s);
    void* popped{}; must(cuCtxPopCurrent_v2(&popped));
    if(popped != s.context) p4_fail_closed();
    s.flags &= ~(P4ComputePending | P4TailRecorded | P4Joining);
    clear_ready(s);
}
void release_handles(P4State& s) {
    if(s.stream) { must(cudaStreamDestroy(s.stream)); s.stream = nullptr; }
    if(s.ready_event) { must(cudaEventDestroy(s.ready_event)); s.ready_event = nullptr; }
    if(s.tail_event) { must(cudaEventDestroy(s.tail_event)); s.tail_event = nullptr; }
}
bool initialize(P4State& s) {
    // At most once per solver. Allocation failure keeps the parent's path.
    if(s.initialized) return s.initialized == 1;
    s.initialized = 2;
    if(cuCtxGetCurrent(&s.context) || !s.context) { s.context = nullptr; return false; }
    void* created{};
    if(cudaStreamCreateWithFlags(&created,1)) return false;
    if(!created) p4_fail_closed(); s.stream=created;
    created=nullptr;
    if(cudaEventCreateWithFlags(&created,3)) { release_handles(s); return false; }
    if(!created) p4_fail_closed(); s.ready_event=created; // Blocking sync + no timing.
    created=nullptr;
    if(cudaEventCreateWithFlags(&created,3)) { release_handles(s); return false; }
    if(!created) p4_fail_closed(); s.tail_event=created;
    s.thread = GetCurrentThreadId(); s.initialized = 1; return true;
}
struct Range { std::uintptr_t begin, end; };
bool range(const void* p, std::size_t size, Range& result) {
    const auto begin = reinterpret_cast<std::uintptr_t>(p);
    if(!begin || !size || size > (std::numeric_limits<std::uintptr_t>::max)()-begin) return false;
    result = {begin, begin+size}; return true;
}
bool disjoint(Range a, Range b) { return a.end <= b.begin || b.end <= a.begin; }
bool independent(void* solver) {
    const auto capacity = field<int>(solver, 0x1A0), active = field<int>(solver, 0x198), rigid = field<int>(solver, 0x138);
    if(capacity <= 0 || capacity > 0x1000000 || active <= 0 || active > capacity || rigid < 0 || rigid > capacity) return false;
    constexpr std::size_t late[] = {0x50, 0x58, 0x60, 0x68, 0x70};
    constexpr std::size_t source[] = {0, 0x10, 0x20, 0x30, 0x48, 0x128, 0x130};
    constexpr unsigned stride[] = {16, 12, 4, 4, 16, 36, 12};
    for(unsigned i = 0; i < 7; ++i) {
        const auto count = i < 5 ? capacity : rigid;
        if(!count) continue;
        Range a{}; if(!range(field<void*>(solver, source[i]), static_cast<std::size_t>(count)*stride[i], a)) return false;
        for(auto offset : late) {
            Range b{}; if(!range(field<void*>(solver, offset), static_cast<std::size_t>(capacity)*16, b) || !disjoint(a,b)) return false;
        }
    }
    return true;
}
bool scope_eligible(void* solver, const P4State& s) {
    return s.initialized == 1 && s.thread == GetCurrentThreadId() && native_ok(s) &&
           (s.flags & P4Ready) && s.ready_generation == s.generation &&
           field<unsigned>(solver, 0x53C) == 0x80000002U;
}
bool supported(void* solver, const void* in, std::size_t bytes) {
    constexpr std::size_t source[] = {0,0x10,0x20,0x30,0x48,0x128,0x130};
    constexpr unsigned stride[] = {16,12,4,4,16,36,12};
    for(unsigned i = 0; i < 7; ++i) if(in == field<void*>(solver,source[i])) {
        const auto count = field<int>(solver, i < 5 ? 0x1A0 : 0x138);
        return count > 0 && bytes && bytes <= static_cast<std::size_t>(count)*stride[i] && bytes%stride[i] == 0;
    }
    return false;
}
void enter_scope(void* solver) {
    auto& s = state(solver);
    if(s.depth) { drain(s); s.initialized = 2; }
    ++s.depth;
}
void leave_scope(void* solver, bool abnormal) {
    auto& s = state(solver);
    // Copy failure was drained BEFORE the original error callback can throw.
    if(s.flags & P4CopyPending) {
        must(cuCtxPushCurrent_v2(s.context)); finish_copies(s);
        void* popped{}; must(cuCtxPopCurrent_v2(&popped)); if(popped != s.context) p4_fail_closed();
    }
    if(s.depth) --s.depth;
    if(abnormal) drain(s);
}
}

P4_API void p4_before(void* solver, const int* error) {
    auto& s = state(solver);
    s.native_error = error;
    if(s.depth) { drain(s); s.initialized = 2; return; }
    const bool eligible = field<unsigned>(solver,0x53C) == 0x80000002U && field<unsigned char>(solver,0x220) && native_ok(s);
    clear_ready(s); s.flags &= ~P4StepEligible;
    if(eligible && !s.initialized) { p4_install_presentation(); initialize(s); }
    if(!s.context) return; // Non-fluid scenes allocate no overlap resources.
    void* context{}; must(cuCtxGetCurrent(&context));
    if(context != s.context || GetCurrentThreadId() != s.thread) {
        drain(s); s.initialized = 2; // Unsupported issuing domain: no routing.
    }
    finish_copies(s); clear_ready(s);
    if(++s.generation == 0) { drain(s); s.initialized = 2; s.flags=P4ComputePending; return; }
    s.flags = P4ComputePending | ((eligible && s.initialized == 1) ? P4StepEligible : 0U);
    s.tail_generation = 0;
}
P4_API void p4_ready(void* solver, int status) {
    auto& s = state(solver); clear_ready(s);
    if(s.initialized != 1 || !(s.flags & P4StepEligible) || status || !native_ok(s) || cudaPeekAtLastError() || !independent(solver)) return;
    if(cudaEventRecord_ptsz(s.ready_event,nullptr)) { s.initialized = 2; return; }
    s.ready_generation = s.generation; s.flags |= P4Ready;
}
P4_API void p4_tail(void* solver) {
    auto& s = state(solver);
    if(!s.tail_event || !(s.flags & P4ComputePending)) return;
    if(cudaEventRecord_ptsz(s.tail_event,nullptr)) { clear_ready(s); s.initialized = 2; return; }
    s.tail_generation = s.generation; s.flags |= P4TailRecorded;
}
P4_API void p4_invalidate(void* solver) {
    auto& s=state(solver);
    if(s.depth) p4_fail_closed();
    drain(s); // Before mutation/rebinding, never merely clear a token.
}
P4_API int p4_copy(void* out, const void* in, std::size_t bytes, int kind, void* solver, unsigned asynchronous) {
    auto& s = state(solver);
    void* context{};
    if(!s.depth || !scope_eligible(solver,s) || kind != 2 || !out || !supported(solver,in,bytes)) {
        if(s.depth) finish_copies(s);
        return asynchronous ? cudaMemcpyAsync_ptsz(out,in,bytes,kind,nullptr) : cudaMemcpy_ptds(out,in,bytes,kind);
    }
    if(cuCtxGetCurrent(&context) || context != s.context) {
        drain(s); s.initialized = 2;
        return asynchronous ? cudaMemcpyAsync_ptsz(out,in,bytes,kind,nullptr) : cudaMemcpy_ptds(out,in,bytes,kind);
    }
    int error = 0;
    if(!(s.flags & P4ReadbackLinked)) {
        error = cudaStreamWaitEvent_ptsz(s.stream,s.ready_event,0);
        if(!error) s.flags |= P4ReadbackLinked;
    }
    if(!error) { s.flags |= P4CopyPending; error = cudaMemcpyAsync_ptsz(out,in,bytes,kind,s.stream); }
    if(!error && !asynchronous) { error = cudaStreamSynchronize_ptsz(s.stream); if(!error) s.flags &= ~P4CopyPending; }
    if(error) { clear_ready(s); drain(s); s.initialized = 2; }
    return error; // Original native copy error-check/callback remains intact.
}
P4_API void p4_pull(void* container, P4Unary original) {
    void* solver = field<void*>(container,8); enter_scope(solver);
    __try { original(container); }
    __finally { leave_scope(solver,AbnormalTermination()!=0); }
}
P4_API void p4_density(void* solver, void* target, P4Density original) {
    enter_scope(solver);
    __try { original(solver,target,0); }
    __finally { leave_scope(solver,AbnormalTermination()!=0); }
}
P4_API void p4_destroy_container(void* container, P4Unary original) {
    auto& s = state(field<void*>(container,8));
    if(s.depth) p4_fail_closed();
    drain(s);
    void* solver=field<void*>(container,8);
#ifdef P4_TIME_ACCUMULATOR
    put(solver,P4SolverBytes,0.0F); // New private time slot, after GPU completion.
#endif
    put(solver,0x548,0U);put(solver,0x540,static_cast<void*>(nullptr));put(solver,0x54C,0U);
    put(solver,0x550,0U);put(solver,0x558,static_cast<void*>(nullptr));put(solver,0x53C,0U);
    p4_release_presentation(solver);
    original(container); // No state/buffer use after destruction.
}
P4_API void p4_destroy_solver(void* solver, P4Unary original) {
    auto& s = state(solver);
    if(s.depth) p4_fail_closed();
    drain(s);
    if(s.context) {
        must(cuCtxPushCurrent_v2(s.context)); release_handles(s);
        void* popped{}; must(cuCtxPopCurrent_v2(&popped)); if(popped != s.context) p4_fail_closed();
    }
#ifdef P4_TIME_ACCUMULATOR
    put(solver,P4SolverBytes,0.0F);
#endif
    s = {};
    p4_release_presentation(solver);
    original(solver);
}


