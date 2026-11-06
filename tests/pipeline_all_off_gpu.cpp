#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "renderer.cpp"
#include <cassert>
#include <fstream>

struct PipelineMeter:ForwardIDirect3DDevice9 {
    uint64_t reads=0,draws=0;bool submit=false;
    explicit PipelineMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** p)override{++reads;return real->GetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** p)override{++reads;return real->GetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE k,DWORD* p)override{++reads;return real->GetRenderState(k,p);}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* p)override{++reads;return real->GetViewport(p);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* p,UINT count)override{++reads;return real->GetVertexShaderConstantF(first,p,count);}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT first,UINT n)override{++draws;return submit?real->DrawPrimitive(t,first,n):D3D_OK;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT minimum,UINT vertices,UINT first,UINT n)override{++draws;return submit?real->DrawIndexedPrimitive(t,base,minimum,vertices,first,n):D3D_OK;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override{++draws;return submit?real->DrawPrimitiveUP(t,n,p,stride):D3D_OK;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT minimum,UINT vertices,UINT n,const void* indices,D3DFORMAT f,const void* p,UINT stride)override{++draws;return submit?real->DrawIndexedPrimitiveUP(t,minimum,vertices,n,indices,f,p,stride):D3D_OK;}
};
struct PipelineOffTest {
    static void mode(Device& d,unsigned mode){
        d.clearFrame();auto q=d.world->quality;
        q.gi=q.actorShadows=q.contactAO=q.autoBump=q.weather=q.equipmentLights=0;q.localLightLimit=0;
#ifdef NORTHLIGHT_PIPELINE_BASELINE
        d.world->quality=q;d.enabled=mode!=0;d.world->setEffects({false,false,false});
#else
        q.effects=mode!=1;q.worldShadows=q.worldFog=q.localLights=mode!=2;
        NorthlightSettings::Config next;next.quality=q;next.celestial=d.world->celestialProfiles;d.world->applyLiveSettings(next);
        d.masterEnabled=mode!=0;d.refreshPipeline();assert(!d.enabled&&!d.world->pipelineActive&&!d.particleOn);
#endif
        d.autoBump->configure(q);d.contactAO=false;d.latchDrawGates();
    }
    static void cpu(Device& d,PipelineMeter& meter){
        constexpr unsigned iterations=25000;
        struct Vertex {float x,y,z,rhw;DWORD color;};Vertex v[3]={{0,0,.5f,1,0xff204080},{64,0,.5f,1,0xff204080},{0,64,.5f,1,0xff204080}};WORD indices[3]={0,1,2};
        for(unsigned round=0;round<3;++round)for(unsigned offset=0;offset<3;++offset){const unsigned mode=(round+offset)%3;
            PipelineOffTest::mode(d,mode);meter.submit=false;meter.draws=meter.reads=0;const auto before=d.drawCalls;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<iterations;++i){
                assert(d.DrawPrimitive(D3DPT_TRIANGLELIST,0,1)==D3D_OK);
                assert(d.DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1)==D3D_OK);
                assert(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex))==D3D_OK);
                assert(d.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,v,sizeof(Vertex))==D3D_OK);
            }
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            assert(meter.draws==4*iterations&&d.drawCalls-before==4*iterations);
#ifndef NORTHLIGHT_PIPELINE_BASELINE
            assert(meter.reads==0);
