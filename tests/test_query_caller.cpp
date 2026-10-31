// Uses the command-stream SDK stub and portable preinclude, like test_command_stream.cpp.
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main
static bool resetDiagnostics=false;
static void resetCaller(){
    std::vector<std::string> lines;unsigned names=0;const auto game=std::this_thread::get_id();
    static unsigned* nameCalls=nullptr;nameCalls=&names;
    StreamDevice::Options options;options.diagnostics=[]{return resetDiagnostics;};
    options.callerModule=[](void* address)->const char*{CHECK(address);++*nameCalls;return "fixture+0x1234";};
    options.log=[&](const char* line){if(std::string(line).rfind("CSTREAM RESET ",0)==0){CHECK(std::this_thread::get_id()==game);lines.emplace_back(line);}};
    Rig rig(true,options);rig.sync();D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth=800;pp.BackBufferHeight=600;pp.BackBufferFormat=(D3DFORMAT)22;pp.Windowed=1;pp.MultiSampleType=(D3DMULTISAMPLE_TYPE)2;pp.BackBufferCount=2;
    const auto firstEpoch=rig.core().queryEpoch.load();
    CHECK(rig.dev->Reset(&pp)==D3D_OK&&names==0&&lines.empty());
    resetDiagnostics=true;CHECK(rig.dev->Reset(nullptr)==D3DERR_INVALIDCALL&&names==0&&lines.empty());
    pp.BackBufferWidth=1024;pp.BackBufferHeight=768;
    CHECK(rig.dev->Reset(&pp)==D3D_OK&&names==1&&lines.size()==2);
    CHECK(lines[0].find("caller=fixture+0x1234")!=std::string::npos&&lines[0].find("previous=800x600/22")!=std::string::npos);
    CHECK(lines[0].find("requested=1024x768/22 windowed=1 msaa=2/0 buffers=2")!=std::string::npos);
    CHECK(lines[1].find("ran=1 result=0x00000000 coop=0x00000000 totalMs=")!=std::string::npos);
    for(const char* key:{"beforeReplayMs=","replayMs=","afterReplayMs="})CHECK(lines[1].find(key)!=std::string::npos);
    gKnobs.coop.store(D3DERR_DEVICELOST);CHECK(rig.dev->Reset(&pp)==D3DERR_DEVICELOST);
    CHECK(lines.size()==4&&names==2&&rig.core().coopState.load()==D3DERR_DEVICELOST);
    char lost[80];std::snprintf(lost,sizeof lost,"result=0x%08lx coop=0x%08lx",(unsigned long)std::uint32_t(D3DERR_DEVICELOST),(unsigned long)std::uint32_t(D3DERR_DEVICELOST));
    CHECK(lines[3].find(lost)!=std::string::npos);
    gKnobs.coop.store(D3DERR_DEVICENOTRESET);CHECK(rig.dev->Reset(&pp)==D3D_OK);
    CHECK(lines.size()==6&&names==3&&rig.core().coopState.load()==D3D_OK&&rig.core().queryEpoch.load()==firstEpoch+4);
    CHECK(rig.target->resets==3);resetDiagnostics=false;
    rig.finish();checkClean();nameCalls=nullptr;
    std::puts("PASS RESET caller diagnostics: game-thread caller, original parameters, success/loss/recovery, timing spans, disabled/null paths, unchanged epoch/results and no leaks");
}
int main(){
    resetCaller();
    static unsigned calls=0;static void* namedQuery=nullptr;static std::uint64_t namedFrame=0;
    StreamDevice::Options options;options.queryCaller=[](std::uint64_t frame,void* query,void* address){CHECK(address);++calls;namedQuery=query;namedFrame=frame;};
    Rig rig(true,options);rig.core().timing=false;
    IDirect3DQuery9* q=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)8,&q)==S_OK);rig.sync();
    CHECK(q->Issue(D3::kIssueEnd)==S_OK);rig.sync();
    (void)q->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==0);
    rig.core().timing=true;DWORD value=0;HRESULT hr=S_FALSE;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(hr==S_FALSE){CHECK(std::chrono::steady_clock::now()<deadline);hr=q->GetData(&value,4,D3::kGetDataFlush);std::this_thread::yield();}
    CHECK(hr==S_OK&&value==0xABCD0000u&&calls==1&&namedQuery==q&&namedFrame==rig.core().frameNo);
    for(unsigned i=0;i<100;++i)CHECK(q->GetData(&value,4,D3::kGetDataFlush)==S_OK);
    CHECK(calls==1);
    CHECK(q->Issue(D3::kIssueEnd)==S_OK);rig.sync();(void)q->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==1);
    IDirect3DQuery9* other=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)8,&other)==S_OK);rig.sync();
    (void)other->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==2&&namedQuery==other);
    other->Release();q->Release();rig.finish();checkClean();
    std::puts("PASS EVENT caller diagnostics: off until enabled, correct query/frame, one callback across polls/reissues, original completion/data, no leaks");
}
