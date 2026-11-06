// Production normal-map generation: model profiles need no CPU source read.
#define main normal_profile_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <chrono>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "forwarders.h"
#include "mirror_resource_forwarders.h"
#include "saved_state.h"
#include "auto_bump_compiled_shaders.h"
#include "auto_normal_cache.h"
struct SourceMeter:NorthlightMirrorResources::ForwardIDirect3DTexture9 {
    unsigned reads=0;size_t bytes=0;
    SourceMeter(IDirect3DTexture9* texture,MirrorGate& gate):ForwardIDirect3DTexture9(texture,gate,[](void*,const char*){},nullptr){}
    HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* lock,const RECT* rect,DWORD flags)override{
        if(flags&D3DLOCK_READONLY){D3DSURFACE_DESC desc{};ok(real->GetLevelDesc(level,&desc));++reads;bytes+=size_t(desc.Width)*desc.Height*4;}
        return real->LockRect(level,lock,rect,flags);
    }
};
struct SourceDevice:ForwardIDirect3DDevice9 {
    SourceMeter* source=nullptr;
    explicit SourceDevice(IDirect3DDevice9* device):ForwardIDirect3DDevice9(device){}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* texture)override{
        return real->SetTexture(stage,source&&texture==source?source->real:texture);
    }
};
static void saveNormal(IDirect3DDevice9* d,IDirect3DTexture9* normal,const std::string& path){
    IDirect3DSurface9 *surface=nullptr,*stage=nullptr;D3DSURFACE_DESC desc{};
    ok(normal->GetSurfaceLevel(0,&surface));ok(surface->GetDesc(&desc));
    ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(surface,stage));
    D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::ofstream out(path,std::ios::binary);assert(out);for(UINT y=0;y<desc.Height;++y)out.write(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,desc.Width*4);
    ok(stage->UnlockRect());drop(stage);drop(surface);
}
int main(int argc,char** argv){
    assert(argc==4);const bool before=std::strcmp(argv[3],"baseline")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightNormalProfile";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    unsigned cases=0,modelReads=0,worldReads=0;size_t modelBytes=0;double modelMs=0;
    {
    SourceDevice device(raw);NorthlightAutoNormalCache cache(&device);
    for(unsigned width:{32u,128u,256u}){
        const unsigned height=width/2;IDirect3DTexture9* texture=nullptr;ok(raw->CreateTexture(width,height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        for(UINT level=0;level<texture->GetLevelCount();++level){D3DSURFACE_DESC desc{};ok(texture->GetLevelDesc(level,&desc));D3DLOCKED_RECT lock{};ok(texture->LockRect(level,&lock,nullptr,0));
            for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){const DWORD value=0xff000000u|(((x*13+y*7+level*31)&255)<<16)|(((x*3+y*17)&255)<<8)|((x*23+y*5)&255);std::memcpy(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch+x*4,&value,4);}ok(texture->UnlockRect(level));}
        MirrorGate gate;SourceMeter source(texture,gate);device.source=&source;
        for(unsigned polarity=0;polarity<3;++polarity)for(bool model:{false,true}){
            cache.reset();cache.endFrame();NorthlightQuality::Settings settings;settings.autoHeightPolarity=polarity;settings.autoHeightDetails=83;settings.autoHeightBalance=61;cache.configure(settings);
            float constants[256*4],restored[256*4];for(unsigned i=0;i<256*4;++i)constants[i]=float(i+1)*.00013f;ok(raw->SetVertexShaderConstantF(0,constants,256));
            source.reads=0;source.bytes=0;ok(raw->BeginScene());const auto start=std::chrono::steady_clock::now();auto* normal=cache.resolve(&source,model);const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();ok(raw->EndScene());assert(normal);
            assert(source.reads==(model&&!before?0u:1u));
            if(model){modelReads+=source.reads;modelBytes+=source.bytes;modelMs+=elapsed;}else worldReads+=source.reads;
            ok(raw->GetVertexShaderConstantF(0,restored,256));assert(!std::memcmp(constants,restored,sizeof constants));
            saveNormal(raw,normal,std::string(argv[2])+"/"+std::to_string(width)+"-"+std::to_string(polarity)+"-"+(model?"model":"world")+".rgba");
            ++cases;
        }
        cache.reset();device.source=nullptr;drop(texture);
    }
    }
    raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("NORMAL_PROFILE_PASS cases=%u modelSourceReads=%u modelSourceBytes=%zu worldSourceReads=%u modelBuildMs=%.3f baseline=%u\n",cases,modelReads,modelBytes,worldReads,modelMs,unsigned(before));
}
