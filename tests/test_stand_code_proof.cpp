#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
using DWORD=std::uint32_t;
static DWORD GetCurrentThreadId(){return DWORD(::syscall(SYS_gettid));}
#define __declspec(x)
#define __cdecl
#endif
#include "stand_api.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <thread>

using namespace NorthlightStream;
namespace C=ClientCodeObservation;
namespace A=SnapshotArchive;
namespace I=StandInputs;
static constexpr std::uintptr_t firstAddress=0x4f6650,secondAddress=0x7bfd1c;
using Block=std::array<unsigned char,16>;
static auto makeProof(const ClientImage::View& view,const Block& first,const Block& second){
    C::Capture capture;assert(capture.begin(0));
    capture.record(view,firstAddress,first.data(),first.size(),true);
    capture.record(view,secondAddress,second.data(),second.size(),true);
    capture.record(view,firstAddress,first.data(),first.size(),true);
    std::vector<unsigned char> encoded(C::MaxEncodedBytes);std::size_t written=0;C::Error error;
    assert(capture.complete()&&C::encode(capture,encoded.data(),encoded.size(),written,error));encoded.resize(written);return encoded;
}
static auto packet(){
    GameSnapshot snapshot;snapshot.triggerKind=1;snapshot.triggerDraw=1;
    unsigned value=10;assert(snapshot.record(0x18000000,&value,4,true)&&snapshot.record(0x18000004,nullptr,4,false));
    std::vector<unsigned char> bytes(A::MaxBytes);std::size_t written=0;A::Error error;
    assert(A::encode(snapshot,bytes.data(),bytes.size(),written,error));bytes.resize(written);return bytes;
}
static void failedObservation(const ClientImage::View& view,const Block& first,bool changed){
    C::Capture capture;assert(capture.begin(0));auto value=first;if(changed)value[0]^=1;
    capture.record(view,firstAddress,value.data(),value.size(),changed);
    std::array<unsigned char,C::MaxEncodedBytes> bytes{};std::size_t written=99;C::Error error;
    assert(!capture.complete()&&!C::encode(capture,bytes.data(),bytes.size(),written,error)&&!written);
}
int main(int argc,char** argv){
    assert(argc==3);const std::string mode=argv[2];
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<unsigned char> image{std::istreambuf_iterator<char>(input),{}};
    ClientImage::View view;ClientImage::Error imageError;assert(view.open(image.data(),image.size(),imageError));
    Block first{},second{};assert(view.readStatic(firstAddress,first.data(),first.size())&&view.readStatic(secondAddress,second.data(),second.size()));
    auto encoded=makeProof(view,first,second);const auto original=encoded;
    if(mode=="before-open")assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size())));
    assert(NorthlightStandOpen(image.data(),unsigned(image.size())));
    auto& state=NorthlightStandApi::state();auto& session=*state.inputs;auto& context=*session.context();
    std::uint64_t status[8]{};assert(NorthlightStandStatus(status,8)&&status[0]==1&&status[1]==1&&!status[2]&&!status[3]&&!status[7]);
    if(mode=="raw-compatibility"){
        Block output{};assert(!context.codeProofInstalled()&&context.read(nullptr,firstAddress,output.data(),output.size())&&output==first);
        assert(context.read(nullptr,secondAddress,output.data(),output.size())&&output==second&&context.valid());
    }else if(mode=="late-static"){
        Block output{};assert(context.read(nullptr,firstAddress,output.data(),output.size())&&output==first);
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid()&&!context.codeProofInstalled());
    }else if(mode=="late-dynamic"||mode=="late-failed-read"){
        GameSnapshot runtime;unsigned value=10,output=99;
        assert(runtime.record(0x18000000,&value,4,true)&&runtime.record(0x18000004,nullptr,4,false));
        if(mode=="late-dynamic")assert(context.read(&runtime,0x18000000,&output,4)&&output==10);
        else assert(!context.read(&runtime,0x18000004,&output,4)&&output==99&&context.recordedFailures==1);
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="late-snapshot"){
        const auto bytes=packet();assert(NorthlightStandSnapshot(bytes.data(),unsigned(bytes.size()),1,1));
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="session-bound"){
        GameSnapshot snapshot;snapshot.triggerKind=1;snapshot.triggerDraw=1;unsigned value=10;
        assert(snapshot.record(0x18000000,&value,4,true)&&session.bind(snapshot,1,1));
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="missing-scope"){
        state.scope.reset();assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="late-api"){
        assert(!NorthlightStandApi::beforeApi()&&state.apiStarted);
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="late-worker"){
        auto pin=I::shareForPlayback();assert(pin);
        std::thread worker([pin]{I::PlaybackScope playback(pin);assert(playback.entered());});worker.join();
        assert(context.workerStarts==1&&context.workerStops==1);
        assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
    }else if(mode=="failed-observation"||mode=="changed-observation"){
        failedObservation(view,first,mode=="changed-observation");
        assert(!NorthlightStandCodeProof(nullptr,0)&&!context.valid());
    }else if(mode=="crc"||mode=="correspondence"||mode=="truncated"||mode=="magic"||mode=="null"||mode=="oversize"){
        const void* data=encoded.data();unsigned size=unsigned(encoded.size());
        if(mode=="crc")encoded.back()^=1;
        else if(mode=="correspondence"){
            encoded[C::HeaderBytes+2*C::RecordBytes]^=1;A::put32(encoded.data()+encoded.size()-4,A::checksum(encoded.data(),encoded.size()-4));
        }else if(mode=="truncated")--size;
        else if(mode=="magic")encoded[0]^=1;
        else if(mode=="null")data=nullptr;
        else size=unsigned(C::MaxEncodedBytes+1);
        assert(!NorthlightStandCodeProof(data,size)&&!context.valid()&&!context.codeProofInstalled());
    }else{
        assert(mode=="valid"||mode=="before-open"||mode=="unknown"||mode=="subspan"||mode=="repeat"||mode=="wrong-thread"||mode=="lifetime");
        if(mode=="wrong-thread"){
            std::thread foreign([&]{assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size())));});foreign.join();assert(context.valid());
        }
        assert(NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&context.codeProofInstalled()&&context.valid());
        if(mode=="repeat")assert(!NorthlightStandCodeProof(encoded.data(),unsigned(encoded.size()))&&!context.valid());
        else if(mode=="unknown"||mode=="subspan"){
            Block output;output.fill(0xa5);const auto before=output;
            assert(!context.read(nullptr,mode=="subspan"?firstAddress:firstAddress+1,output.data(),mode=="subspan"?8:16));
            assert(output==before&&!context.valid()&&context.missingReads==1);
        }else if(mode=="lifetime"){
            auto pin=I::shareForPlayback();const auto weak=std::weak_ptr<I::Context>(pin);assert(pin);
            std::atomic<bool> entered{false},released{false};
            std::thread worker([pin,&entered,&released,first,second]{
                I::PlaybackScope playback(pin);assert(playback.entered());entered.store(true);
                while(!released.load())std::this_thread::yield();
                Block output{};assert(I::read(firstAddress,output.data(),output.size())&&output==first);
                assert(I::read(secondAddress,output.data(),output.size())&&output==second&&pin->valid());
            });
            while(!entered.load())std::this_thread::yield();state.scope.reset();state.inputs.reset();pin.reset();
            std::fill(image.begin(),image.end(),0);std::fill(encoded.begin(),encoded.end(),0);assert(!weak.expired());released.store(true);worker.join();assert(weak.expired());
        }else{
            std::fill(image.begin(),image.end(),0);std::fill(encoded.begin(),encoded.end(),0);
            Block output{};for(unsigned i=0;i<3;++i){assert(context.read(nullptr,firstAddress,output.data(),output.size())&&output==first);assert(context.read(nullptr,secondAddress,output.data(),output.size())&&output==second);}
            assert(context.valid()&&NorthlightStandStatus(status,8)&&status[0]==1&&status[1]==1&&status[3]==6&&!status[6]&&!status[7]);
            const auto bytes=packet();assert(NorthlightStandSnapshot(bytes.data(),unsigned(bytes.size()),1,1)&&NorthlightStandApi::beforeApi());
            assert(original.size()>C::HeaderBytes);
        }
    }
    state.scope.reset();state.inputs.reset();assert(!I::active&&!I::playbackContext);
    std::printf("PASS stand code proof %s: exact observed static keys, owned PE/proof, strict pre-use admission; authored raw compatibility only; no game/GPU/FPS claim\n",mode.c_str());
}
