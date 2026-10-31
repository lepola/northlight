#define NORTHLIGHT_REPLAY_STAND 1
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main
#include <fstream>
#include <iterator>
#ifndef _WIN32
#define __declspec(x)
#define __cdecl
static DWORD GetCurrentThreadId(){return Replayer::currentTid();}
#endif
#include "stand_api.h"

namespace I=NorthlightStream::StandInputs;
namespace A=NorthlightStream::SnapshotArchive;
static constexpr std::uint32_t oldTexture=0x0abc1000;
static bool captureAliases(GameSnapshot& destination,Trigger trigger,std::uint64_t draw){
    I::CaptureScope admission(unsigned(trigger),draw);if(!admission.entered())return false;
    {ScopedRecording recording(destination);unsigned value=0;
        CHECK(I::read(0x18000000,&value,4)&&I::read(0x18000004,&value,4));
        value=99;CHECK(!I::read(0x18000008,&value,4)&&value==99);
        destination.triggerKind=unsigned(trigger);destination.triggerDraw=draw;}
    return admission.commit(destination);
}
static void importPacket(std::uint64_t draw,unsigned width,std::uint32_t id=oldTexture){
    GameSnapshot snapshot;snapshot.triggerKind=1;snapshot.triggerDraw=draw;
    CHECK(snapshot.record(0x18000000,&id,4,true)&&snapshot.record(0x18000004,&width,4,true)&&snapshot.record(0x18000008,nullptr,4,false));
    std::vector<unsigned char> packet(A::MaxBytes);std::size_t size=0;A::Error error;
    CHECK(A::encode(snapshot,packet.data(),packet.size(),size,error));
    CHECK(NorthlightStandSnapshot(packet.data(),unsigned(size),draw,1));
}
struct AliasTarget:TargetDevice {
    std::atomic<bool> entered{false},releaseFirst{false};bool blockFirst=false,expectInvalid=false,expectMissing=false;
    std::unordered_map<unsigned,const void*> created;std::vector<unsigned> seen;
    HRESULT CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared)override{
        const auto result=TargetDevice::CreateTexture(w,h,levels,usage,format,pool,out,shared);
        if(SUCCEEDED(result)&&out&&*out)created[w]=*out;return result;
    }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type,UINT start,UINT count)override{
        if(blockFirst&&seen.empty()){
            entered.store(true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!releaseFirst.load()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
        }
        CHECK(!I::active&&I::playbackContext);
        if(expectInvalid){CHECK(!I::currentContext()->valid());return TargetDevice::DrawPrimitive(type,start,count);}
        CHECK(activeSnapshot&&activeSnapshot->mode==GameSnapshot::Mode::Playing);
        const auto& aliases=activeSnapshot->textureAliases();CHECK(aliases.sealed&&aliases.count<=10);
        unsigned id=0,width=0;CHECK(I::read(0x18000000,&id,4)&&I::read(0x18000004,&width,4));
        const auto* resolved=NorthlightStream::inner(reinterpret_cast<const void*>(std::uintptr_t(id)));
        if(expectMissing)CHECK(!resolved&&!I::currentContext()->valid());
        else if(!id)CHECK(!resolved&&!width&&I::currentContext()->valid());
        else CHECK(resolved==created.at(width)&&I::currentContext()->valid());
        seen.push_back(width);return TargetDevice::DrawPrimitive(type,start,count);
    }
};
static void sync(StreamDevice& stream){DWORD result=0;CHECK(stream.ValidateDevice(&result)==5);}
static auto importedBytes(){
    std::vector<unsigned char> bytes(A::MaxBytes);std::size_t size=0;A::Error error;
    CHECK(A::encode(NorthlightStandApi::state().snapshot,bytes.data(),bytes.size(),size,error));bytes.resize(size);return bytes;
}
static void bind(IDirect3DTexture9* texture,std::uint64_t draw=1){
    const auto before=importedBytes();
    const std::uint64_t pairs[]={oldTexture,reinterpret_cast<std::uintptr_t>(texture)};
    CHECK(NorthlightStandAliases(pairs,1,draw,1));
    CHECK(importedBytes()==before); // Only the separate capability table may change.
}
static void registryFamilies(){
    Registry registry;std::uint64_t generation=88;const auto exposed=reinterpret_cast<IUnknown*>(std::uintptr_t(0x100000));
    CHECK(!registry.textureGeneration(exposed,generation)&&!generation);
    for(Kind kind:{Kind::Texture,Kind::CubeTexture,Kind::VolumeTexture,Kind::Surface,Kind::Volume,Kind::VertexBuffer}){
        ProxyBase proxy(nullptr,kind);proxy.unk=exposed;proxy.inner=reinterpret_cast<IUnknown*>(std::uintptr_t(0x200000));registry.addProxy(&proxy);
        const bool texture=kind==Kind::Texture||kind==Kind::CubeTexture||kind==Kind::VolumeTexture;
        CHECK(registry.textureGeneration(exposed,generation)==texture);
        CHECK(registry.textureInner(exposed,proxy.standGeneration)==(texture?proxy.inner:nullptr));
        CHECK(!registry.textureInner(exposed,proxy.standGeneration+1));
        proxy.dead.store(true);CHECK(!registry.textureGeneration(exposed,generation)&&!generation&&!registry.textureInner(exposed,proxy.standGeneration));
        proxy.dead.store(false);proxy.inner=nullptr;CHECK(!registry.textureInner(exposed,proxy.standGeneration));registry.erase(&proxy);
    }
}
int main(int argc,char** argv){
    CHECK(argc==3);const std::string mode=argv[2];registryFamilies();
    std::ifstream file(argv[1],std::ios::binary);CHECK(file);const std::vector<unsigned char> image{std::istreambuf_iterator<char>(file),{}};
    CHECK(NorthlightStandOpen(image.data(),unsigned(image.size())));
    auto& state=NorthlightStandApi::state();const auto weak=std::weak_ptr<I::Context>(state.inputs->context());
    importPacket(1,mode=="zero"?0:16,mode=="zero"?0:oldTexture);
    std::uint64_t status[8]{};CHECK(NorthlightStandStatus(status,8)&&status[0]==1&&status[1]==1&&status[2]==1&&!status[7]);
    if(mode=="no-stream"){
        CHECK(!NorthlightStandAliases(nullptr,0,1,1)&&!state.inputs->valid());
    }else if(mode=="before-marker"){
        CHECK(!NorthlightStream::inner(reinterpret_cast<const void*>(std::uintptr_t(oldTexture)))&&!state.inputs->valid());
    }else{
        auto* target=new AliasTarget;FakeD3D parent;const char* reason=nullptr;
        StreamDevice::Options options;options.capture=&captureAliases;
        D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferCount=2;pp.BackBufferFormat=22;
        auto* stream=StreamDevice::make(target,&parent,&pp,std::move(options),&reason);CHECK(stream&&I::textureAliasGeneration);
        IDirect3DTexture9* first=nullptr;IDirect3DTexture9* second=nullptr;IDirect3DVertexBuffer9* buffer=nullptr;
        alignas(StreamTexture) unsigned char reused[sizeof(StreamTexture)];StreamTexture* placement=nullptr;
        if(mode=="reused"){
            placement=new(reused)StreamTexture(&stream->streamCore(),16,16,1,0,22,D3::kPoolManaged);first=placement;
            CHECK(runTask(stream->streamCore(),[&](StreamCore& core){
                auto* inner=new TTexture(16,16,1,0,22,D3::kPoolManaged);target->created[16]=inner;core.reg.bindInner(placement,inner);
            },Cmd::SyncInit));
        }else CHECK(stream->CreateTexture(16,16,1,0,22,D3::kPoolManaged,&first,nullptr)==D3D_OK&&first);
        auto* proxy=stream->streamCore().reg.findProxy(first);CHECK(proxy);const auto refs=proxy->refs.load(),uses=proxy->use.load();
        if(mode=="ordered"||mode=="retired-owner"){
            target->blockFirst=true;CHECK(stream->CreateTexture(32,32,1,0,22,D3::kPoolManaged,&second,nullptr)==D3D_OK);
            bind(first);CHECK(proxy->refs==refs&&proxy->use==uses);CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);stream->streamCore().q.publish();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!target->entered.load()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
            importPacket(2,32);bind(second,2);
            CHECK(first->Release()==0);first=nullptr; // Its Destroy follows the blocked draw; no alias pin is permitted.
            if(mode=="retired-owner"){state.scope.reset();state.inputs.reset();CHECK(!weak.expired());}
            target->releaseFirst.store(true);sync(*stream);CHECK(target->seen==std::vector<unsigned>({16}));
            if(mode=="ordered"){
                CHECK(stream->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK);
                CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);sync(*stream);CHECK(target->seen==std::vector<unsigned>({16,32}));
                pp.BackBufferWidth=720;CHECK(stream->Reset(&pp)==D3D_OK);
                CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);sync(*stream);CHECK(target->seen==std::vector<unsigned>({16,32,32}));
            }
        }else if(mode=="zero"||mode=="missing-id"||mode=="unsealed"){
            if(mode!="unsealed")CHECK(NorthlightStandAliases(nullptr,0,1,1));
            target->expectMissing=mode=="missing-id";target->expectInvalid=mode=="unsealed";
            CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);sync(*stream);
            CHECK(state.inputs->valid()==(mode=="zero"));
        }else if(mode=="dead"||mode=="reused"||mode=="max10"||mode=="wrong-thread"){
            if(mode=="wrong-thread"){
                std::thread foreign([]{CHECK(!NorthlightStandAliases(nullptr,0,1,1));});foreign.join();CHECK(state.inputs->valid());
            }
            if(mode=="max10"){
                std::uint64_t pairs[20];for(unsigned i=0;i<10;++i){pairs[2*i]=oldTexture+4*i;pairs[2*i+1]=reinterpret_cast<std::uintptr_t>(first);}
                CHECK(NorthlightStandAliases(pairs,10,1,1));
            }else bind(first);
            CHECK(proxy->refs==refs&&proxy->use==uses);CHECK(stream->DrawPrimitive(4,0,1)==D3D_OK);sync(*stream);
            CHECK(target->seen==std::vector<unsigned>({16}));
            if(mode=="dead"){
                CHECK(first->Release()==0);first=nullptr;
                CHECK(runTask(stream->streamCore(),[&](StreamCore&){CHECK(!NorthlightStream::inner(reinterpret_cast<const void*>(std::uintptr_t(oldTexture))));},Cmd::SyncInit));
                CHECK(!state.inputs->valid());
            }else if(mode=="reused"){
                const auto generation=placement->standGeneration;
                CHECK(runTask(stream->streamCore(),[&](StreamCore& core){
                    core.reg.erase(placement);placement->inner->Release();placement->~StreamTexture();
                    placement=new(reused)StreamTexture(&core,32,32,1,0,22,D3::kPoolManaged);
                    CHECK(placement->standGeneration!=generation&&placement->unk==first);
                    core.reg.bindInner(placement,new TTexture(32,32,1,0,22,D3::kPoolManaged));
                    CHECK(!NorthlightStream::inner(reinterpret_cast<const void*>(std::uintptr_t(oldTexture))));
                },Cmd::SyncInit));CHECK(!state.inputs->valid());
            }
        }else if(mode=="commit-order"||mode=="missing-commit"){
            bind(first);GameSnapshot destination;
            {I::CaptureScope admission(1,1);CHECK(admission.entered());
                {ScopedRecording recording(destination);unsigned value=0;CHECK(I::read(0x18000000,&value,4));destination.triggerKind=1;destination.triggerDraw=1;
                    if(mode=="commit-order")CHECK(!admission.commit(destination));}}
            CHECK(!state.inputs->valid()&&!destination.textureAliases().sealed);
        }else{
            std::uint64_t pairs[22]={oldTexture,reinterpret_cast<std::uintptr_t>(first)};unsigned count=1,trigger=1;std::uint64_t draw=1;const std::uint64_t* data=pairs;
            if(mode=="duplicate"){pairs[2]=pairs[0];pairs[3]=pairs[1];count=2;}
            else if(mode=="wrong-kind"){
                CHECK(stream->CreateVertexBuffer(16,0,0,D3::kPoolManaged,&buffer,nullptr)==D3D_OK);pairs[1]=reinterpret_cast<std::uintptr_t>(buffer);
            }else if(mode=="dead-bind"){CHECK(first->Release()==0);sync(*stream);first=nullptr;}
            else if(mode=="missing-bind")pairs[1]=0x12345678;
            else if(mode=="low-id")pairs[0]=4;
            else if(mode=="unaligned-id")pairs[0]=oldTexture+1;
            else if(mode=="wide-id")pairs[0]=0x100000000ull;
            else if(mode=="end-id")pairs[0]=0xfffffffcul;
            else if(mode=="null-proxy")pairs[1]=0;
            else if(mode=="oversize")count=11;
            else if(mode=="null-pairs")data=nullptr;
            else if(mode=="wrong-draw")draw=2;
            else if(mode=="wrong-trigger")trigger=2;
            else if(mode=="provider-throws")I::textureAliasGeneration=[](std::uintptr_t,std::uint64_t&)->bool{throw 1;};
            else if(mode=="repeat"){CHECK(NorthlightStandAliases(nullptr,0,1,1));count=0;data=nullptr;}
            else CHECK(false);
            CHECK(!NorthlightStandAliases(data,count,draw,trigger)&&!state.inputs->valid());
        }
        if(placement){CHECK(runTask(stream->streamCore(),[&](StreamCore& core){core.reg.erase(placement);placement->inner->Release();placement->~StreamTexture();},Cmd::SyncInit));first=nullptr;}
        if(buffer)buffer->Release();if(first)first->Release();if(second)second->Release();
        stream->Release();CHECK(!I::textureAliasGeneration&&!activeCore&&!activeSnapshot&&!I::playbackContext);
    }
    state.scope.reset();state.inputs.reset();CHECK(weak.expired()&&!I::active&&!I::playbackContext&&!activeSnapshot);checkClean();
    std::printf("PASS stand aliases %s: actual queued Replayer, per-packet POD generation proof, unchanged raw keys, no COM alias pins; no GPU/game/FPS claim\n",mode.c_str());
}
