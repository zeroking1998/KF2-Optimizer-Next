#include "render_interop.hpp"
#include "render_contract.hpp"

#include <windows.h>
#include <d3d11.h>
#include <xmmintrin.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace {
using CuDevicePointer = std::uint64_t;
using UploadVertices = void(*)(void*, const void*, const void*, const int*);
using ReleaseRhi = void(*)(void*);
using DestroyProxy = void*(*)(void*, unsigned);

extern "C" __declspec(dllimport) int cuCtxPushCurrent_v2(void*);
extern "C" __declspec(dllimport) int cuCtxPopCurrent_v2(void**);
extern "C" __declspec(dllimport) int cuGraphicsD3D11RegisterResource(
    void**, ID3D11Resource*, unsigned);
extern "C" __declspec(dllimport) int cuGraphicsMapResources(unsigned, void**, void*);
extern "C" __declspec(dllimport) int cuGraphicsResourceGetMappedPointer_v2(
    CuDevicePointer*, std::size_t*, void*);
extern "C" __declspec(dllimport) int cuGraphicsUnmapResources(unsigned, void**, void*);
extern "C" __declspec(dllimport) int cuGraphicsUnregisterResource(void*);
extern "C" __declspec(dllimport) int cuModuleLoadDataEx(
    void**, const void*, unsigned, void*, void*);
extern "C" __declspec(dllimport) int cuModuleGetFunction(void**, void*, const char*);
extern "C" __declspec(dllimport) int cuModuleUnload(void*);
extern "C" __declspec(dllimport) int cuLaunchKernel(
    void*, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
    unsigned, void*, void**, void**);
extern "C" __declspec(dllimport) int cuMemAlloc_v2(CuDevicePointer*, std::size_t);
extern "C" __declspec(dllimport) int cuMemFree_v2(CuDevicePointer);
extern "C" __declspec(dllimport) int cuMemcpyHtoD_v2(
    CuDevicePointer, const void*, std::size_t);

extern "C" __declspec(dllimport) int cudaStreamCreateWithFlags(void**, unsigned);
extern "C" __declspec(dllimport) int cudaStreamDestroy(void*);
extern "C" __declspec(dllimport) int cudaStreamSynchronize_ptsz(void*);
extern "C" __declspec(dllimport) int cudaStreamWaitEvent_ptsz(
    void*, void*, unsigned);
extern "C" __declspec(dllimport) int cudaEventCreateWithFlags(void**, unsigned);
extern "C" __declspec(dllimport) int cudaEventDestroy(void*);
extern "C" __declspec(dllimport) int cudaEventRecord_ptsz(void*, void*);
extern "C" __declspec(dllimport) int cudaEventSynchronize(void*);
extern "C" __declspec(dllimport) int cudaEventQuery(void*);
extern "C" __declspec(dllimport) int cudaMemcpyAsync_ptsz(
    void*, const void*, std::size_t, int, void*);

constexpr std::size_t UploadCallRva = 0x4F3F61;
constexpr std::size_t UploadFunctionRva = 0x4F2A20;
constexpr std::size_t VertexBufferVtableRva = 0x17DC9E0;
constexpr std::size_t VertexBufferReleaseSlot = 0x20;
constexpr std::size_t VertexBufferReleaseRva = 0x15AF30;
constexpr std::size_t ProxyVtableRva = 0x17E2620;
constexpr std::size_t ProxyDestroyRva = 0x4DA610;
constexpr unsigned KfGameTimestamp = 0x65B94562U;
constexpr unsigned KfGameImageSize = 0x2481000U;
constexpr int CudaMemoryDeviceToDevice = 3;
constexpr int CudaNotReady = 34;
constexpr unsigned ThreadsPerBlock = 256;

constexpr char PackVerticesPtx[] = R"ptx(
.version 4.3
.target sm_50
.address_size 64

