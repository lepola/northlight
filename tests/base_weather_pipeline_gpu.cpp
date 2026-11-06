// Manual stage measurement of the production WorldRenderer on frozen scene/depth.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <array>
#include <cassert>
struct BaseCounts {unsigned long long draws=0,copies=0,targets=0,sets=0,gets=0,captures=0,applies=0,psRegisters=0,vsRegisters=0,clears=0,mrtBinds=0;};
static BaseCounts baseCounts;
static IDirect3DPixelShader9* baseWatchedShader=nullptr;static bool baseShaderBound=false;
struct BaseRow {unsigned long long frame;char stage[40];double ms;BaseCounts a,b;};
static std::array<BaseRow,64> baseRows;static unsigned baseRowCount=0;
void northlightBaseStage(const char* prefix,const char* stage,unsigned long long frame){
    if(std::strcmp(prefix,"BASE_GPU"))return;
    using Clock=std::chrono::steady_clock;static auto previous=Clock::now();static BaseCounts before;
    const auto now=Clock::now();
    if(std::strcmp(stage,"begin")){
        const auto& a=baseCounts;const auto& b=before;
        assert(baseRowCount<baseRows.size());auto& row=baseRows[baseRowCount++];row.frame=frame;std::snprintf(row.stage,sizeof row.stage,"%s",stage);row.ms=std::chrono::duration<double,std::milli>(now-previous).count();row.a=a;row.b=b;
    }else baseRowCount=0;
    before=baseCounts;previous=Clock::now();
}
#define main northlight_unused_screen_main
#include "world_screen_effects_gpu.cpp"
#undef main
static void basePrintRows(){for(unsigned i=0;i<baseRowCount;++i){const auto& row=baseRows[i];const auto& a=row.a;const auto& b=row.b;std::printf("BASE_CPU frame=%llu stage=%s ms=%.9f draws=%llu copies=%llu targets=%llu sets=%llu gets=%llu captures=%llu applies=%llu psRegisters=%llu vsRegisters=%llu clears=%llu mrtBinds=%llu\n",row.frame,row.stage,row.ms,a.draws-b.draws,a.copies-b.copies,a.targets-b.targets,a.sets-b.sets,a.gets-b.gets,a.captures-b.captures,a.applies-b.applies,a.psRegisters-b.psRegisters,a.vsRegisters-b.vsRegisters,a.clears-b.clears,a.mrtBinds-b.mrtBinds);}}
static void baseChecked(HRESULT hr,unsigned line){if(FAILED(hr)){std::printf("BASE_HRESULT line=%u hr=%08lx\n",line,(unsigned long)hr);std::abort();}}
#define screenOk(call) baseChecked((call),__LINE__)

