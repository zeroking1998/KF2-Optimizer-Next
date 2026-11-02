#include "overlap.hpp"
#include "presentation.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <new>
#include <utility>

namespace {
constexpr unsigned P3Commit = 0x80000002U;
constexpr unsigned P3Middle = 0x80000001U;
constexpr unsigned P3Late = 0x80000000U;
constexpr std::size_t SyncCallRva = 0x83DFA5;
constexpr std::size_t SyncFunctionRva = 0x1BD8B0;
constexpr std::size_t RigidIatRva = 0x1FB2DB0;
constexpr std::size_t RigidDelayThunkRva = 0x139213C;
constexpr std::size_t RigidReadReturnRvaA = 0x4F141A;
constexpr std::size_t RigidReadReturnRvaB = 0x962434;
constexpr unsigned KfGameTimestamp = 0x65B94562U;
constexpr unsigned KfGameImageSize = 0x2481000U;

template<class T> T field(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof value);
    return value;
}
struct Quaternion { float x, y, z, w; };
struct PresentationState {
    void* allocation{};
    std::size_t allocation_bytes{};
    int capacity{}, rigid_count{}, samples{};
    int* rigid_ids{};
    float *rotation_previous{}, *rotation_current{}, *rotation_view{};
    float *translation_previous{}, *translation_current{}, *translation_view{};
    Quaternion *quaternion_previous{}, *quaternion_current{};
    const float *view_rotations{}, *view_translations{};
    void* solver{};
    void* bounds_lower{};
    void* bounds_upper{};
    P4BoundsCallback bounds_original{};
    int bounds_memory{};
    bool bounds_pending{};
};

P4Synchronize g_synchronize{};
P4RigidData g_rigid{};
INIT_ONCE g_install_once = INIT_ONCE_STATIC_INIT;
thread_local PresentationState* g_active_view{};
#ifndef P4_CPU_MODEL
void* g_relay{};
unsigned char* g_image_base{};
#endif

PresentationState*& presentation_slot(void* solver) {
    return *reinterpret_cast<PresentationState**>(static_cast<char*>(solver) + P4PresentationOffset);
}

bool add_bytes(std::size_t& total, std::size_t count, std::size_t width) {
    if(count > (std::numeric_limits<std::size_t>::max)() / width) return false;
    const auto bytes = count * width;
    if(total > (std::numeric_limits<std::size_t>::max)() - bytes) return false;
    total += bytes;
    return true;
}

template<class T> T* take(char*& cursor, std::size_t count) {
    auto* result = reinterpret_cast<T*>(cursor);
    cursor += sizeof(T) * count;
    return result;
}

PresentationState* allocate_state(void* solver, int capacity) {
    if(capacity < 0 || capacity > 0x1000000) return nullptr;
    const auto count = static_cast<std::size_t>(capacity);
    std::size_t bytes = sizeof(PresentationState);
    constexpr std::size_t widths[]{sizeof(int), sizeof(float) * 27U,
                                   sizeof(float) * 9U, sizeof(Quaternion) * 2U};
    for(const auto width : widths)
        if(!add_bytes(bytes, count, width)) return nullptr;
    void* memory = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if(!memory) return nullptr;
    auto* state = new(memory) PresentationState{};
    state->allocation = memory;
    state->allocation_bytes = bytes;
    state->capacity = capacity;
    state->solver = solver;
    char* cursor = static_cast<char*>(memory) + sizeof(PresentationState);
    state->rigid_ids = take<int>(cursor, count);
    state->rotation_previous = take<float>(cursor, count * 9U);
    state->rotation_current = take<float>(cursor, count * 9U);
    state->rotation_view = take<float>(cursor, count * 9U);
    state->translation_previous = take<float>(cursor, count * 3U);
    state->translation_current = take<float>(cursor, count * 3U);
    state->translation_view = take<float>(cursor, count * 3U);
    state->quaternion_previous = take<Quaternion>(cursor, count);
    state->quaternion_current = take<Quaternion>(cursor, count);
    if(static_cast<std::size_t>(cursor - static_cast<char*>(memory)) != bytes) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return nullptr;
    }
    presentation_slot(solver) = state;
    return state;
}

bool valid_state(void* solver, PresentationState* state) {
    if(!state) return false;
    MEMORY_BASIC_INFORMATION region{};
    return VirtualQuery(state,&region,sizeof region) &&
           region.State==MEM_COMMIT && region.AllocationBase==state &&
           region.Type==MEM_PRIVATE && state->allocation==state &&
           state->solver==solver;
}

float inverse_length(float x, float y, float z, float w) {
    const float square = x*x + y*y + z*z + w*w;
    return square > 1.0e-12F && std::isfinite(square) ? 1.0F / std::sqrt(square) : 0.0F;
}