.visible .entry kf2_pack_vertices(
    .param .u64 output,
    .param .u64 positions,
    .param .u64 normals,
    .param .u64 indices,
    .param .u32 count)
{
    .reg .pred %p<4>;
    .reg .b32 %r<16>;
    .reg .b64 %rd<16>;
    .reg .f32 %f<7>;

    ld.param.u64 %rd1, [output];
    ld.param.u64 %rd2, [positions];
    ld.param.u64 %rd3, [normals];
    ld.param.u64 %rd4, [indices];
    ld.param.u32 %r1, [count];
    mov.u32 %r2, %ctaid.x;
    mov.u32 %r3, %ntid.x;
    mov.u32 %r4, %tid.x;
    mad.lo.s32 %r5, %r2, %r3, %r4;
    setp.ge.s32 %p1, %r5, %r1;
    @%p1 bra done;

    mul.wide.s32 %rd5, %r5, 4;
    add.s64 %rd6, %rd4, %rd5;
    ld.global.s32 %r6, [%rd6];
    mul.wide.s32 %rd7, %r6, 16;
    add.s64 %rd8, %rd2, %rd7;
    mul.wide.s32 %rd9, %r6, 16;
    add.s64 %rd10, %rd3, %rd9;
    mul.wide.s32 %rd11, %r5, 16;
    add.s64 %rd12, %rd1, %rd11;

    ld.global.u32 %r10, [%rd8];
    ld.global.u32 %r11, [%rd8+4];
    ld.global.u32 %r12, [%rd8+8];
    st.global.u32 [%rd12], %r10;
    st.global.u32 [%rd12+4], %r11;
    st.global.u32 [%rd12+8], %r12;

    ld.global.u32 %r10, [%rd10];
    xor.b32 %r10, %r10, 0x80000000;
    mov.b32 %f4, %r10;
    add.rn.f32 %f4, %f4, 0f3F800000;
    mul.rn.f32 %f4, %f4, 0f42FF0000;
    cvt.rzi.s32.f32 %r7, %f4;
    max.s32 %r7, %r7, 0;
    min.s32 %r7, %r7, 255;
    setp.neu.f32 %p2, %f4, %f4;
    setp.ge.f32 %p3, %f4, 0f4F000000;
    or.pred %p2, %p2, %p3;
    selp.s32 %r7, 0, %r7, %p2;

    ld.global.u32 %r10, [%rd10+4];
    xor.b32 %r10, %r10, 0x80000000;
    mov.b32 %f5, %r10;
    add.rn.f32 %f5, %f5, 0f3F800000;
    mul.rn.f32 %f5, %f5, 0f42FF0000;
    cvt.rzi.s32.f32 %r8, %f5;
    max.s32 %r8, %r8, 0;
    min.s32 %r8, %r8, 255;
    setp.neu.f32 %p2, %f5, %f5;
    setp.ge.f32 %p3, %f5, 0f4F000000;
    or.pred %p2, %p2, %p3;
    selp.s32 %r8, 0, %r8, %p2;
    shl.b32 %r8, %r8, 8;
    or.b32 %r7, %r7, %r8;

    ld.global.u32 %r10, [%rd10+8];
    xor.b32 %r10, %r10, 0x80000000;
    mov.b32 %f6, %r10;
    add.rn.f32 %f6, %f6, 0f3F800000;
    mul.rn.f32 %f6, %f6, 0f42FF0000;
    cvt.rzi.s32.f32 %r9, %f6;
    max.s32 %r9, %r9, 0;
    min.s32 %r9, %r9, 255;
    setp.neu.f32 %p2, %f6, %f6;
    setp.ge.f32 %p3, %f6, 0f4F000000;
    or.pred %p2, %p2, %p3;
    selp.s32 %r9, 0, %r9, %p2;
    shl.b32 %r9, %r9, 16;
    or.b32 %r7, %r7, %r9;
    or.b32 %r7, %r7, 0xff000000;
    st.global.u32 [%rd12+12], %r7;
done:
    ret;
}
)ptx";

struct SnapshotSlot {
    CuDevicePointer positions{};
    CuDevicePointer normals{};
    void* completion{};
    std::atomic<bool> busy{};
    std::atomic<int> prefix{};
};

struct ResourceState {
    ResourceState* next{};
    void* resource{};
    ID3D11Buffer* buffer{};
    const int* host_indices{};
    int count{};
    void* instance{};
    const int* particle_indices{};
    int particle_count{};
    int maximum_particle{};
    CuDevicePointer device_indices{};
    void* graphics{};
    std::atomic<bool> usable{};
};

