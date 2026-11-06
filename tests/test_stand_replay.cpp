#define NORTHLIGHT_REPLAY_STAND 1
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main
#include <fstream>
#include <iterator>

namespace I=NorthlightStream::StandInputs;
static bool captureImport(GameSnapshot& destination,Trigger trigger,std::uint64_t draw){
    I::CaptureScope admission(unsigned(trigger),draw);CHECK(admission.entered());
    {ScopedRecording recording(destination);unsigned value=0;
        CHECK(I::read(0x18000000,&value,4));value=99;CHECK(!I::read(0x18000004,&value,4)&&value==99);
        destination.triggerKind=unsigned(trigger);destination.triggerDraw=draw;}
    return admission.commit(destination)&&destination.usable()&&I::currentContext()->valid();
}
static void fillImport(GameSnapshot& snapshot,std::uint64_t draw,unsigned value){
    snapshot.clear();snapshot.triggerKind=1;snapshot.triggerDraw=draw;
    CHECK(snapshot.record(0x18000000,&value,4,true)&&snapshot.record(0x18000004,nullptr,4,false));
}
struct ImportedTarget:TargetDevice {
    std::atomic<bool> entered{false},releaseFirst{false};std::vector<unsigned> seen;
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type,UINT first,UINT count)override{
        if(seen.empty()){
            entered.store(true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!releaseFirst.load()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
        }
        CHECK(!I::active&&I::playbackContext&&activeSnapshot&&activeSnapshot->mode==GameSnapshot::Mode::Playing);
        for(unsigned repeat=0;repeat<3;++repeat){
            unsigned value=0;CHECK(I::read(0x18000000,&value,4));seen.push_back(value);
            value=99;CHECK(!I::read(0x18000004,&value,4)&&value==99&&I::currentContext()->valid());
        }
        unsigned char proof[2]{};CHECK(I::read(0x4f6650,proof,sizeof proof)&&proof[0]==0x55&&proof[1]==0x8b);
        return TargetDevice::DrawPrimitive(type,first,count);
    }
};
int main(int argc,char** argv){
    CHECK(argc==3);const std::string mode=argv[2];
    std::ifstream file(argv[1],std::ios::binary);CHECK(file);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),{}};
    auto session=std::make_unique<I::Session>(bytes.data(),bytes.size());CHECK(session->valid());
    auto scope=std::make_unique<I::Scope>(*session);CHECK(scope->entered());
    std::weak_ptr<I::Context> weak=session->context();
    bytes.assign(bytes.size(),0);bytes.clear();bytes.shrink_to_fit(); // The worker must not borrow the caller buffer.
    GameSnapshot imported;fillImport(imported,1,10);CHECK(session->bind(imported,1,1));
    auto* target=new ImportedTarget;FakeD3D parent;const char* reason=nullptr;
    StreamDevice::Options options;options.capture=&captureImport;
    unsigned starts=0;
    options.threadStart=[&]{
        CHECK(I::currentContext()&&I::currentContext()->valid());
        if(!I::active){CHECK(I::playbackContext);++starts;}
    };
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferCount=2;pp.BackBufferFormat=22;
    if(mode=="unavailable"){
        session->reject();CHECK(!StreamDevice::make(target,&parent,&pp,std::move(options),&reason));
        CHECK(reason&&std::string(reason)=="thread"&&starts==0);target->Release();
    }else{
        CHECK(mode=="ordered"||mode=="retired-owner"||mode=="init-failure"||mode=="throw-start");
        if(mode=="init-failure")gKnobs.failSwapChain.store(true);
        if(mode=="throw-start")options.threadStart=[]{throw 1;};
        auto* stream=StreamDevice::make(target,&parent,&pp,std::move(options),&reason);
        if(mode=="throw-start"){
            CHECK(!stream&&reason&&std::string(reason)=="thread"&&!session->valid());target->Release();
        }else if(mode=="init-failure"){
            CHECK(!stream&&reason&&std::string(reason)=="init"&&starts==1&&!session->valid());
            gKnobs.failSwapChain.store(false);target->Release();
        }else{
            CHECK(stream&&starts==1);CHECK(session->bindAliases(nullptr,0,1,1));CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);
            stream->streamCore().q.publish();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!target->entered.load()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
            fillImport(imported,2,20);CHECK(session->bind(imported,2,1)&&session->bindAliases(nullptr,0,2,1));
            if(mode=="retired-owner"){scope.reset();session.reset();CHECK(!weak.expired());}
            target->releaseFirst.store(true);DWORD n=0;CHECK(stream->ValidateDevice(&n)==5);
            CHECK(target->seen==std::vector<unsigned>({10,10,10}));
            if(mode=="ordered"){
                CHECK(stream->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK);
                CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK&&stream->ValidateDevice(&n)==5);
                CHECK(target->seen==std::vector<unsigned>({10,10,10,20,20,20}));
                pp.BackBufferWidth=720;CHECK(stream->Reset(&pp)==D3D_OK);
                CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK&&stream->ValidateDevice(&n)==5);
                CHECK(target->seen==std::vector<unsigned>({10,10,10,20,20,20,20,20,20}));
                CHECK(stream->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK);
                fillImport(imported,4,40);CHECK(session->bind(imported,4,1)&&session->bindAliases(nullptr,0,4,1));
                CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK&&stream->ValidateDevice(&n)==5);
                CHECK(target->seen==std::vector<unsigned>({10,10,10,20,20,20,20,20,20,40,40,40}));
            }
            CHECK(!I::playbackContext);stream->Release();
        }
    }
    if(session)CHECK(session->context()->workerStarts==session->context()->workerStops);
    scope.reset();session.reset();CHECK(weak.expired()&&!I::active&&!I::playbackContext&&!activeSnapshot);
    checkClean();std::printf("PASS stand actual Replayer %s: pinned PE, per-trigger queue copies, recorded failure, lifecycle; no GPU/game/FPS claim\n",mode.c_str());
}
