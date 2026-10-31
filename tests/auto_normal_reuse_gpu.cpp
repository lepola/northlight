// Real-device cache pressure: every recycled map must equal a fresh render.
#define main normal_reuse_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "forwarders.h"
#include "saved_state.h"
#include "auto_bump_compiled_shaders.h"
static BOOL cacheMemory(LPMEMORYSTATUSEX m){m->ullAvailVirtual=715ull<<20;return TRUE;}
#define GlobalMemoryStatusEx cacheMemory
#include "auto_normal_cache.h"
#undef GlobalMemoryStatusEx
struct Meter:ForwardIDirect3DDevice9 {
    unsigned allocations=0;bool failDraw=false;
    std::array<DWORD,8> pixelInputs{};
    explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD usage,D3DFORMAT f,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared)override{
        if(usage&D3DUSAGE_RENDERTARGET)++allocations;return real->CreateTexture(w,h,l,usage,f,pool,out,shared);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride)override{
        ok(real->GetPixelShaderConstantF(0,reinterpret_cast<float*>(pixelInputs.data()),2));
        if(failDraw){failDraw=false;return E_FAIL;}return real->DrawPrimitiveUP(type,count,data,stride);
    }
};
static std::vector<unsigned char> pixels(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    D3DSURFACE_DESC desc{};ok(texture->GetLevelDesc(0,&desc));IDirect3DSurface9 *surface=nullptr,*stage=nullptr;
    ok(texture->GetSurfaceLevel(0,&surface));ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(surface,stage));
    D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(size_t(desc.Width)*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(out.data()+size_t(y)*desc.Width*4,static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,desc.Width*4);
    ok(stage->UnlockRect());drop(stage);drop(surface);return out;
}
static void visibleSetStability(IDirect3DDevice9* d,const char* fixture){
    NorthlightAutoNormalCache cache(d),fresh(d);std::vector<IDirect3DTexture9*> sources;
    for(unsigned i=0;i<32;++i){
        Image im{512,512,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(512*512*4)};
        if(!i&&fixture)im=image(fixture);
        else for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x){const DWORD v=0xff000000u|(((x*13+y*7+i*31)&255)<<16)|(((x*3+y*17+i*19)&255)<<8)|((x*23+y*5+i*11)&255);std::memcpy(im.pixels.data()+(y*512+x)*4,&v,4);}
        sources.push_back(texture(d,im));
    }
    std::array<bool,32> admitted{};std::array<IDirect3DTexture9*,32> maps{};unsigned checked=0;
    for(unsigned f=0;f<96;++f){
        ok(d->BeginScene());
        for(unsigned j=0;j<32;++j){const unsigned i=f<48?j:(f&1?31-j:(j+f)%32);auto map=cache.acquire(sources[i],i%2==0);
            if(f==47){admitted[i]=bool(map);maps[i]=map.get();}
            if(f>=48){assert(bool(map)==admitted[i]);if(map)assert(map.get()==maps[i]);}
        }
        ok(d->EndScene());cache.endFrame();
    }
    assert(admitted[0]);const auto count=std::count(admitted.begin(),admitted.end(),true);assert(count>0&&count<32);
    // Every admitted map still equals a freshly rendered production map.
    for(unsigned i=0;i<32;++i)if(admitted[i]){ok(d->BeginScene());auto map=cache.acquire(sources[i],i%2==0);auto reference=fresh.acquire(sources[i],i%2==0);assert(map&&reference);ok(d->EndScene());assert(pixels(d,map.get())==pixels(d,reference.get()));++checked;fresh.reset();fresh.endFrame();}
    cache.reset();for(auto*& source:sources)drop(source);
    std::printf("NORMAL_VISIBLE_SET_PASS frames=96 visible=32 resident=%u transitions=0 exactMaps=%u honorHold=%u mixedModelWorld=1\n",unsigned(count),checked,unsigned(fixture!=nullptr));
}
#ifndef NORTHLIGHT_NORMAL_REUSE_SUPPORT_ONLY
int main(int argc,char** argv){
    assert(argc==2||argc==3);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    unsigned compared=0,churnAllocations=0;
    {
    Meter device(raw);NorthlightAutoNormalCache cache(&device),reference(&device);std::vector<IDirect3DTexture9*> sources;
    std::vector<std::vector<unsigned char>> expected;std::vector<std::array<DWORD,8>> expectedInputs;
    for(unsigned i=0;i<31;++i){const unsigned height=i==30?256:512;IDirect3DTexture9* source=nullptr;
        ok(raw->CreateTexture(512,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr));D3DLOCKED_RECT lock{};ok(source->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<512;++x){const DWORD value=0xff000000u|(((x*13+y*7+i*31)&255)<<16)|(((x*3+y*17+i*19)&255)<<8)|((x*23+y*5+i*11)&255);std::memcpy(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch+x*4,&value,4);}
        ok(source->UnlockRect(0));sources.push_back(source);
    }
    float constants[256*4],restored[256*4];for(unsigned k=0;k<256*4;++k)constants[k]=float(k+1)*.00013f;
    for(bool model:{false,true}){
        expected.clear();expectedInputs.clear();cache.reset();reference.reset();
        for(unsigned i=0;i<30;++i){ok(raw->SetVertexShaderConstantF(0,constants,256));ok(raw->BeginScene());auto normal=reference.acquire(sources[i],model);assert(normal);ok(raw->EndScene());expected.push_back(pixels(raw,normal.get()));expectedInputs.push_back(device.pixelInputs);reference.reset();reference.endFrame();}
        assert(expected[0]!=expected[1]);
        const unsigned before=device.allocations;
        for(unsigned round=0;round<3;++round)for(unsigned i=0;i<30;++i){
            ok(raw->SetVertexShaderConstantF(0,constants,256));
            ok(raw->BeginScene());auto normal=cache.acquire(sources[i],model);assert(normal);ok(raw->EndScene());
            if(device.pixelInputs!=expectedInputs[i]){std::fprintf(stderr,"NORMAL_REUSE_INPUT_MISMATCH model=%u round=%u source=%u\n",unsigned(model),round,i);for(unsigned k=0;k<8;++k)std::fprintf(stderr,"c%u %08lx %08lx\n",k,static_cast<unsigned long>(expectedInputs[i][k]),static_cast<unsigned long>(device.pixelInputs[k]));}assert(device.pixelInputs==expectedInputs[i]);
            ok(raw->GetVertexShaderConstantF(0,restored,256));assert(!std::memcmp(constants,restored,sizeof constants));
            const auto actual=pixels(raw,normal.get());if(actual!=expected[i]){unsigned differences=0,peak=0;for(size_t k=0;k<actual.size();++k)if(actual[k]!=expected[i][k]){++differences;peak=std::max(peak,unsigned(std::abs(int(actual[k])-expected[i][k])));}std::fprintf(stderr,"NORMAL_REUSE_MISMATCH model=%u round=%u source=%u differences=%u peak=%u allocations=%u\n",unsigned(model),round,i,differences,peak,device.allocations-before);}assert(actual==expected[i]);++compared;cache.endFrame();
        }
#ifdef NORMAL_REUSE_BASELINE
        assert(device.allocations-before==90);
#else
        assert(device.allocations-before==24);
#endif
        churnAllocations+=device.allocations-before;
    }
    ok(raw->BeginScene());
    auto fill=[&](){cache.reset();cache.endFrame();for(unsigned i=0;i<24;++i){assert(cache.resolve(sources[i],true));cache.endFrame();}};
    fill();
    {
        auto* old=cache.resolve(sources[0],true);auto saved=pixels(raw,old);for(unsigned i=1;i<24;++i)assert(cache.resolve(sources[i],true));
        IDirect3DSurface9 *previous=nullptr,*surface=nullptr;ok(raw->GetRenderTarget(0,&previous));ok(old->GetSurfaceLevel(0,&surface));ok(raw->SetRenderTarget(0,surface));
        assert(!cache.resolve(sources[24],true));cache.endFrame();cache.endFrame();
        const unsigned before=device.allocations;assert(cache.resolve(sources[24],true));assert(device.allocations==before+1);assert(pixels(raw,old)==saved);ok(raw->SetRenderTarget(0,previous));drop(surface);drop(previous);
    }
    fill();
    {
        auto held=cache.acquire(sources[0],true);auto old=pixels(raw,held.get());
        for(unsigned i=1;i<24;++i)assert(cache.resolve(sources[i],true));
        assert(!cache.resolve(sources[24],true));cache.endFrame();cache.endFrame();
        const unsigned before=device.allocations;auto next=cache.acquire(sources[24],true);assert(next&&next.get()!=held.get());assert(device.allocations==before+1);assert(pixels(raw,held.get())==old);
        cache.reset();assert(pixels(raw,held.get())==old);
    }
    fill();
    {
        auto* old=cache.resolve(sources[0],true);auto saved=pixels(raw,old);for(unsigned i=1;i<24;++i)assert(cache.resolve(sources[i],true));
        ok(raw->SetTexture(3,old));assert(!cache.resolve(sources[24],true));cache.endFrame();cache.endFrame();const unsigned before=device.allocations;assert(cache.resolve(sources[24],true));assert(device.allocations==before+1);assert(pixels(raw,old)==saved);ok(raw->SetTexture(3,nullptr));
    }
    fill();
    {
        NorthlightAutoNormalCache::Lease held[9];for(unsigned i=0;i<8;++i)held[i]=cache.acquire(sources[23],true);held[8]=cache.acquire(sources[22],true);auto old=pixels(raw,held[8].get());
        for(unsigned i=0;i<22;++i)assert(cache.resolve(sources[i],true));assert(cache.resolve(sources[23],true));
        assert(!cache.resolve(sources[24],true));cache.endFrame();cache.endFrame();
        const unsigned before=device.allocations;auto next=cache.acquire(sources[24],true);assert(next&&device.allocations==before+1);assert(pixels(raw,held[8].get())==old);
    }
    fill();
    {const unsigned before=device.allocations;assert(cache.resolve(sources[30],true));assert(device.allocations==before+1);}
    fill();device.failDraw=true;assert(!cache.resolve(sources[24],true));cache.endFrame();auto* recovered=cache.resolve(sources[24],true);assert(recovered&&pixels(raw,recovered)==expected[24]);ok(raw->EndScene());
    cache.reset();for(auto*& source:sources)drop(source);
    }
    visibleSetStability(raw,argc==3?argv[2]:nullptr);
    raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("NORMAL_REUSE_PASS compared=%u churnAllocations=%u freshAllocations=180 lease=checked bound=checked renderTarget=checked overflow=checked resize=checked failedDraw=checked reset=checked\n",compared,churnAllocations);
}

#endif