struct SolverRenderState {
    SolverRenderState* next{};
    ResourceState* resources{};
    void* solver{};
    void* context{};
    void* render_stream{};
    void* module{};
    void* pack{};
    CuDevicePointer snapshot_allocation{};
    int capacity{};
    std::atomic<bool> usable{};
    SnapshotSlot slots[2]{};
    std::atomic<int> published{-1};
    std::atomic<std::uint64_t> current_generation{};
    std::atomic<std::uint64_t> published_generation{};
};

SRWLOCK g_lock = SRWLOCK_INIT;
SolverRenderState* g_solvers{};
INIT_ONCE g_install_once = INIT_ONCE_STATIC_INIT;
UploadVertices g_original_upload{};
ReleaseRhi g_original_release{};
DestroyProxy g_original_proxy_destroy{};
void* g_upload_relay{};
std::atomic<bool> g_installed{};

template<class T>
T field(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof value);
    return value;
}

[[noreturn]] void fail_closed() {
    RaiseFailFastException(nullptr, nullptr, 0);
    __assume(0);
}

void must_cleanup(int error) {
    if(error) fail_closed();
}

bool multiply(std::size_t count, std::size_t width, std::size_t& result) {
    if(count > (std::numeric_limits<std::size_t>::max)() / width) return false;
    result = count * width;
    return true;
}

SolverRenderState* find_solver(void* solver) {
    for(auto* state = g_solvers; state; state = state->next)
        if(state->solver == solver) return state;
    return nullptr;
}

ResourceState* find_resource(SolverRenderState& state, void* resource) {
    for(auto* entry = state.resources; entry; entry = entry->next)
        if(entry->resource == resource) return entry;
    return nullptr;
}

void destroy_resource(ResourceState* resource) {
    if(resource->graphics)
        must_cleanup(cuGraphicsUnregisterResource(resource->graphics));
    if(resource->device_indices)
        must_cleanup(cuMemFree_v2(resource->device_indices));
    delete resource;
}

void destroy_solver_contents(SolverRenderState& state) {
    if(state.render_stream)
        must_cleanup(cudaStreamSynchronize_ptsz(state.render_stream));
    for(auto& slot : state.slots)
        if(slot.completion) must_cleanup(cudaEventSynchronize(slot.completion));
    while(state.resources) {
        auto* resource = state.resources;
        state.resources = resource->next;
        destroy_resource(resource);
    }
    if(state.snapshot_allocation)
        must_cleanup(cuMemFree_v2(state.snapshot_allocation));
    if(state.module)
        must_cleanup(cuModuleUnload(state.module));
    for(auto& slot : state.slots)
        if(slot.completion) must_cleanup(cudaEventDestroy(slot.completion));
    if(state.render_stream)
        must_cleanup(cudaStreamDestroy(state.render_stream));
    state.resources = nullptr;
    state.snapshot_allocation = 0;
    state.module = nullptr;
    state.pack = nullptr;
    state.render_stream = nullptr;
    for(auto& slot : state.slots) {
        slot.positions = 0;
        slot.normals = 0;
        slot.completion = nullptr;
        slot.busy.store(false, std::memory_order_relaxed);
        slot.prefix.store(0, std::memory_order_relaxed);
    }
}

bool initialize_solver(SolverRenderState& state) {
    std::size_t position_bytes{}, normal_bytes{};
    const auto capacity = static_cast<std::size_t>(state.capacity);
    if(!multiply(capacity, 16U, position_bytes) ||
       !multiply(capacity, 16U, normal_bytes) ||
       position_bytes > (std::numeric_limits<std::size_t>::max)() - normal_bytes)
        return false;
    const auto slot_bytes = position_bytes + normal_bytes;
    if(slot_bytes > (std::numeric_limits<std::size_t>::max)() / 2U) return false;

    if(cudaStreamCreateWithFlags(&state.render_stream, 1U)) return false;
    for(auto& slot : state.slots)
        if(cudaEventCreateWithFlags(&slot.completion, 2U)) return false;
    if(cuMemAlloc_v2(&state.snapshot_allocation, slot_bytes * 2U)) return false;
    for(unsigned index = 0; index < 2; ++index) {
        const auto begin = state.snapshot_allocation + slot_bytes * index;
        state.slots[index].positions = begin;
        state.slots[index].normals = begin + position_bytes;
    }
    if(cuModuleLoadDataEx(&state.module, PackVerticesPtx, 0, nullptr, nullptr)) return false;
    if(cuModuleGetFunction(&state.pack, state.module, "kf2_pack_vertices")) return false;
    return true;
}