Quaternion matrix_to_quaternion(const float* m) {
    Quaternion q{};
    const float trace = m[0] + m[4] + m[8];
    if(trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q.w = 0.25F * s; q.x = (m[7] - m[5]) / s; q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s;
    } else if(m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0F + m[0] - m[4] - m[8]) * 2.0F;
        q.w = (m[7] - m[5]) / s; q.x = 0.25F * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s;
    } else if(m[4] > m[8]) {
        const float s = std::sqrt(1.0F + m[4] - m[0] - m[8]) * 2.0F;
        q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25F * s; q.z = (m[5] + m[7]) / s;
    } else {
        const float s = std::sqrt(1.0F + m[8] - m[0] - m[4]) * 2.0F;
        q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25F * s;
    }
    const float scale = inverse_length(q.x, q.y, q.z, q.w);
    if(scale == 0.0F) return {0.0F, 0.0F, 0.0F, 1.0F};
    q.x *= scale; q.y *= scale; q.z *= scale; q.w *= scale;
    return q;
}

Quaternion nlerp(Quaternion a, Quaternion b, float alpha) {
    if(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w < 0.0F) {
        b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w;
    }
    Quaternion q{a.x + (b.x-a.x)*alpha, a.y + (b.y-a.y)*alpha,
                 a.z + (b.z-a.z)*alpha, a.w + (b.w-a.w)*alpha};
    const float scale = inverse_length(q.x, q.y, q.z, q.w);
    if(scale == 0.0F) return a;
    q.x*=scale; q.y*=scale; q.z*=scale; q.w*=scale;
    return q;
}

void quaternion_to_matrix(Quaternion q, float* m) {
    const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    m[0]=1.0F-2.0F*(yy+zz); m[1]=2.0F*(xy-wz); m[2]=2.0F*(xz+wy);
    m[3]=2.0F*(xy+wz); m[4]=1.0F-2.0F*(xx+zz); m[5]=2.0F*(yz-wx);
    m[6]=2.0F*(xz-wy); m[7]=2.0F*(yz+wx); m[8]=1.0F-2.0F*(xx+yy);
}

int rigid_ids(void* container, int capacity, const int** ids) {
    const auto begin = reinterpret_cast<std::uintptr_t>(field<const int*>(container, 0xE8));
    const auto end = reinterpret_cast<std::uintptr_t>(field<const int*>(container, 0xF0));
    *ids = reinterpret_cast<const int*>(begin);
    if(!begin && !end) return 0;
    if(!begin || !end || end < begin || (end - begin) % sizeof(int)) return -1;
    const auto count = (end - begin) / sizeof(int);
    return count <= static_cast<std::size_t>(capacity) ? static_cast<int>(count) : -1;
}

bool same_topology(PresentationState& state, int rigid, const int* ids) {
    return state.samples && state.rigid_count == rigid &&
           !std::memcmp(state.rigid_ids,ids,static_cast<std::size_t>(rigid)*sizeof(int));
}

void copy_rigid_snapshot(PresentationState& state, const float* rotations, const float* translations, bool shift) {
    const auto rigid = static_cast<std::size_t>(state.rigid_count);
    if(!rigid) return;
    if(shift) {
        std::swap(state.rotation_previous,state.rotation_current);
        std::swap(state.translation_previous,state.translation_current);
        std::swap(state.quaternion_previous,state.quaternion_current);
    }
    std::memcpy(state.rotation_current, rotations, rigid*9U*sizeof(float));
    std::memcpy(state.translation_current, translations, rigid*3U*sizeof(float));
    for(std::size_t i=0; i<rigid; ++i) state.quaternion_current[i] = matrix_to_quaternion(rotations+i*9U);
}

void interpolate(PresentationState& state, float alpha) {
    for(int i=0; i<state.rigid_count; ++i) {
        quaternion_to_matrix(nlerp(state.quaternion_previous[i],state.quaternion_current[i],alpha),state.rotation_view+i*9);
        for(int axis=0; axis<3; ++axis) {
            const auto at=i*3+axis;
            state.translation_view[at]=state.translation_previous[at]+(state.translation_current[at]-state.translation_previous[at])*alpha;
        }
    }
}

