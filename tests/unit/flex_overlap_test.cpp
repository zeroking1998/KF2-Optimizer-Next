#include "overlap.hpp"
#include "presentation.hpp"

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
    int capacity;
    std::array<std::byte,P4ExtendedSolverBytes> solver{};
    std::array<std::byte,0x1E0> container{};
    std::array<std::vector<std::byte>,4> buffers;
    std::vector<int> active_ids;
    std::array<int,4> observed{};
    int native_error{};

    explicit PullFixture(std::vector<int> ids, int maximum = 16)
        : capacity(maximum), active_ids(std::move(ids)) {
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
int routed_copies{};
int context_queries{};
void* current_context{};
void* pushed_context{};
void* solver_context{};
bool simulate_getter_guards{};
int phase_copy_result{-1};
int first_copy_result{-1};
int second_copy_result{-1};
void copy_phase(void*) {
    current->observe();
    phase_copy_result=p4_copy(current->buffers[2].data(),
        read<void*>(current->solver.data()+0x20),
        static_cast<std::size_t>(current->observed[2])*4,2,
        current->solver.data(),1);
}
void copy_pull_payloads(void*) {
    current->observe();
    constexpr std::size_t sources[] = {0,0x10,0x20,0x48,0x128,0x130};
    constexpr std::size_t sizes[] = {16,12,4,16,36,12};
    for(unsigned index=0;index<6;++index) {
        void* saved=current_context;
        if(simulate_getter_guards) current_context=solver_context;
        const int result=p4_copy(current->buffers[index%4].data(),
            read<void*>(current->solver.data()+sources[index]),sizes[index],2,
            current->solver.data(),1);
        if(simulate_getter_guards) current_context=saved;
        if(index==2) phase_copy_result=result;
    }
}
void copy_then_native_error(void*) {
    current->observe();
    first_copy_result=p4_copy(current->buffers[0].data(),
        read<void*>(current->solver.data()),16,2,
        current->solver.data(),1);
    current->native_error=1;
    second_copy_result=p4_copy(current->buffers[1].data(),
        read<void*>(current->solver.data()+0x10),12,2,
        current->solver.data(),1);
}
void arm_pull(PullFixture& fixture, void* context) {
    auto& overlap=*reinterpret_cast<P4State*>(
        fixture.solver.data()+P4StateOffset);
    overlap.initialized=1;
    overlap.thread=GetCurrentThreadId();
    overlap.context=context;
    overlap.stream=reinterpret_cast<void*>(2);
    overlap.ready_event=reinterpret_cast<void*>(3);
    overlap.flags=P4Ready;
    overlap.generation=7;
    overlap.ready_generation=7;
    overlap.native_error=&fixture.native_error;
    write(fixture.solver.data()+0x53C,0x80000002U);
    write(fixture.solver.data()+0x138,2);
    constexpr std::size_t sources[] = {0,0x10,0x20,0x48,0x128,0x130};
    for(unsigned index=0;index<6;++index)
        write(fixture.solver.data()+sources[index],
            reinterpret_cast<void*>(0x1000ULL*(index+1)));
}
bool abnormal_pull_restores(PullFixture* fixture) {
    bool caught=false;
    __try { p4_pull(fixture->container.data(),raise); }
    __except(EXCEPTION_EXECUTE_HANDLER) { caught=true; }
    return caught && fixture->restored();
}

struct PresentationFixture {
    std::array<std::byte,P4ExtendedSolverBytes> solver{};
    std::array<std::byte,0x200> container{};
    std::array<std::byte,0x20> instance{};
    std::array<int,2> ids{3,7};
    std::array<float,18> rotations{};
    std::array<float,6> translations{};
    std::array<float,18> observed_rotations{};
    std::array<float,6> observed_translations{};

    PresentationFixture() {
        write(instance.data()+0x8,static_cast<void*>(container.data()));
        write(instance.data()+0x10,static_cast<void*>(solver.data()));
        write(container.data()+0x8,static_cast<void*>(solver.data()));
        write(container.data()+0xE8,ids.data());
        write(container.data()+0xF0,ids.data()+ids.size());
        write(solver.data()+0x1A0,4);
        set_sample(0.0F,1.0F,0.0F);
    }

    void set_sample(float translation, float cosine, float sine) {
        rotations.fill(0.0F);
        translations.fill(translation);
        for(std::size_t rigid=0;rigid<ids.size();++rigid) {
            auto* matrix=rotations.data()+rigid*9U;
            matrix[0]=cosine; matrix[1]=-sine;
            matrix[3]=sine; matrix[4]=cosine; matrix[8]=1.0F;
        }
    }

    void present(unsigned marker) {
        write(solver.data()+0x53C,marker);
        p4_present_sync(instance.data(),1.0F/60.0F);
    }
};

PresentationFixture* presentation{};
void rigid_data(void*,float** rotations,float** translations) {
    *rotations=presentation->rotations.data();
    *translations=presentation->translations.data();
}
void synchronize(void*,float) {
    float *rotations{}, *translations{};
    p4_present_rigid(presentation->container.data(),&rotations,&translations);
    std::memcpy(presentation->observed_rotations.data(),rotations,
                presentation->observed_rotations.size()*sizeof(float));
    std::memcpy(presentation->observed_translations.data(),translations,
                presentation->observed_translations.size()*sizeof(float));
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
extern "C" int cudaMemcpyAsync_ptsz(void*,const void*,std::size_t,int,void* stream) {
    ++async_copies;
    if(stream) ++routed_copies;
    return 0;
}
extern "C" int cuCtxGetCurrent(void** result) {
    ++context_queries;
    *result=current_context;
    return 0;
}
extern "C" int cuCtxPushCurrent_v2(void* context) {
    pushed_context=current_context;
    current_context=context;
    return 0;
}
extern "C" int cuCtxPopCurrent_v2(void** result) {
    *result=current_context;
    current_context=pushed_context;
    return 0;
}
extern "C" int cuCtxSynchronize() { return 0; }
void p4_install_render_interop() {}
void p4_render_generation(void*,std::uint64_t) {}
int p4_render_snapshot(void*,void*,std::uint64_t,int,int,
                       const void*,const void*) {
    return -1;
}
void p4_render_publish(void*,std::uint64_t,int) {}
void p4_release_render_interop(void*) {}

int main() {
    {
        constexpr unsigned commit=0x80000002U, middle=0x80000001U;
        PresentationFixture fixture; presentation=&fixture;
        p4_test_bind_presentation(synchronize,rigid_data);

        fixture.present(commit);
        CHECK(fixture.observed_translations[0]==0.0F);

        fixture.set_sample(9.0F,0.0F,1.0F);
        fixture.present(commit);
        CHECK(fixture.observed_translations[0]==0.0F);
        CHECK(fixture.observed_rotations[0]==1.0F);
        fixture.present(middle);
        CHECK(fixture.observed_translations[0]==3.0F);
        CHECK(fixture.observed_rotations[0]>0.8F &&
              fixture.observed_rotations[0]<0.95F);

        fixture.set_sample(18.0F,-1.0F,0.0F);
        fixture.present(commit);
        CHECK(fixture.observed_translations[0]==9.0F);
        CHECK(fixture.observed_rotations[0]==0.0F);

        fixture.ids[0]=11;
        fixture.set_sample(27.0F,1.0F,0.0F);
        fixture.present(commit);
        fixture.present(middle);
        CHECK(fixture.observed_translations[0]==27.0F);

        fixture.set_sample(36.0F,0.0F,-1.0F);
        fixture.present(commit);
        CHECK(fixture.observed_translations[0]==27.0F);
        CHECK(fixture.observed_rotations[0]==1.0F);
        p4_release_presentation(fixture.solver.data());
    }
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
        constexpr int capacity = 26746;
        std::vector<int> low_ids(512);
        for(int index = 0; index < 512; ++index) low_ids[index] = index;
        PullFixture fixture{std::move(low_ids),capacity}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{512,512,512,512}));
        CHECK(fixture.restored());
        const auto optimized_bytes =
            static_cast<long long>(fixture.observed[0]) * (48 + 44);
        const auto full_bytes =
            static_cast<long long>(fixture.capacity) * (48 + 48);
        CHECK(optimized_bytes == 47104);
        CHECK(full_bytes == 2567616);
    }
    {
        constexpr int capacity = 26746;
        std::vector<int> high_ids(512);
        for(int index = 0; index < 511; ++index) high_ids[index] = index;
        high_ids.back() = capacity-1;
        PullFixture fixture{std::move(high_ids),capacity}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed ==
               std::array<int,4>{capacity,capacity,capacity,capacity}));
        CHECK(fixture.restored());
    }
    {
        PullFixture fixture{{}}; current=&fixture;
        p4_pull(fixture.container.data(),observe);
        CHECK((fixture.observed == std::array<int,4>{1,1,1,1}));
        CHECK(fixture.restored());
    }
    for(unsigned short pending : {static_cast<unsigned short>(1),
                                  static_cast<unsigned short>(0x100)}) {
        PullFixture fixture{{}}; current=&fixture;
        write(fixture.container.data()+0x1D8,pending);
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
        async_copies=0;
        phase_copy_result=-1;
        p4_pull(fixture.container.data(),copy_phase);
        CHECK(phase_copy_result==0);
        CHECK(async_copies==0);
        CHECK(fixture.restored());

        // A direct phase read remains byte-for-byte on the native copy path.
        CHECK(p4_copy(fixture.buffers[2].data(),
            read<void*>(fixture.solver.data()+0x20),4,2,
            fixture.solver.data(),1)==0);
        CHECK(async_copies==1);
    }
    {
        // The exact Pull keeps an already-correct FleX context unchanged across
        // every getter. Validate it once instead of querying the driver again
        // for five routed copies and the render snapshot.
        PullFixture fixture{{0,3,7}}; current=&fixture;
        solver_context=reinterpret_cast<void*>(0x1132);
        current_context=solver_context;
        simulate_getter_guards=false;
        arm_pull(fixture,solver_context);
        async_copies=0;
        routed_copies=0;
        context_queries=0;
        phase_copy_result=-1;
        p4_pull(fixture.container.data(),copy_pull_payloads);
        CHECK(context_queries==1);
        CHECK(async_copies==5);
        CHECK(routed_copies==5);
        CHECK(phase_copy_result==0);
        CHECK(current_context==solver_context);
        CHECK(!(reinterpret_cast<P4State*>(fixture.solver.data()+
            P4StateOffset)->flags & P4PullEligible));
        CHECK(fixture.restored());
    }
    {
        // A caller with another current context keeps the original per-copy
        // validation; the synthetic guards model each stock FleX getter.
        PullFixture fixture{{0,3,7}}; current=&fixture;
        solver_context=reinterpret_cast<void*>(0x1132);
        current_context=reinterpret_cast<void*>(0x2244);
        simulate_getter_guards=true;
        arm_pull(fixture,solver_context);
        async_copies=0;
        routed_copies=0;
        context_queries=0;
        phase_copy_result=-1;
        p4_pull(fixture.container.data(),copy_pull_payloads);
        CHECK(context_queries==7);
        CHECK(async_copies==5);
        CHECK(routed_copies==5);
        CHECK(phase_copy_result==0);
        CHECK(current_context==reinterpret_cast<void*>(0x2244));
        CHECK(fixture.restored());
    }
    {
        // Pull-wide invariants are cached, but a native failure is still read
        // before every payload and immediately returns to the original stream.
        PullFixture fixture{{0,3,7}}; current=&fixture;
        solver_context=reinterpret_cast<void*>(0x1132);
        current_context=solver_context;
        arm_pull(fixture,solver_context);
        async_copies=0;
        routed_copies=0;
        first_copy_result=-1;
        second_copy_result=-1;
        p4_pull(fixture.container.data(),copy_then_native_error);
        CHECK(first_copy_result==0);
        CHECK(second_copy_result==0);
        CHECK(async_copies==2);
        CHECK(routed_copies==1);
        CHECK(!(reinterpret_cast<P4State*>(fixture.solver.data()+
            P4StateOffset)->flags & P4PullEligible));
        CHECK(fixture.restored());
    }
    return 0;
}