SolverRenderState* ensure_solver_locked(void* solver, void* context, int capacity,
                                        std::uint64_t generation) {
    auto* state = find_solver(solver);
    if(!state) {
        state = new(std::nothrow) SolverRenderState{};
        if(state) {
            state->solver = solver;
            state->context = context;
            state->capacity = capacity;
            state->current_generation.store(generation, std::memory_order_relaxed);
            state->usable.store(initialize_solver(*state), std::memory_order_relaxed);
            if(!state->usable.load(std::memory_order_relaxed))
                destroy_solver_contents(*state);
            state->next = g_solvers;
            g_solvers = state;
        }
    }
    return state;
}

void release_resource(void* resource) {
    if(!resource) return;
    AcquireSRWLockExclusive(&g_lock);
    SolverRenderState* owner{};
    ResourceState** link{};
    for(auto* state = g_solvers; state && !owner; state = state->next) {
        for(auto** entry = &state->resources; *entry; entry = &(*entry)->next) {
            if((*entry)->resource == resource) {
                owner = state;
                link = entry;
                break;
            }
        }
    }
    if(owner && link) {
        auto* entry = *link;
        *link = entry->next;
        must_cleanup(cuCtxPushCurrent_v2(owner->context));
        must_cleanup(cudaStreamSynchronize_ptsz(owner->render_stream));
        destroy_resource(entry);
        void* popped{};
        must_cleanup(cuCtxPopCurrent_v2(&popped));
        if(popped != owner->context) fail_closed();
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool same_resource(const ResourceState& state, ID3D11Buffer* buffer,
                   const int* indices, int count, void* instance,
                   const int* particle_indices, int particle_count) {
    return state.buffer == buffer && state.host_indices == indices &&
           state.count == count && state.instance == instance &&
           state.particle_indices == particle_indices &&
           state.particle_count == particle_count;
}

void ensure_resource(void* solver, void* resource, ID3D11Buffer* buffer,
                     const int* indices, int count, void* instance,
                     const int* particle_indices, int particle_count) {
    AcquireSRWLockExclusive(&g_lock);
    auto* state = find_solver(solver);
    if(!state || !state->usable.load(std::memory_order_acquire) ||
       count <= 0 || count > state->capacity) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    auto** link = &state->resources;
    while(*link && (*link)->resource != resource) link = &(*link)->next;
    if(*link && same_resource(**link, buffer, indices, count, instance,
                              particle_indices, particle_count)) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    must_cleanup(cuCtxPushCurrent_v2(state->context));
    if(*link) {
        auto* stale = *link;
        *link = stale->next;
        must_cleanup(cudaStreamSynchronize_ptsz(state->render_stream));
        destroy_resource(stale);
    }

    auto* created = new(std::nothrow) ResourceState{};
    if(created) {
        created->resource = resource;
        created->buffer = buffer;
        created->host_indices = indices;
        created->count = count;
        created->instance = instance;
        created->particle_indices = particle_indices;
        created->particle_count = particle_count;
        // The render asset stores component-local indices. The solver snapshot
        // is global, so cache the instance-owned mapping once per binding.
        std::unique_ptr<int[]> composed{
            new(std::nothrow) int[static_cast<std::size_t>(count)]};
        bool valid = buffer && composed &&
            kf2::flex::optimized::compose_particle_indices(
                indices, count, particle_indices, particle_count,
                state->capacity, composed.get(), created->maximum_particle);
        std::size_t vertex_bytes{}, index_bytes{};
        D3D11_BUFFER_DESC description{};
        if(valid) {
            buffer->GetDesc(&description);
            valid = multiply(static_cast<std::size_t>(count), 16U, vertex_bytes) &&
                    multiply(static_cast<std::size_t>(count), sizeof(int), index_bytes) &&
                    description.ByteWidth >= vertex_bytes &&
                    (description.BindFlags & D3D11_BIND_VERTEX_BUFFER) != 0;
        }
        if(valid && !cuGraphicsD3D11RegisterResource(
                         &created->graphics, buffer, 0U) &&
           !cuMemAlloc_v2(&created->device_indices, index_bytes) &&
           !cuMemcpyHtoD_v2(created->device_indices, composed.get(), index_bytes))
            created->usable.store(true, std::memory_order_relaxed);
        created->next = state->resources;
        state->resources = created;
    }
    void* popped{};
    must_cleanup(cuCtxPopCurrent_v2(&popped));
    if(popped != state->context) fail_closed();
    ReleaseSRWLockExclusive(&g_lock);
}

void fallback_upload(void* resource, const void* positions,
                     const void* normals, const int* indices) {
    const auto original = g_original_upload;
    if(original) original(resource, positions, normals, indices);
}

bool safe_mxcsr() {
    constexpr unsigned ExceptionMasks = 0x1F80U;
    const auto value = _mm_getcsr();
    return (value & _MM_ROUND_MASK) == _MM_ROUND_NEAREST &&
           (value & ExceptionMasks) == ExceptionMasks;
}

extern "C" void p4_render_upload(void* resource, const void* positions,
                                  const void* normals, const int* indices) {
    if(!resource || !positions || !normals || !safe_mxcsr()) {
        fallback_upload(resource, positions, normals, indices);
        return;
    }

    auto* proxy = static_cast<unsigned char*>(resource) - 0x16C;
    void* component = field<void*>(proxy, 0x4BC);
    void* scene = component ? field<void*>(component, 0x2F4) : nullptr;
    void* container = scene ? field<void*>(scene, 8) : nullptr;
    void* solver = container ? field<void*>(container, 8) : nullptr;
    void* asset_owner = component ? field<void*>(component, 0x250) : nullptr;
    void* asset = asset_owner ? field<void*>(asset_owner, 0x88) : nullptr;
    void* instance = component ? field<void*>(component, 0x2EC) : nullptr;
    const int* particle_indices = instance ? field<const int*>(instance, 0) : nullptr;
    const int particle_count = instance ? field<int>(instance, 8) : 0;
    void* handle = field<void*>(resource, 0x24);
    auto* buffer = handle ? field<ID3D11Buffer*>(handle, 0) : nullptr;
    const int count = field<int>(resource, 0x2C);
    // KF2 edits attached particles on the CPU before gathering the render
    // arrays. Those components must retain the original upload path.
    if(!solver || !buffer || !indices || !asset || !instance ||
       field<unsigned char>(asset, 0x60) != 0 ||
       indices != field<const int*>(asset, 0x108) ||
       field<int>(component, 0x334) != 0 ||
       positions != field<const void*>(component, 0x30C) ||
       normals != field<const void*>(component, 0x31C)) {
        fallback_upload(resource, positions, normals, indices);
        return;
    }

    for(unsigned attempt = 0; attempt < 2; ++attempt) {
        AcquireSRWLockShared(&g_lock);
        auto* state = find_solver(solver);
        auto* cached = state ? find_resource(*state, resource) : nullptr;
        if(!state || !state->usable.load(std::memory_order_acquire) || !cached ||
           !same_resource(*cached, buffer, indices, count, instance,
                          particle_indices, particle_count)) {
            ReleaseSRWLockShared(&g_lock);
            if(attempt == 0) {
                ensure_resource(solver, resource, buffer, indices, count,
                                instance, particle_indices, particle_count);
                continue;
            }
            fallback_upload(resource, positions, normals, indices);
            return;
        }
        if(!cached->usable.load(std::memory_order_acquire)) {
            ReleaseSRWLockShared(&g_lock);
            fallback_upload(resource, positions, normals, indices);
            return;
        }

        const auto published_generation =
            state->published_generation.load(std::memory_order_acquire);
        const auto current_generation =
            state->current_generation.load(std::memory_order_acquire);
        const int slot_index = state->published.load(std::memory_order_acquire);
        if(!published_generation || published_generation != current_generation ||
           slot_index < 0 || slot_index > 1) {
            ReleaseSRWLockShared(&g_lock);
            fallback_upload(resource, positions, normals, indices);
            return;
        }
        auto& slot = state->slots[slot_index];
        if(cached->maximum_particle >=
           slot.prefix.load(std::memory_order_acquire)) {
            ReleaseSRWLockShared(&g_lock);
            fallback_upload(resource, positions, normals, indices);
            return;
        }
        const bool previously_busy = slot.busy.exchange(true, std::memory_order_acq_rel);
        if(state->published_generation.load(std::memory_order_acquire) !=
               published_generation ||
           state->published.load(std::memory_order_acquire) != slot_index) {
            if(!previously_busy) slot.busy.store(false, std::memory_order_release);
            ReleaseSRWLockShared(&g_lock);
            fallback_upload(resource, positions, normals, indices);
            return;
        }

        bool launched = false;
        bool mapped = false;
        int error = cuCtxPushCurrent_v2(state->context);
        const bool pushed = !error;
        if(pushed)
            error = cudaStreamWaitEvent_ptsz(
                state->render_stream, slot.completion, 0U);
        if(!error) {
            error = cuGraphicsMapResources(1U, &cached->graphics,
                                           state->render_stream);
            mapped = !error;
        }
        CuDevicePointer output{};
        std::size_t mapped_bytes{};
        if(!error)
            error = cuGraphicsResourceGetMappedPointer_v2(
                &output, &mapped_bytes, cached->graphics);
        std::size_t required{};
        if(!error && (!multiply(static_cast<std::size_t>(count), 16U, required) ||
                      mapped_bytes < required))
            error = 1;
        if(!error) {
            auto output_argument = output;
            auto position_argument = slot.positions;
            auto normal_argument = slot.normals;
            auto index_argument = cached->device_indices;
            auto count_argument = static_cast<unsigned>(count);
            void* arguments[]{&output_argument, &position_argument, &normal_argument,
                              &index_argument, &count_argument};
            const auto blocks = (count_argument + ThreadsPerBlock - 1U) /
                                ThreadsPerBlock;
            error = cuLaunchKernel(state->pack, blocks, 1U, 1U,
                                   ThreadsPerBlock, 1U, 1U, 0U,
                                   state->render_stream, arguments, nullptr);
            launched = !error;
        }
        if(mapped) {
            const int unmap_error = cuGraphicsUnmapResources(
                1U, &cached->graphics, state->render_stream);
            if(unmap_error) fail_closed();
        }
        if(launched) {
            if(cudaEventRecord_ptsz(slot.completion, state->render_stream)) {
                must_cleanup(cudaStreamSynchronize_ptsz(state->render_stream));
                slot.busy.store(false, std::memory_order_release);
            }
        } else if(!previously_busy) {
            slot.busy.store(false, std::memory_order_release);
        }
        void* popped{};
        if(pushed) {
            if(!error) error = cuCtxPopCurrent_v2(&popped);
            else must_cleanup(cuCtxPopCurrent_v2(&popped));
            if(popped != state->context) fail_closed();
        }
        if(error) cached->usable.store(false, std::memory_order_release);
        ReleaseSRWLockShared(&g_lock);
        if(!error) return;
        fallback_upload(resource, positions, normals, indices);
        return;
    }
    fallback_upload(resource, positions, normals, indices);
}

extern "C" void p4_release_rhi(void* resource) {
    release_resource(resource);
    if(g_original_release) g_original_release(resource);
}

extern "C" void* p4_destroy_proxy(void* proxy, unsigned flags) {
    if(proxy) release_resource(static_cast<unsigned char*>(proxy) + 0x16C);
    return g_original_proxy_destroy ? g_original_proxy_destroy(proxy, flags) : proxy;
}

void* relay_near(unsigned char* base, std::size_t image_size, const void* target) {
    constexpr std::uintptr_t Granularity = 0x10000U;
    constexpr std::uintptr_t Limit = 0x70000000U;
    auto start = (reinterpret_cast<std::uintptr_t>(base) + image_size +
                  Granularity - 1U) & ~(Granularity - 1U);
    for(std::uintptr_t distance = 0; distance < Limit; distance += Granularity) {
        auto* memory = VirtualAlloc(reinterpret_cast<void*>(start + distance),
                                    0x1000, MEM_RESERVE | MEM_COMMIT,
                                    PAGE_READWRITE);
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

bool write_memory(void* destination, const void* source, std::size_t size) {
    DWORD old{};
    if(!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old))
        return false;
    std::memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored{};
    if(!VirtualProtect(destination, size, old, &ignored)) fail_closed();
    return true;
}

BOOL CALLBACK install_once(PINIT_ONCE, PVOID, PVOID*) {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!base) return TRUE;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic != IMAGE_DOS_SIGNATURE) return TRUE;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if(nt->Signature != IMAGE_NT_SIGNATURE ||
       nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
       nt->FileHeader.TimeDateStamp != KfGameTimestamp ||
       nt->OptionalHeader.SizeOfImage != KfGameImageSize)
        return TRUE;

    constexpr unsigned char ExpectedCall[]{0xE8, 0xBA, 0xEA, 0xFF, 0xFF};
    auto** release_slot = reinterpret_cast<ReleaseRhi*>(
        base + VertexBufferVtableRva + VertexBufferReleaseSlot);
    auto** destroy_slot = reinterpret_cast<DestroyProxy*>(base + ProxyVtableRva);
    const auto expected_release = reinterpret_cast<ReleaseRhi>(
        base + VertexBufferReleaseRva);
    const auto expected_destroy = reinterpret_cast<DestroyProxy>(
        base + ProxyDestroyRva);
    if(std::memcmp(base + UploadCallRva, ExpectedCall, sizeof ExpectedCall) ||
       *release_slot != expected_release || *destroy_slot != expected_destroy)
        return TRUE;

    auto* relay = relay_near(base, nt->OptionalHeader.SizeOfImage,
                             reinterpret_cast<const void*>(&p4_render_upload));
    if(!relay) return TRUE;
    const auto next = reinterpret_cast<std::intptr_t>(base + UploadCallRva + 5U);
    const auto distance = reinterpret_cast<std::intptr_t>(relay) - next;
    if(distance < (std::numeric_limits<std::int32_t>::min)() ||
       distance > (std::numeric_limits<std::int32_t>::max)()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return TRUE;
    }
    unsigned char replacement[5]{0xE8};
    const auto displacement = static_cast<std::int32_t>(distance);
    std::memcpy(replacement + 1, &displacement, sizeof displacement);

    g_original_upload = reinterpret_cast<UploadVertices>(base + UploadFunctionRva);
    g_original_release = expected_release;
    g_original_proxy_destroy = expected_destroy;
    const ReleaseRhi release_hook = &p4_release_rhi;
    const DestroyProxy destroy_hook = &p4_destroy_proxy;
    bool release_patched = write_memory(release_slot, &release_hook,
                                        sizeof release_hook);
    bool destroy_patched = release_patched &&
        write_memory(destroy_slot, &destroy_hook, sizeof destroy_hook);
    bool call_patched = destroy_patched &&
        write_memory(base + UploadCallRva, replacement, sizeof replacement);
    if(!call_patched) {
        bool restored = true;
        if(destroy_patched)
            restored = write_memory(destroy_slot, &expected_destroy,
                                    sizeof expected_destroy) && restored;
        if(release_patched)
            restored = write_memory(release_slot, &expected_release,
                                    sizeof expected_release) && restored;
        if(!restored) fail_closed();
        g_original_upload = nullptr;
        g_original_release = nullptr;
        g_original_proxy_destroy = nullptr;
        VirtualFree(relay, 0, MEM_RELEASE);
        return TRUE;
    }
    g_upload_relay = relay;
    g_installed.store(true, std::memory_order_release);
    return TRUE;
}
} // namespace