PresentationState* prepare_view(void* instance) {
    if(!g_rigid || !instance) return nullptr;
    void* container=field<void*>(instance,0x8); void* solver=field<void*>(instance,0x10);
    if(!container || !solver || field<void*>(container,8)!=solver) return nullptr;
    auto*& slot=presentation_slot(solver);
    const auto reset=[&slot] { if(slot) slot->samples=0; };
    if(field<unsigned short>(container,0x1D8)) { reset(); return nullptr; }
    const int capacity=field<int>(solver,0x1A0);
    const unsigned marker=field<unsigned>(solver,0x53C);
    if(capacity<=0 || capacity>0x1000000 ||
       (marker!=P3Commit && marker!=P3Middle && marker!=P3Late)) { reset(); return nullptr; }
    if(marker==P3Commit) {
        const int* ids{};
        const int rigids=rigid_ids(container,capacity,&ids);
        if(rigids<=0 || !ids) { reset(); return nullptr; }
        if(slot && slot->capacity!=capacity) p4_release_presentation(solver);
        auto* state=slot ? slot : allocate_state(solver,capacity);
        if(!state) return nullptr;
        const bool shift=same_topology(*state,rigids,ids);
        if(!shift) {
            state->rigid_count=rigids;
            std::memcpy(state->rigid_ids,ids,static_cast<std::size_t>(rigids)*sizeof(int));
        }
        float *rotations{}, *translations{};
        g_rigid(container,&rotations,&translations);
        if(!rotations || !translations) { state->samples=0; return nullptr; }
        copy_rigid_snapshot(*state,rotations,translations,shift);
        state->samples=shift ? 2 : 1;
        if(!shift) return nullptr;
        state->view_rotations=state->rotation_previous; state->view_translations=state->translation_previous;
        return state;
    }
    auto* state=slot;
    if(!state) return nullptr;
    if(state->samples<2) return nullptr;
    const float alpha=marker==P3Middle ? 1.0F/3.0F : 2.0F/3.0F;
    interpolate(*state,alpha);
    state->view_rotations=state->rotation_view; state->view_translations=state->translation_view;
    return state;
}

#ifndef P4_CPU_MODEL
void* relay_near(unsigned char* base, std::size_t image_size, const void* target) {
    constexpr std::uintptr_t granularity=0x10000U, limit=0x70000000U;
    auto start=(reinterpret_cast<std::uintptr_t>(base)+image_size+granularity-1U)&~(granularity-1U);
    for(std::uintptr_t distance=0; distance<limit; distance+=granularity) {
        auto* memory=VirtualAlloc(reinterpret_cast<void*>(start+distance),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!memory) continue;
        unsigned char code[]{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
        const auto address=reinterpret_cast<std::uint64_t>(target);
        std::memcpy(code+2,&address,sizeof address); std::memcpy(memory,code,sizeof code);
        DWORD old{};
        if(VirtualProtect(memory,0x1000,PAGE_EXECUTE_READ,&old)) {
            FlushInstructionCache(GetCurrentProcess(),memory,sizeof code);
            return memory;
        }
        VirtualFree(memory,0,MEM_RELEASE);
    }
    return nullptr;
}

bool protect_write(void* address, const void* bytes, std::size_t size) {
    DWORD old{};
    if(!VirtualProtect(address,size,PAGE_EXECUTE_READWRITE,&old)) return false;
    std::memcpy(address,bytes,size);
    FlushInstructionCache(GetCurrentProcess(),address,size);
    DWORD ignored{}; return VirtualProtect(address,size,old,&ignored)!=0;
}

BOOL CALLBACK install_once(PINIT_ONCE, PVOID, PVOID*) {
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* extension=GetModuleHandleW(L"flexExtRelease_x64.dll");
    if(!base) return TRUE;
    if(!extension) return FALSE;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return TRUE;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->FileHeader.TimeDateStamp!=KfGameTimestamp || nt->OptionalHeader.SizeOfImage!=KfGameImageSize) return TRUE;
    auto original_rigid=reinterpret_cast<P4RigidData>(GetProcAddress(extension,"flexExtGetRigidData"));
    auto** iat=reinterpret_cast<P4RigidData*>(base+RigidIatRva);
    const auto delay_rigid=reinterpret_cast<P4RigidData>(base+RigidDelayThunkRva);
    const auto previous_rigid=*iat;
    const unsigned char expected_call[]{0xE8,0x06,0xF9,0x97,0xFF};
    const unsigned char expected_sync[]{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20};
    if(!original_rigid || (previous_rigid!=original_rigid && previous_rigid!=delay_rigid) ||
       std::memcmp(base+SyncCallRva,expected_call,sizeof expected_call) ||
       std::memcmp(base+SyncFunctionRva,expected_sync,sizeof expected_sync)) return TRUE;
    auto target=reinterpret_cast<std::intptr_t>(&p4_present_sync);
    const auto next=reinterpret_cast<std::intptr_t>(base+SyncCallRva+5);
    auto distance=target-next;
    if(distance<(std::numeric_limits<std::int32_t>::min)() || distance>(std::numeric_limits<std::int32_t>::max)()) {
        g_relay=relay_near(base,nt->OptionalHeader.SizeOfImage,reinterpret_cast<void*>(target));
        if(!g_relay) return TRUE;
        target=reinterpret_cast<std::intptr_t>(g_relay); distance=target-next;
    }
    if(distance<(std::numeric_limits<std::int32_t>::min)() || distance>(std::numeric_limits<std::int32_t>::max)()) return TRUE;
    unsigned char replacement[5]{0xE8}; const auto displacement=static_cast<std::int32_t>(distance);
    std::memcpy(replacement+1,&displacement,sizeof displacement);
    g_synchronize=reinterpret_cast<P4Synchronize>(base+SyncFunctionRva);
    g_rigid=original_rigid; g_image_base=base;
    DWORD old{};
    if(!VirtualProtect(iat,sizeof *iat,PAGE_READWRITE,&old)) {
        g_synchronize=nullptr; g_rigid=nullptr; g_image_base=nullptr;
        if(g_relay) { VirtualFree(g_relay,0,MEM_RELEASE); g_relay=nullptr; }
        return TRUE;
    }
    *iat=&p4_present_rigid;
    DWORD ignored{}; VirtualProtect(iat,sizeof *iat,old,&ignored);
    if(!protect_write(base+SyncCallRva,replacement,sizeof replacement)) {
        if(VirtualProtect(iat,sizeof *iat,PAGE_READWRITE,&old)) {
            *iat=previous_rigid;
            const bool restored=VirtualProtect(iat,sizeof *iat,old,&ignored)!=0;
            if(restored) {
                g_synchronize=nullptr; g_rigid=nullptr; g_image_base=nullptr;
                if(g_relay) { VirtualFree(g_relay,0,MEM_RELEASE); g_relay=nullptr; }
            }
        }
    }
    return TRUE;
}
#endif
}

