// Binding/state regression; validate_parallax_msaa.py also submits actual draws.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "forwarders.h"
#include "device_mirror.h"
#include "memory_guard.h"
struct Meter:ForwardIDirect3DDevice9 {
    unsigned shaders=0,maps=0;
    explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* p,IDirect3DPixelShader9** s)override{++shaders;return real->CreatePixelShader(p,s);}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* s)override{if(u&D3DUSAGE_RENDERTARGET)++maps;return real->CreateTexture(w,h,l,u,f,p,t,s);}
};
int main(int argc,char** argv){
    assert(argc==3);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"ParallaxMSAAAdmission";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* real=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real));
    Meter meter(real);DeviceMirror mirror;mirror.gate.exclusive=true;ExtensionDevice extension(&meter,&mirror);IDirect3DDevice9* d=&extension;
    const std::string folder=argv[2];auto vb=read(folder+"shaders_vertex_vs_3_0_terrain.bls-0.bin"),pb=read(folder+"shaders_pixel_ps_3_0_terrain1.bls-0.bin");std::vector<uint32_t> words;
    assert(!NorthlightWaterShaderPatch::patch(reinterpret_cast<uint32_t*>(vb.data()),vb.size()/4,words));
    IDirect3DVertexShader9 *vs=nullptr,*forward=nullptr;IDirect3DPixelShader9* ps=nullptr;
    ok(d->CreateVertexShader(reinterpret_cast<DWORD*>(vb.data()),&vs));ok(d->CreateVertexShader(reinterpret_cast<DWORD*>(words.data()),&forward));ok(d->CreatePixelShader(reinterpret_cast<DWORD*>(pb.data()),&ps));
    IDirect3DTexture9* source=nullptr;ok(d->CreateTexture(128,128,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr));D3DLOCKED_RECT lock{};ok(source->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<128;++y)std::memset(static_cast<char*>(lock.pBits)+y*lock.Pitch,128,512);source->UnlockRect(0);
    unsigned cases=0;
    for(unsigned samples:{2u,4u,8u,0u}){
        const auto msaa=static_cast<D3DMULTISAMPLE_TYPE>(samples);if(FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,msaa,nullptr)))continue;
        IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,msaa,0,FALSE,&target,nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));ok(d->SetTexture(0,source));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})ok(d->SetRenderState(state,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));
        const unsigned shaders=meter.shaders,maps=meter.maps;NorthlightAutoNormalCache cache(d);NorthlightAutoParallax parallax(d,cache);const float projection[]={1,1,-1,0},control[]={.018f,16,16,0};unsigned calls=0;
        ok(d->BeginScene());ok(parallax.draw(vs,forward,1,projection,control,[&]{++calls;IDirect3DPixelShader9* active=nullptr;ok(d->GetPixelShader(&active));const bool replaced=active!=ps;drop(active);assert(replaced);return S_OK;}));ok(d->EndScene());
        assert(calls==1);IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==ps);drop(restored);
        assert(meter.shaders>shaders&&meter.maps==maps+1);
        IDirect3DSurface9* originalDepth=nullptr;ok(d->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,msaa,0,FALSE,&originalDepth,nullptr));ok(d->SetDepthStencilSurface(originalDepth));
        D3DVIEWPORT9 viewport={0,0,64,64,0,1};ok(d->SetViewport(&viewport));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
        NorthlightAutoBump capture(d);NorthlightQuality::Settings settings;settings.autoBump=1;capture.configure(settings);
        capture.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<uint32_t*>(vb.data()),vb.size()/4),reinterpret_cast<DWORD*>(vb.data()),vb.size()/4);
        capture.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<uint32_t*>(pb.data()),pb.size()/4));
        unsigned captures=0;ok(d->BeginScene());capture.capture(vs,[&]{
            IDirect3DSurface9 *color=nullptr,*depth=nullptr;ok(d->GetRenderTarget(0,&color));ok(d->GetDepthStencilSurface(&depth));D3DSURFACE_DESC a{},b{};ok(color->GetDesc(&a));ok(depth->GetDesc(&b));
            assert(a.MultiSampleType==D3DMULTISAMPLE_NONE&&b.MultiSampleType==D3DMULTISAMPLE_NONE);assert(samples==0||depth!=originalDepth);drop(color);drop(depth);++captures;return S_OK;
        });ok(d->EndScene());assert(captures==1&&capture.texture());
        IDirect3DSurface9 *restoredColor=nullptr,*restoredDepth=nullptr;ok(d->GetRenderTarget(0,&restoredColor));ok(d->GetDepthStencilSurface(&restoredDepth));assert(restoredColor==target&&restoredDepth==originalDepth);drop(restoredColor);drop(restoredDepth);ok(d->SetDepthStencilSurface(nullptr));drop(originalDepth);
        std::printf("STATIC_CAPTURE_MSAA_PASS samples=%u privateDepth=1 restored=1\n",samples);
        std::printf("PARALLAX_MSAA_PASS samples=%u callbackNative=%d generatedMaps=%u stateRestored=1\n",samples,0,meter.maps-maps);++cases;drop(target);
    }
    assert(cases>=2);
    // Geometry-demand pressure keeps stream caps full. Material caches shrink without suspending relief.
    NorthlightAutoBump bump(d);NorthlightQuality::Settings settings;bump.configure(settings);
    NorthlightMemoryGuard::Guard guard;const uint64_t MiB=1024ull*1024ull;
    const auto entered=guard.update(715*MiB,228*MiB,1,768*MiB);assert(entered.entered&&guard.pressure()&&!guard.capsHalved());
    bump.setMemoryPressure(guard.pressure());assert(bump.active()&&!bump.texture());const unsigned maps=meter.maps,shaders=meter.shaders;
    for(unsigned i=0;i<120;++i){unsigned calls=0;ok(bump.nativeDraw([&]{++calls;return S_OK;}));assert(calls==1);bump.endFrame();}
    assert(meter.maps==maps&&meter.shaders==shaders);
    guard.update(1200*MiB,300*MiB,10001,0);const auto recovered=guard.update(1200*MiB,300*MiB,21001,0);assert(recovered.recovered&&!guard.pressure());
    bump.setMemoryPressure(guard.pressure());assert(bump.active());
    std::puts("MATERIAL_PRESSURE_PASS demand768 available715 capsFull=1 reliefActive120Frames=1 recovered=1");return 0;
}