void p4_install_render_interop() {
    InitOnceExecuteOnce(&g_install_once, install_once, nullptr, nullptr);
}

void p4_render_generation(void* solver, std::uint64_t generation) {
    AcquireSRWLockShared(&g_lock);
    if(auto* state = find_solver(solver))
        state->current_generation.store(generation, std::memory_order_release);
    ReleaseSRWLockShared(&g_lock);
}

int p4_render_snapshot(void* solver, void* context, std::uint64_t generation,
                       int capacity, int prefix,
                       const void* positions, const void* normals) {
    if(!g_installed.load(std::memory_order_acquire) || !solver || !context ||
       !positions || !normals || capacity <= 0 ||
       capacity > 0x1000000 || prefix <= 0 || prefix > capacity)
        return -1;
    // Solver-state discovery, first-use initialization and snapshot submission
    // share one non-blocking handoff. Never wait for a concurrent render upload;
    // this generation can safely use KF2's original upload instead.
    if(!TryAcquireSRWLockExclusive(&g_lock)) return -1;
    auto* state = ensure_solver_locked(solver, context, capacity, generation);
    if(!state || !state->usable.load(std::memory_order_acquire) ||
       state->context != context ||
       state->capacity != capacity) {
        ReleaseSRWLockExclusive(&g_lock);
        return -1;
    }
    bool has_consumer = false;
    for(auto* resource = state->resources; resource; resource = resource->next)
        if(resource->usable.load(std::memory_order_acquire)) {
            has_consumer = true;
            break;
        }
    if(!has_consumer) {
        ReleaseSRWLockExclusive(&g_lock);
        return -1;
    }
    state->current_generation.store(generation, std::memory_order_release);
    const int published = state->published.load(std::memory_order_acquire);
    int candidate = published == 0 ? 1 : 0;
    auto& slot = state->slots[candidate];
    if(slot.busy.load(std::memory_order_acquire)) {
        const int query = cudaEventQuery(slot.completion);
        if(query == 0) slot.busy.store(false, std::memory_order_release);
        else {
            if(query != CudaNotReady)
                state->usable.store(false, std::memory_order_release);
            ReleaseSRWLockExclusive(&g_lock);
            return -1;
        }
    }

    std::size_t position_bytes{}, normal_bytes{};
    const auto count = static_cast<std::size_t>(prefix);
    if(!multiply(count, 16U, position_bytes) ||
       !multiply(count, 16U, normal_bytes)) {
        ReleaseSRWLockExclusive(&g_lock);
        return -1;
    }
    // This hook runs on the verified solver thread. Its per-thread default
    // stream is ordered after this solve and before the next one, so the
    // source lifetime needs no host wait or additional synchronization state.
    int error = cudaMemcpyAsync_ptsz(
        reinterpret_cast<void*>(slot.positions), positions, position_bytes,
        CudaMemoryDeviceToDevice, nullptr);
    if(!error)
        error = cudaMemcpyAsync_ptsz(
            reinterpret_cast<void*>(slot.normals), normals, normal_bytes,
            CudaMemoryDeviceToDevice, nullptr);
    if(!error)
        error = cudaEventRecord_ptsz(slot.completion, nullptr);
    if(error) {
        must_cleanup(cudaStreamSynchronize_ptsz(nullptr));
        state->usable.store(false, std::memory_order_release);
    } else slot.prefix.store(prefix, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    return error ? -1 : candidate;
}

void p4_render_publish(void* solver, std::uint64_t generation, int slot) {
    if(slot < 0 || slot > 1) return;
    AcquireSRWLockShared(&g_lock);
    if(auto* state = find_solver(solver); state &&
       state->usable.load(std::memory_order_acquire) &&
       state->current_generation.load(std::memory_order_acquire) == generation) {
        state->published.store(slot, std::memory_order_relaxed);
        state->published_generation.store(generation, std::memory_order_release);
    }
    ReleaseSRWLockShared(&g_lock);
}

void p4_release_render_interop(void* solver) {
    if(!solver) return;
    AcquireSRWLockExclusive(&g_lock);
    auto** link = &g_solvers;
    while(*link && (*link)->solver != solver) link = &(*link)->next;
    if(*link) {
        auto* state = *link;
        *link = state->next;
        must_cleanup(cuCtxPushCurrent_v2(state->context));
        destroy_solver_contents(*state);
        void* popped{};
        must_cleanup(cuCtxPopCurrent_v2(&popped));
        if(popped != state->context) fail_closed();
        delete state;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