void p4_install_presentation() {
#ifndef P4_CPU_MODEL
    InitOnceExecuteOnce(&g_install_once,install_once,nullptr,nullptr);
#endif
}

bool p4_defer_bounds(void* solver, void* lower, void* upper, int memory,
                     P4BoundsCallback original) {
    if(!solver || !lower || !upper || !original) return false;
    auto*& state=presentation_slot(solver);
    if(!state) state=allocate_state(solver,0);
    if(!state || state->bounds_pending) return false;
    state->bounds_lower=lower;
    state->bounds_upper=upper;
    state->bounds_original=original;
    state->bounds_memory=memory;
    state->bounds_pending=true;
    return true;
}

bool p4_finish_bounds(void* solver) {
    auto* state=solver ? presentation_slot(solver) : nullptr;
    if(!state || !state->bounds_pending || !state->bounds_original)
        return false;
    const auto original=state->bounds_original;
    auto* lower=state->bounds_lower;
    auto* upper=state->bounds_upper;
    const int memory=state->bounds_memory;
    state->bounds_lower=nullptr;
    state->bounds_upper=nullptr;
    state->bounds_original=nullptr;
    state->bounds_memory=0;
    state->bounds_pending=false;
    original(solver,lower,upper,memory);
    return true;
}

void p4_release_presentation(void* solver) {
    if(!solver) return;
    auto*& state=presentation_slot(solver);
    if(!state) return;
    if(!valid_state(solver,state)) return;
    void* memory=state->allocation;
    state=nullptr;
    VirtualFree(memory,0,MEM_RELEASE);
}

P4_PRESENT_API void p4_present_sync(void* instance, float delta_seconds) {
    auto synchronize=g_synchronize;
    if(!synchronize || !instance || g_active_view) { if(synchronize) synchronize(instance,delta_seconds); return; }
    auto* state=prepare_view(instance);
    if(!state) { synchronize(instance,delta_seconds); return; }
    g_active_view=state;
    __try { synchronize(instance,delta_seconds); }
    __finally { g_active_view=nullptr; }
}

P4_PRESENT_API void p4_present_rigid(void* container, float** rotations, float** translations) {
    auto original=g_rigid;
    if(!original) { if(rotations)*rotations=nullptr; if(translations)*translations=nullptr; return; }
    original(container,rotations,translations);
    auto* state=g_active_view;
    if(!state || !container || field<void*>(container,8)!=state->solver || !rotations || !translations) return;
#ifndef P4_CPU_MODEL
    const auto return_address=static_cast<unsigned char*>(_ReturnAddress());
    if(!g_image_base || (return_address!=g_image_base+RigidReadReturnRvaA &&
                         return_address!=g_image_base+RigidReadReturnRvaB)) return;
#endif
    *rotations=const_cast<float*>(state->view_rotations);
    *translations=const_cast<float*>(state->view_translations);
}

#ifdef P4_CPU_MODEL
void p4_test_bind_presentation(P4Synchronize synchronize, P4RigidData rigid) {
    g_synchronize=synchronize; g_rigid=rigid;
}
#endif
