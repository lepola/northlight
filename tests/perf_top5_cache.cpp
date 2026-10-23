// Offscreen real-device oracle: original client VS/PS, production capture and LocalDirect.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");std::fflush(stdout);va_end(args);}
#include "saved_state.h"
static unsigned availableMiB=1024;
static BOOL cacheMemory(LPMEMORYSTATUSEX m){m->ullAvailVirtual=uint64_t(availableMiB)<<20;return TRUE;}
#define GlobalMemoryStatusEx cacheMemory
#include "auto_bump_renderer.h"
#undef GlobalMemoryStatusEx
#include "world_compiled_shaders.h"
#include "forwarders.h"
struct MaterialCallMeter:ForwardIDirect3DDevice9 {
    unsigned samplerGets=0,samplerSets=0;
    explicit MaterialCallMeter(IDirect3DDevice9* device):ForwardIDirect3DDevice9(device){}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD* v) override{++samplerGets;return real->GetSamplerState(n,k,v);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD v) override{++samplerSets;return real->SetSamplerState(n,k,v);}
};
struct GenerationMeter:MaterialCallMeter {
    unsigned generations=0,normalDraws=0;
    explicit GenerationMeter(IDirect3DDevice9* device):MaterialCallMeter(device){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD usage,D3DFORMAT f,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared)override{if(usage&D3DUSAGE_RENDERTARGET)++generations;return real->CreateTexture(w,h,l,usage,f,pool,out,shared);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride)override{++normalDraws;return real->DrawPrimitiveUP(type,count,data,stride);}
};
int main(int argc,char** argv){
    assert(argc==2);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialPerformance";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));GenerationMeter meter(raw);IDirect3DDevice9* d=&meter;

    NorthlightAutoNormalCache cache(d);std::vector<IDirect3DTexture9*> sources;
    for(unsigned i=0;i<48;++i){IDirect3DTexture9* tex=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_DXT1,D3DPOOL_MANAGED,&tex,nullptr));D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<128;++y)std::memset((char*)lock.pBits+y*lock.Pitch,0,1024);tex->UnlockRect(0);sources.push_back(tex);}
    ok(d->BeginScene());
    unsigned counts[2];
    for(unsigned round=0;round<2;++round){auto before=meter.generations;for(auto* source:sources){assert(cache.resolve(source));cache.endFrame();}counts[round]=meter.generations-before;}
    assert(counts[0]==48);
#ifdef PERF_BASELINE
    assert(counts[1]==48);
#else
    assert(counts[1]==0);
    LARGE_INTEGER frequency,start,finish;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    for(unsigned n=0;n<200000;++n)for(auto* source:sources){auto* normal=cache.resolve(source);assert(normal&&cache.resolve(source)==normal);}
    QueryPerformanceCounter(&finish);
    std::printf("NORMAL_CACHE_WARM pairs=9600000 cpuMs=%.3f\n",double(finish.QuadPart-start.QuadPart)*1000/frequency.QuadPart);
    auto* retained=cache.resolve(sources[2]);assert(retained);
    availableMiB=715;cache.refreshBudget();
    // LRU eviction moves source 2 away from its former, still-valid vector index.
    assert(cache.resolve(sources[2])==retained);
    auto* model=cache.resolve(sources[2],true);assert(model&&model!=retained);
    assert(cache.resolve(sources[2])==retained);
    auto before=meter.normalDraws;for(auto* source:sources){assert(cache.resolve(source));cache.endFrame();}assert(meter.normalDraws>before);
    assert(NorthlightAutoNormalMemory::budget(400ull<<20,32ull<<20)==(32ull<<20));assert(NorthlightAutoNormalMemory::budget(950ull<<20,96ull<<20)==(96ull<<20));assert(NorthlightAutoNormalMemory::budget(300ull<<20,96ull<<20)==(32ull<<20));
#endif
    std::printf("CACHE_PERF_PASS first=%u revisit=%u pressureFallback=checked\n",counts[0],counts[1]);cache.report();ok(d->EndScene());cache.reset();for(auto*& source:sources)drop(source);d->Release();api->Release();DestroyWindow(window);return 0;
}