#endif
            std::printf("ALL_OFF_CPU mode=%u round=%u ms=%.6f draws=%u reads=%llu\n",mode,round,ms,4*iterations,(unsigned long long)meter.reads);
        }
    }
    static void workerPause(Device& d){
#ifndef NORTHLIGHT_PIPELINE_BASELINE
        auto& w=*d.world;PipelineOffTest::mode(d,1);
        auto cache=std::make_shared<WorldRenderer::Snapshot>();cache->bvh=std::make_shared<NorthlightGI::BVH>();cache->serial=77;w.active=cache;
        std::shared_ptr<const WorldRenderer::PaletteRegion> before;
        {std::lock_guard<std::mutex> lock(w.mutex);before=w.publishedPaletteRegion;w.request.map="OfflinePaused";w.request.id++;w.pending=true;}w.wake.notify_one();
        for(unsigned n=0;n<2000;++n){bool done;{std::lock_guard<std::mutex> lock(w.mutex);done=!w.pending&&!w.workerBusy;}if(done)break;Sleep(1);}
        {std::lock_guard<std::mutex> lock(w.mutex);assert(!w.pending&&!w.workerBusy&&!w.request.enabled&&w.publishedPaletteRegion==before);}assert(w.active==cache&&w.active->serial==77);
        w.setPipelineEnabled(true);
        {std::lock_guard<std::mutex> lock(w.mutex);w.request.id++;w.pending=true;}w.wake.notify_one();
        bool resumed=false;for(unsigned n=0;n<3000;++n){{std::lock_guard<std::mutex> lock(w.mutex);resumed=w.publishedPaletteRegion!=before;}if(resumed)break;Sleep(1);}assert(resumed&&w.active==cache);
        w.setPipelineEnabled(false);std::puts("ALL_OFF_WORKER_PASS pause=1 queuedRequestIgnored=1 resume=1 cacheRetained=1");
#endif
    }
    static void frames(Device& d,PipelineMeter& meter){
        for(unsigned round=0;round<3;++round)for(unsigned offset=0;offset<3;++offset){const unsigned mode=(round+offset)%3;PipelineOffTest::mode(d,mode);
            for(unsigned i=0;i<20;++i)d.finishFrame(false);meter.draws=meter.reads=0;
            const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i)d.finishFrame(false);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            assert(meter.draws==0);std::printf("ALL_OFF_FRAME_CPU mode=%u round=%u frames=200 ms=%.6f stateReads=%llu\n",mode,round,ms,(unsigned long long)meter.reads);
        }
    }
    static void diagnostics(Device& d,PipelineMeter& meter){
        PipelineOffTest::mode(d,1);meter.submit=false;assert(!d.gpuFrameStarted);NorthlightDiagnostics::configure(true);
        assert(SUCCEEDED(d.DrawPrimitive(D3DPT_TRIANGLELIST,0,1)));assert(d.gpuFrameStarted);
        NorthlightDiagnostics::configure(false);d.clearFrame();std::puts("ALL_OFF_DIAGNOSTICS_PASS requestedProfilingRetained=1");
    }
};
int main(int argc,char** argv){
    assert(argc==3);std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightAllOffStand";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,128,80,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=128;pp.BackBufferHeight=80;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw)));
    PipelineMeter meter(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();auto* d=new Device(&meter,api,presentation);
    PipelineOffTest::cpu(*d,meter);PipelineOffTest::frames(*d,meter);PipelineOffTest::workerPause(*d);PipelineOffTest::diagnostics(*d,meter);
    IDirect3DSurface9 *target=nullptr,*read=nullptr;assert(SUCCEEDED(raw->CreateRenderTarget(128,80,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));assert(SUCCEEDED(raw->CreateOffscreenPlainSurface(128,80,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr)));
    struct V {float x,y,z,rhw;DWORD color;};const V triangle[]={{0,0,.5f,1,0xff2080c0},{100,0,.5f,1,0xff2080c0},{0,70,.5f,1,0xff2080c0}};
    std::vector<unsigned char> reference;for(unsigned mode=0;mode<3;++mode){PipelineOffTest::mode(*d,mode);meter.submit=true;
        assert(SUCCEEDED(raw->SetRenderTarget(0,target)));raw->SetDepthStencilSurface(nullptr);raw->SetVertexShader(nullptr);raw->SetPixelShader(nullptr);raw->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);raw->SetTexture(0,nullptr);
        raw->SetRenderState(D3DRS_ZENABLE,FALSE);raw->SetRenderState(D3DRS_LIGHTING,FALSE);raw->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);raw->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        raw->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);raw->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);raw->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0);
        assert(SUCCEEDED(raw->BeginScene()));assert(SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,sizeof(V))));assert(SUCCEEDED(raw->EndScene()));assert(SUCCEEDED(raw->GetRenderTargetData(target,read)));
        D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));std::vector<unsigned char> pixels(128*80*4);for(unsigned y=0;y<80;++y)std::memcpy(pixels.data()+y*128*4,static_cast<const char*>(lock.pBits)+y*lock.Pitch,128*4);read->UnlockRect();
        if(!mode)reference=pixels;else assert(reference==pixels);assert(std::any_of(pixels.begin(),pixels.end(),[](unsigned char c){return c==0xc0;}));
    }
    std::puts("ALL_OFF_IMAGE_PASS F10equalsF11=1 originalDraw=1");read->Release();target->Release();d->Release();raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("ALL_OFF_PIPELINE_PASS");
}
