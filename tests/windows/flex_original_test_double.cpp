#include <Windows.h>

namespace {
volatile LONG last_substeps{};
volatile LONGLONG update_calls{};
volatile LONGLONG active_count_calls{};
volatile LONGLONG create_calls{};
volatile LONGLONG destroy_calls{};
volatile LONGLONG fence_set_calls{};
volatile LONGLONG fence_wait_calls{};
volatile LONGLONG particle_upload_calls{};
volatile LONGLONG particle_download_calls{};
volatile LONGLONG phase_upload_calls{};
volatile LONGLONG phase_download_calls{};
volatile LONGLONG velocity_upload_calls{};
volatile LONGLONG velocity_download_calls{};
volatile LONGLONG bounds_calls{};
volatile LONGLONG params_calls{};
volatile LONG last_transfer_elements{};
volatile LONG last_transfer_memory{};
volatile LONG last_capacity{};
int solver_tokens[4]{};
}

extern "C" __declspec(dllexport) void* flexCreateSolver(int capacity) noexcept {
    const auto call = InterlockedIncrement64(&create_calls);
    InterlockedExchange(&last_capacity, capacity);
    return call >= 1 && call <= 4 ? &solver_tokens[call - 1] : nullptr;
}

extern "C" __declspec(dllexport) void flexDestroySolver(void*) noexcept {
    InterlockedIncrement64(&destroy_calls);
}

extern "C" __declspec(dllexport) int flexGetActiveCount(void*) noexcept {
    InterlockedIncrement64(&active_count_calls);
    return 37;
}

extern "C" __declspec(dllexport) void flexSetFence() noexcept {
    InterlockedIncrement64(&fence_set_calls);
}

extern "C" __declspec(dllexport) void flexWaitFence() noexcept {
    InterlockedIncrement64(&fence_wait_calls);
}

extern "C" __declspec(dllexport) void flexGetBounds(
    void*, float* lower, float* upper) noexcept {
    InterlockedIncrement64(&bounds_calls);
    if (lower) { lower[0] = -1.0F; lower[1] = -2.0F; lower[2] = -3.0F; }
    if (upper) { upper[0] = 1.0F; upper[1] = 2.0F; upper[2] = 3.0F; }
}

extern "C" __declspec(dllexport) void flexSetParams(
    void*, const void*) noexcept {
    InterlockedIncrement64(&params_calls);
}

void record_transfer(volatile LONGLONG* calls, int elements, int memory) noexcept {
    InterlockedIncrement64(calls);
    InterlockedExchange(&last_transfer_elements, elements);
    InterlockedExchange(&last_transfer_memory, memory);
}

extern "C" __declspec(dllexport) void flexGetParticles(
    void*, void*, int elements, int memory) noexcept {
    record_transfer(&particle_download_calls, elements, memory);
}
extern "C" __declspec(dllexport) void flexGetPhases(
    void*, void*, int elements, int memory) noexcept {
    record_transfer(&phase_download_calls, elements, memory);
}
extern "C" __declspec(dllexport) void flexGetVelocities(
    void*, void*, int elements, int memory) noexcept {
    record_transfer(&velocity_download_calls, elements, memory);
}
extern "C" __declspec(dllexport) void flexSetParticles(
    void*, const void*, int elements, int memory) noexcept {
    record_transfer(&particle_upload_calls, elements, memory);
}
extern "C" __declspec(dllexport) void flexSetPhases(
    void*, const void*, int elements, int memory) noexcept {
    record_transfer(&phase_upload_calls, elements, memory);
}
extern "C" __declspec(dllexport) void flexSetVelocities(
    void*, const void*, int elements, int memory) noexcept {
    record_transfer(&velocity_upload_calls, elements, memory);
}

extern "C" __declspec(dllexport) void flexUpdateSolver(
    void*, float, int substeps, void*) noexcept {
    InterlockedExchange(&last_substeps, substeps);
    InterlockedIncrement64(&update_calls);
}

extern "C" __declspec(dllexport) int flexTestLastSubsteps() noexcept {
    return InterlockedCompareExchange(&last_substeps, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestUpdateCalls() noexcept {
    return InterlockedCompareExchange64(&update_calls, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestActiveCountCalls() noexcept {
    return InterlockedCompareExchange64(&active_count_calls, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestCreateCalls() noexcept {
    return InterlockedCompareExchange64(&create_calls, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestDestroyCalls() noexcept {
    return InterlockedCompareExchange64(&destroy_calls, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestFenceSetCalls() noexcept {
    return InterlockedCompareExchange64(&fence_set_calls, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestFenceWaitCalls() noexcept {
    return InterlockedCompareExchange64(&fence_wait_calls, 0, 0);
}

extern "C" __declspec(dllexport) int flexTestLastCapacity() noexcept {
    return InterlockedCompareExchange(&last_capacity, 0, 0);
}

extern "C" __declspec(dllexport) long long flexTestParticleUploadCalls() noexcept {
    return InterlockedCompareExchange64(&particle_upload_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestParticleDownloadCalls() noexcept {
    return InterlockedCompareExchange64(&particle_download_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestPhaseUploadCalls() noexcept {
    return InterlockedCompareExchange64(&phase_upload_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestPhaseDownloadCalls() noexcept {
    return InterlockedCompareExchange64(&phase_download_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestVelocityUploadCalls() noexcept {
    return InterlockedCompareExchange64(&velocity_upload_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestVelocityDownloadCalls() noexcept {
    return InterlockedCompareExchange64(&velocity_download_calls, 0, 0);
}
extern "C" __declspec(dllexport) int flexTestLastTransferElements() noexcept {
    return InterlockedCompareExchange(&last_transfer_elements, 0, 0);
}
extern "C" __declspec(dllexport) int flexTestLastTransferMemory() noexcept {
    return InterlockedCompareExchange(&last_transfer_memory, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestBoundsCalls() noexcept {
    return InterlockedCompareExchange64(&bounds_calls, 0, 0);
}
extern "C" __declspec(dllexport) long long flexTestParamsCalls() noexcept {
    return InterlockedCompareExchange64(&params_calls, 0, 0);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) noexcept {
    return TRUE;
}
