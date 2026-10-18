#include "overlap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
#include <windows.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

template<class T>
void write(std::byte* target, T value) {
    std::memcpy(target,&value,sizeof value);
}

template<class T>
T read(const std::byte* source) {
    T value{};
    std::memcpy(&value,source,sizeof value);
    return value;
}

struct PullFixture {
    static constexpr int capacity = 16;
    std::array<std::byte,P4ExtendedSolverBytes> solver{};
    std::array<std::byte,0x1E0> container{};
    std::array<std::vector<std::byte>,4> buffers;
    std::vector<int> active_ids;
    std::array<int,4> observed{};

    explicit PullFixture(std::vector<int> ids) : active_ids(std::move(ids)) {
        write(container.data()+8,static_cast<void*>(solver.data()));
        write(container.data()+0x10,active_ids.data());
        write(solver.data()+0x1A0,capacity);
        write(solver.data()+0x198,static_cast<int>(active_ids.size()));
        constexpr std::size_t begin_offsets[] = {0x58,0x70,0x88,0xA0};
        constexpr std::size_t end_offsets[] = {0x60,0x78,0x90,0xA8};
        constexpr std::size_t strides[] = {16,12,4,16};
        for(unsigned i=0;i<4;++i) {
            buffers[i].resize(capacity*strides[i]);
            auto* begin=buffers[i].data();
            write(container.data()+begin_offsets[i],begin);
            write(container.data()+end_offsets[i],begin+buffers[i].size());
        }
    }

    void observe() {
        constexpr std::size_t begin_offsets[] = {0x58,0x70,0x88,0xA0};
        constexpr std::size_t end_offsets[] = {0x60,0x78,0x90,0xA8};
        constexpr std::size_t strides[] = {16,12,4,16};
        for(unsigned i=0;i<4;++i) {
            auto* begin=read<std::byte*>(container.data()+begin_offsets[i]);
            auto* end=read<std::byte*>(container.data()+end_offsets[i]);
            observed[i]=static_cast<int>((end-begin)/strides[i]);
        }
    }

    bool restored() const {
        constexpr std::size_t end_offsets[] = {0x60,0x78,0x90,0xA8};
        for(unsigned i=0;i<4;++i)
            if(read<std::byte*>(container.data()+end_offsets[i]) !=
               buffers[i].data()+buffers[i].size()) return false;
        return true;
    }
};

PullFixture* current{};
void observe(void*) { current->observe(); }
void raise(void*) { RaiseException(0xE0421132U,0,0,nullptr); }
int async_copies{};
int phase_copy_result{-1};
void copy_phase(void*) {
    current->observe();
    phase_copy_result=p4_copy(current->buffers[2].data(),
        read<void*>(current->solver.data()+0x20),
        static_cast<std::size_t>(current->observed[2])*4,2,
        current->solver.data(),1);
}
bool abnormal_pull_restores(PullFixture* fixture) {
    bool caught=false;
    __try { p4_pull(fixture->container.data(),raise); }
    __except(EXCEPTION_EXECUTE_HANDLER) { caught=true; }
    return caught && fixture->restored();
}

} // namespace

void p4_fail_closed() { std::terminate(); }
extern "C" int cudaStreamCreateWithFlags(void**,unsigned) { return 0; }
extern "C" int cudaStreamDestroy(void*) { return 0; }
extern "C" int cudaStreamSynchronize_ptsz(void*) { return 0; }
extern "C" int cudaStreamWaitEvent_ptsz(void*,void*,unsigned) { return 0; }
extern "C" int cudaEventCreateWithFlags(void**,unsigned) { return 0; }
extern "C" int cudaEventDestroy(void*) { return 0; }
extern "C" int cudaEventRecord_ptsz(void*,void*) { return 0; }
extern "C" int cudaEventSynchronize(void*) { return 0; }
extern "C" int cudaPeekAtLastError() { return 0; }
extern "C" int cudaMemcpy_ptds(void*,const void*,std::size_t,int) { return 0; }
extern "C" int cudaMemcpyAsync_ptsz(void*,const void*,std::size_t,int,void*) { ++async_copies; return 0; }
extern "C" int cuCtxGetCurrent(void**) { return 0; }
extern "C" int cuCtxPushCurrent_v2(void*) { return 0; }
extern "C" int cuCtxPopCurrent_v2(void**) { return 0; }
extern "C" int cuCtxSynchronize() { return 0; }
void p4_install_presentation() {}
void p4_release_presentation(void*) {}

int main() {
    {
        PullFixture fixture{{0,3,7}}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{8,8,8,8}));
        CHECK(fixture.restored());
    }
    {
        PullFixture fixture{{0,7,15}}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{16,16,16,16}));
        CHECK(fixture.restored());
    }
    {
        PullFixture fixture{{}}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{16,16,16,16}));
        CHECK(fixture.restored());
    }
    {
        PullFixture fixture{{0,16}}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{16,16,16,16}));
        CHECK(fixture.restored());
    }
    {
        PullFixture fixture{{0,3,7}}; current=&fixture;
        auto* short_end=fixture.buffers[3].data()+15*16;
        write(fixture.container.data()+0xA8,short_end);
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{16,16,16,15}));
        CHECK(read<std::byte*>(fixture.container.data()+0xA8)==short_end);
    }
    {
        PullFixture fixture{{0,3,7}}; current=&fixture;
        CHECK(abnormal_pull_restores(&fixture));
    }
    {
        PullFixture fixture{{0,3,7}}; current=&fixture;
        write(fixture.solver.data()+0x20,
            static_cast<void*>(fixture.buffers[2].data()));
        write(fixture.solver.data()+0x53C,0x80000002U);
        int native_error{};
        auto& overlap=*reinterpret_cast<P4State*>(
            fixture.solver.data()+P4StateOffset);
        overlap.initialized=1;
        overlap.thread=GetCurrentThreadId();
        overlap.flags=P4Ready;
        overlap.generation=1;
        overlap.ready_generation=1;
        overlap.native_error=&native_error;
        async_copies=0;
        phase_copy_result=-1;
        p4_pull(fixture.container.data(),copy_phase);
        CHECK(phase_copy_result==0);
        CHECK(async_copies==0);
        CHECK((overlap.flags&P4PullScope)==0);
        CHECK(fixture.restored());

        // A direct phase read remains byte-for-byte on the native copy path.
        CHECK(p4_copy(fixture.buffers[2].data(),
            read<void*>(fixture.solver.data()+0x20),4,2,
            fixture.solver.data(),1)==0);
        CHECK(async_copies==1);
    }
    return 0;
}