struct BaseBlock:IDirect3DStateBlock9 {
    IDirect3DStateBlock9* real;ULONG refs=1;
    explicit BaseBlock(IDirect3DStateBlock9* block):real(block){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(id!=IID_IUnknown&&id!=IID_IDirect3DStateBlock9)return E_NOINTERFACE;*out=this;AddRef();return D3D_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){real->Release();delete this;}return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out)override{return real->GetDevice(out);}
    HRESULT STDMETHODCALLTYPE Capture()override{++baseCounts.captures;return real->Capture();}
    HRESULT STDMETHODCALLTYPE Apply()override{++baseCounts.applies;return real->Apply();}
};
struct BaseMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    bool failNextDraw=false;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{baseShaderBound|=p&&p==baseWatchedShader;return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** out)override{IDirect3DStateBlock9* p=nullptr;const auto hr=real->CreateStateBlock(t,&p);if(SUCCEEDED(hr)&&p){++baseCounts.captures;*out=new BaseBlock(p);}else *out=p;return hr;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s)override{++baseCounts.draws;if(failNextDraw){failNextDraw=false;return E_FAIL;}return real->DrawPrimitiveUP(t,n,p,s);}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE f)override{++baseCounts.copies;return real->StretchRect(a,ar,b,br,f);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{++baseCounts.targets;baseCounts.mrtBinds+=n>0&&p;return real->SetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{++baseCounts.clears;return real->Clear(n,rects,flags,color,z,stencil);}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE k,DWORD v)override{++baseCounts.sets;return real->SetRenderState(k,v);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD v)override{++baseCounts.sets;return real->SetSamplerState(n,k,v);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD n,IDirect3DBaseTexture9* p)override{++baseCounts.sets;return real->SetTexture(n,p);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT n,const float* p,UINT count)override{baseCounts.psRegisters+=count;return real->SetPixelShaderConstantF(n,p,count);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT n,const float* p,UINT count)override{baseCounts.vsRegisters+=count;return real->SetVertexShaderConstantF(n,p,count);}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** p)override{++baseCounts.gets;return real->GetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* p)override{++baseCounts.gets;return real->GetViewport(p);}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** p)override{++baseCounts.gets;return real->GetDepthStencilSurface(p);}
};
struct BaseImage {unsigned header[5];std::vector<unsigned char> bytes;};
static BaseImage baseRead(const char* path){std::ifstream f(path,std::ios::binary);assert(f);BaseImage image{};f.read(reinterpret_cast<char*>(image.header),20);assert(image.header[0]==0x31524746);image.bytes.resize(size_t(image.header[1])*image.header[2]*image.header[4]);f.read(reinterpret_cast<char*>(image.bytes.data()),image.bytes.size());assert(f);return image;}
static IDirect3DTexture9* baseUpload(IDirect3DDevice9* d,const BaseImage& image){IDirect3DTexture9* t=nullptr;screenOk(d->CreateTexture(image.header[1],image.header[2],1,D3DUSAGE_RENDERTARGET,D3DFORMAT(image.header[3]),D3DPOOL_DEFAULT,&t,nullptr));IDirect3DSurface9 *source=nullptr,*destination=nullptr;screenOk(d->CreateOffscreenPlainSurface(image.header[1],image.header[2],D3DFORMAT(image.header[3]),D3DPOOL_SYSTEMMEM,&source,nullptr));D3DLOCKED_RECT lock{};screenOk(source->LockRect(&lock,nullptr,0));for(unsigned y=0;y<image.header[2];++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,image.bytes.data()+size_t(y)*image.header[1]*image.header[4],size_t(image.header[1])*image.header[4]);screenOk(source->UnlockRect());screenOk(t->GetSurfaceLevel(0,&destination));screenOk(d->UpdateSurface(source,nullptr,destination,nullptr));source->Release();destination->Release();return t;}
static void baseDumpAuxiliary(IDirect3DDevice9* d,IDirect3DTexture9* texture,const std::string& path){
    assert(texture);IDirect3DSurface9 *surface=nullptr,*readback=nullptr;screenOk(texture->GetSurfaceLevel(0,&surface));D3DSURFACE_DESC desc{};screenOk(surface->GetDesc(&desc));
    const unsigned pixelBytes=desc.Format==D3DFMT_R32F?4u:desc.Format==D3DFMT_A16B16G16R16F?8u:0u;assert(pixelBytes);
    screenOk(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&readback,nullptr));screenOk(d->GetRenderTargetData(surface,readback));
    D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));assert(lock.Pitch>=INT(desc.Width*pixelBytes));
    FILE* file=std::fopen(path.c_str(),"wb");assert(file);const unsigned header[]={0x31524746,desc.Width,desc.Height,unsigned(desc.Format),pixelBytes};assert(std::fwrite(header,20,1,file)==1);
    for(unsigned y=0;y<desc.Height;++y)assert(std::fwrite(static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*pixelBytes,1,file)==1);
    assert(std::fclose(file)==0);screenOk(readback->UnlockRect());readback->Release();surface->Release();
}
// Outside profiling: include bindings/constants that the world pass overwrites.
struct BaseState {
    IDirect3DSurface9* targets[4]={};IDirect3DSurface9* depth=nullptr;IDirect3DBaseTexture9* textures[16]={};
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
    IDirect3DVertexBuffer9* stream=nullptr;IDirect3DIndexBuffer9* indices=nullptr;UINT offset=0,stride=0,frequency=0;
    D3DVIEWPORT9 viewport{};float pixel[224][4]={},vertex[256][4]={};DWORD states[8]={},samplers[16][11]={};
    explicit BaseState(IDirect3DDevice9* d){
        for(unsigned i=0;i<4;++i)d->GetRenderTarget(i,&targets[i]);d->GetDepthStencilSurface(&depth);screenOk(d->GetViewport(&viewport));
        screenOk(d->GetVertexShader(&vs));screenOk(d->GetPixelShader(&ps));screenOk(d->GetVertexDeclaration(&declaration));
        screenOk(d->GetStreamSource(0,&stream,&offset,&stride));screenOk(d->GetStreamSourceFreq(0,&frequency));screenOk(d->GetIndices(&indices));
        screenOk(d->GetPixelShaderConstantF(0,pixel[0],224));screenOk(d->GetVertexShaderConstantF(0,vertex[0],256));
        const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_COLORWRITEENABLE};
        for(unsigned i=0;i<8;++i)screenOk(d->GetRenderState(keys[i],&states[i]));
        for(unsigned i=0;i<16;++i){screenOk(d->GetTexture(i,&textures[i]));for(unsigned k=0;k<11;++k)screenOk(d->GetSamplerState(i,D3DSAMPLERSTATETYPE(k+1),&samplers[i][k]));}
    }
    BaseState(const BaseState&)=delete;BaseState& operator=(const BaseState&)=delete;
    ~BaseState(){for(auto* p:targets)if(p)p->Release();if(depth)depth->Release();for(auto* p:textures)if(p)p->Release();if(vs)vs->Release();if(ps)ps->Release();if(declaration)declaration->Release();if(stream)stream->Release();if(indices)indices->Release();}
    void equal(const BaseState& b)const{
        assert(!std::memcmp(targets,b.targets,sizeof targets)&&depth==b.depth&&!std::memcmp(textures,b.textures,sizeof textures));
        assert(vs==b.vs&&ps==b.ps&&declaration==b.declaration&&stream==b.stream&&indices==b.indices&&offset==b.offset&&stride==b.stride&&frequency==b.frequency);
        assert(!std::memcmp(&viewport,&b.viewport,sizeof viewport)&&!std::memcmp(pixel,b.pixel,sizeof pixel)&&!std::memcmp(vertex,b.vertex,sizeof vertex));
        assert(!std::memcmp(states,b.states,sizeof states)&&!std::memcmp(samplers,b.samplers,sizeof samplers));
    }
};
int main(int argc,char** argv){
    assert(argc==7);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    const auto sceneImage=baseRead(argv[3]),depthImage=baseRead(argv[4]);const unsigned W=sceneImage.header[1],H=sceneImage.header[2];assert(depthImage.header[1]==W&&depthImage.header[2]==H);
    float bank[79][4];std::ifstream constants(argv[5],std::ios::binary);assert(constants.read(reinterpret_cast<char*>(bank),sizeof bank));
    const std::string settings=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,settings.c_str(),-1,rootPath,MAX_PATH));
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    const auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));BaseMeter meter(raw);auto* d=&meter;
    auto* scene=baseUpload(d,sceneImage);auto* depth=baseUpload(d,depthImage);IDirect3DSurface9* input=nullptr;screenOk(scene->GetSurfaceLevel(0,&input));
    auto* ao=screenTexture(d,std::max(1u,W/2),std::max(1u,H/2),(const float[4]){0,0,0,1});
    IDirect3DQuery9* event=nullptr;screenOk(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
    auto wait=[&]{screenOk(event->Issue(D3DISSUE_END));const auto at=GetTickCount();HRESULT hr;while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-at<10000);SwitchToThread();}screenOk(hr);};
    for(bool hdr:{false,true}){
        const auto format=hdr?D3DFMT_A16B16G16R16F:D3DFORMAT(sceneImage.header[3]);
        auto world=std::make_unique<WorldRenderer>(d);WorldScreenEffectsTest::weatherBase(*world,bank[0]);NorthlightStateBlockPool outerPool(d);NorthlightGpuProfile profile(d,"BASE_GPU");
        IDirect3DTexture9* copy=nullptr;IDirect3DSurface9 *copySurface=nullptr,*target=nullptr,*readback=nullptr;
        screenOk(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFORMAT(sceneImage.header[3]),D3DPOOL_DEFAULT,&copy,nullptr));screenOk(copy->GetSurfaceLevel(0,&copySurface));
        screenOk(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        for(unsigned frame=0;frame<40;++frame){
            screenOk(d->SetDepthStencilSurface(nullptr));screenOk(d->SetRenderTarget(0,target));D3DVIEWPORT9 viewport{0,0,W,H,0,1};screenOk(d->SetViewport(&viewport));
            assert(profile.beginFrame(frame+1000*hdr,true));const auto begin=std::chrono::steady_clock::now();
            screenOk(d->StretchRect(input,nullptr,copySurface,nullptr,D3DTEXF_NONE));profile.mark("SceneCopy1x");
            screenOk(d->BeginScene());{
                SavedState outer(d,&outerPool);assert(outer.ok);profile.mark("CallerStateCapture");
                if(frame&&std::getenv("NORTHLIGHT_BASE_REFERENCE"))WorldScreenEffectsTest::refuseBaseCompositor(*world);
                baseShaderBound=false;baseWatchedShader=WorldScreenEffectsTest::baseCompositor(*world);
                const bool proof=!std::getenv("NORTHLIGHT_BASE_AO_UNKNOWN");
                const bool ok=world->render(target,depth,W,H,D3DFORMAT(sceneImage.header[3]),bank[0][2],bank[0][3],bank[1][3],bank[1][3]+1/bank[2][0],0,&profile,nullptr,copy,ao,frame,hdr?copy:nullptr,hdr,true,false,proof
#ifdef NORTHLIGHT_TEST_WORLD_ENCLOSING_STATE
                    ,std::getenv("NORTHLIGHT_BASE_KEEP_WORLD_STATE_SAVE")?nullptr:&outer
#endif
                );assert(ok&&world->composited);
                if(frame)assert(baseShaderBound==(hdr&&proof&&std::getenv("NORTHLIGHT_BASE_BACKEND_VERIFIED")&&!std::getenv("NORTHLIGHT_BASE_REFERENCE")));
                profile.mark("WorldReturn");
            }profile.mark("CallerStateRestore");screenOk(d->EndScene());profile.endFrame();
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();wait();profile.poll();basePrintRows();
            std::printf("BASE_FRAME frame=%u hdr=%u warm=%u cpuMs=%.9f\n",frame+1000*hdr,unsigned(hdr),unsigned(frame<16),ms);
            if(frame==39){screenOk(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::string name=std::string(argv[6])+(hdr?"-hdr.fgr":"-sdr.fgr");FILE* file=std::fopen(name.c_str(),"wb");assert(file);const unsigned header[]={0x31524746,W,H,unsigned(format),hdr?8u:4u};assert(std::fwrite(header,20,1,file)==1);for(unsigned y=0;y<H;++y)assert(std::fwrite(static_cast<char*>(lock.pBits)+y*lock.Pitch,W*header[4],1,file)==1);std::fclose(file);screenOk(readback->UnlockRect());
                const std::string prefix=std::string(argv[6])+(hdr?"-hdr":"-sdr");
                baseDumpAuxiliary(d,WorldScreenEffectsTest::currentDistance(*world),prefix+"-distance.fgr");
                baseDumpAuxiliary(d,WorldScreenEffectsTest::rawFog(*world),prefix+"-fog.fgr");
            }
        }
        // An invalid enclosure must retain the standalone save. The valid one
        // must restore the same device state when the caller leaves its scope.
#ifdef NORTHLIGHT_TEST_WORLD_ENCLOSING_STATE
        for(bool valid:{false,true}){
            BaseState before(raw);screenOk(d->BeginScene());
            {SavedState outer(d,&outerPool),invalid(nullptr);assert(outer.ok);
             const auto captures=baseCounts.captures,applies=baseCounts.applies;
             assert(world->render(target,depth,W,H,D3DFORMAT(sceneImage.header[3]),bank[0][2],bank[0][3],bank[1][3],bank[1][3]+1/bank[2][0],0,nullptr,nullptr,copy,ao,40,hdr?copy:nullptr,hdr,true,false,true,valid?&outer:&invalid));
             assert(baseCounts.captures-captures==unsigned(!valid)&&baseCounts.applies-applies==unsigned(!valid));}
            screenOk(d->EndScene());BaseState after(raw);before.equal(after);
        }
        if(hdr){ // Last: a failed world draw may disable its renderer.
            BaseState before(raw);screenOk(d->BeginScene());
            {SavedState outer(d,&outerPool);assert(outer.ok);meter.failNextDraw=true;
             assert(!world->render(target,depth,W,H,D3DFORMAT(sceneImage.header[3]),bank[0][2],bank[0][3],bank[1][3],bank[1][3]+1/bank[2][0],0,nullptr,nullptr,copy,ao,41,copy,hdr,true,false,true,&outer));
             assert(!meter.failNextDraw&&!world->composited);}
            screenOk(d->EndScene());BaseState after(raw);before.equal(after);
        }
        std::printf("BASE_STATE_PASS hdr=%u validEnclosure=1 invalidEnclosureFallback=1 bindingsConstantsRestored=1 drawFailureRestored=%u\n",unsigned(hdr),unsigned(hdr));
#endif
        world.reset();readback->Release();target->Release();copySurface->Release();copy->Release();
    }
    event->Release();ao->Release();input->Release();scene->Release();depth->Release();raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);logFile=nullptr;std::puts("BASE_WEATHER_PROFILE_PASS");
}
