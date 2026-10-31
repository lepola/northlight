#define NORTHLIGHT_NORMAL_REUSE_SUPPORT_ONLY
#include "auto_normal_reuse_gpu.cpp"
#undef NORTHLIGHT_NORMAL_REUSE_SUPPORT_ONLY
#include <thread>
#include "dxvk_compatibility.h"

struct DiskMeter:Meter {
    unsigned generated=0,readbacks=0,failedUploads=0,generatorCreates=0;bool queuedReadback=false,denyCapture=false,failUpload=false;
    explicit DiskMeter(IDirect3DDevice9* d):Meter(d){}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{if(code==kAutoNormalShader||code==kAutoNormalModelShader)++generatorCreates;return real->CreatePixelShader(code,out);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* data,UINT stride)override{++generated;return Meter::DrawPrimitiveUP(t,n,data,stride);}
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* a,IDirect3DSurface9* b)override{++readbacks;assert(queuedReadback);return real->GetRenderTargetData(a,b);}
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* a,const RECT* r,IDirect3DSurface9* b,const POINT* p)override{if(failUpload){failUpload=false;++failedUploads;return E_OUTOFMEMORY;}return real->UpdateSurface(a,r,b,p);}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE s,DWORD q,BOOL lockable,IDirect3DSurface9** out,HANDLE* shared)override{
        if(denyCapture&&lockable){*out=nullptr;return E_OUTOFMEMORY;}return real->CreateRenderTarget(w,h,f,s,q,lockable,out,shared);
    }
};
struct BusyState {bool query=true,lock=true;unsigned polls=0,locks=0;};
struct BusyQuery:IDirect3DQuery9 {
    IDirect3DQuery9* real;BusyState& state;ULONG refs=1;
    BusyQuery(IDirect3DQuery9* q,BusyState& s):real(q),state(s){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{if(--refs)return refs;real->Release();delete this;return 0;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d)override{return real->GetDevice(d);}
    D3DQUERYTYPE STDMETHODCALLTYPE GetType()override{return real->GetType();}
    DWORD STDMETHODCALLTYPE GetDataSize()override{return real->GetDataSize();}
    HRESULT STDMETHODCALLTYPE Issue(DWORD f)override{return real->Issue(f);}
    HRESULT STDMETHODCALLTYPE GetData(void* p,DWORD n,DWORD flags)override{assert(!flags);++state.polls;return state.query?S_FALSE:real->GetData(p,n,flags);}
};
struct BusySurface:IDirect3DSurface9 {
    IDirect3DSurface9* real;BusyState& state;ULONG refs=1;
    BusySurface(IDirect3DSurface9* p,BusyState& s):real(p),state(s){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{if(--refs)return refs;real->Release();delete this;return 0;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d)override{return real->GetDevice(d);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID)override{return E_NOTIMPL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD)override{return 0;}DWORD STDMETHODCALLTYPE GetPriority()override{return 0;}
    void STDMETHODCALLTYPE PreLoad()override{}D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_SURFACE;}
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void** out)override{*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* desc)override{return real->GetDesc(desc);}
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* p,const RECT* r,DWORD flags)override{assert(flags==(D3DLOCK_READONLY|D3DLOCK_DONOTWAIT));++state.locks;return state.lock?D3DERR_WASSTILLDRAWING:real->LockRect(p,r,flags);}
    HRESULT STDMETHODCALLTYPE UnlockRect()override{return real->UnlockRect();}
    HRESULT STDMETHODCALLTYPE GetDC(HDC*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE ReleaseDC(HDC)override{return E_NOTIMPL;}
};
struct MaterialDiskGpuTestAccess {
    static void busy(NorthlightAutoNormalCache& c,BusyState& state){assert(c.disk&&!c.disk->saves.empty());auto& s=*c.disk->saves.front();s.event=new BusyQuery(s.event,state);s.surface=new BusySurface(s.surface,state);}
};
static IDirect3DTexture9* sourceTexture(IDirect3DDevice9* d,const Image& image,const std::string& format){
    if(format=="rgba")return texture(d,image);
    const bool bc=format=="dxt1"||format=="dxt3"||format=="dxt5";
    const D3DFORMAT f=format=="dxt1"?D3DFMT_DXT1:format=="dxt3"?D3DFMT_DXT3:format=="dxt5"?D3DFMT_DXT5:format=="rgb565"?D3DFMT_R5G6B5:format=="argb4444"?D3DFMT_A4R4G4B4:format=="argb1555"?D3DFMT_A1R5G5B5:format=="xrgb1555"?D3DFMT_X1R5G5B5:D3DFMT_X8R8G8B8;
    const unsigned w=512,h=128;IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(w,h,1,0,f,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT lock{};ok(t->LockRect(0,&lock,nullptr,0));
    if(bc){const unsigned stride=format=="dxt1"?8:16;for(unsigned y=0;y<h/4;++y)for(unsigned x=0;x<w/4;++x){auto* p=static_cast<unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch+x*stride;std::memset(p,0,stride);if(stride==16){if(format=="dxt3")std::memset(p,255,8);else p[0]=p[1]=255;p+=8;}const WORD a=WORD(0xffffu-((x*19+y*37)&1023)),b=WORD((x*7+y*11)&1023);const DWORD indices=0xe4e4e4e4u;std::memcpy(p,&a,2);std::memcpy(p+2,&b,2);std::memcpy(p+4,&indices,4);}}
    else for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* p=static_cast<unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch;const WORD v=WORD(0x8000u|((x*31+y*17)&0x7fff));if(f==D3DFMT_X8R8G8B8){const DWORD c=0xff000000u|((x*13+y*19)&0xffffff);std::memcpy(p+x*4,&c,4);}else std::memcpy(p+x*2,&v,2);}
    ok(t->UnlockRect(0));return t;
}
static std::vector<unsigned char> mipSamples(IDirect3DDevice9* d,IDirect3DTexture9* map){
    float bank[1024];ok(d->GetVertexShaderConstantF(0,bank,256));SavedState saved(d);assert(saved.ok);
    IDirect3DSurface9 *target=nullptr,*readback=nullptr;ok(d->CreateRenderTarget(16,16,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,16,16,0,1};ok(d->SetViewport(&vp));
    ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetTexture(0,map));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,k,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));ok(d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0));
    for(auto k:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})ok(d->SetTextureStageState(0,k,D3DTOP_SELECTARG1));for(auto k:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})ok(d->SetTextureStageState(0,k,D3DTA_TEXTURE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{15.5f,-.5f,0,1,1,0},{-.5f,15.5f,0,1,0,1},{15.5f,15.5f,0,1,1,1}};std::vector<unsigned char> out;
    D3DSURFACE_DESC desc{};ok(map->GetLevelDesc(0,&desc));const float footprint=std::log2(float(std::max(desc.Width,desc.Height))/16);
    for(unsigned level:{0u,2u,4u,6u,8u}){const float bias=float(level)-footprint;DWORD bits;std::memcpy(&bits,&bias,4);ok(d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,bits));ok(d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<16;++y){const auto* p=static_cast<const unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch;out.insert(out.end(),p,p+64);}ok(readback->UnlockRect());}
    drop(readback);drop(target);ok(d->SetVertexShaderConstantF(0,bank,256));return out;
}
int main(int argc,char** argv){
    assert(argc>=4&&argc<=6);const std::string mode=argv[3],format=argc==6?argv[5]:"rgba";const std::filesystem::path root=std::filesystem::u8path(argv[2]);const bool basicCold=mode=="produce"||mode=="repair"||mode=="settings";
    if(std::strstr(argv[1],"dxvk")){const char* existing=std::getenv("DXVK_CONFIG");const auto config=NorthlightDxvkCompatibility::config(existing?existing:"");assert(SetEnvironmentVariableA("DXVK_CONFIG",config.c_str()));}
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));auto api=create(D3D_SDK_VERSION);assert(api);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    DiskMeter meter(raw);meter.queuedReadback=std::strstr(argv[1],"dxvk")!=nullptr;const char* backend=meter.queuedReadback?(std::strstr(argv[1],"dxvk2")?"dxvk2":"dxvk"):"native";unsigned checked=0,expectedGenerations=0,expectedFailures=0;
    {
        NorthlightAutoNormalCache cache(&meter),reference(&meter);cache.setPersistentCache(root/"maps",backend,nullptr,meter.queuedReadback);
        Image im{256,128,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(256*128*4)};
        if(argc>=5&&std::strlen(argv[4]))im=image(argv[4]);else for(size_t i=0;i<im.pixels.size();i+=4){im.pixels[i]=std::uint8_t(i/4*23);im.pixels[i+1]=std::uint8_t(i/19*13);im.pixels[i+2]=std::uint8_t(i/7*31);im.pixels[i+3]=255;}
        auto source=sourceTexture(raw,im,format);
        NorthlightQuality::Settings settings;if(mode=="settings"||mode=="settings-hit")settings.autoHeightPolarity=2;if(mode=="equivalent"){settings.autoHeightPolarity=1;settings.autoHeightDetails=17;settings.autoHeightBalance=70;}
        cache.configure(settings);reference.configure(settings);
        float constants[1024],restored[1024];for(unsigned i=0;i<1024;++i)constants[i]=float(i+1)*.00013f;
        std::vector<unsigned char> lastExpected,lastMips;
        for(bool model:{false,true}){
            const bool cold=basicCold||(mode=="equivalent"&&!model);expectedGenerations+=cold;expectedFailures+=!cold;
            ok(raw->SetVertexShaderConstantF(0,constants,256));ok(raw->BeginScene());auto fresh=reference.acquire(source,model);assert(fresh);ok(raw->EndScene());
            const auto expected=pixels(raw,fresh.get()),expectedMips=mipSamples(raw,fresh.get());lastExpected=expected;lastMips=expectedMips;reference.reset();reference.endFrame();
            const auto before=meter.generated,beforeShaders=meter.generatorCreates;NorthlightAutoNormalCache::Lease normal;BusyState busy;meter.failUpload=!cold;
            for(unsigned f=0;f<1200&&!normal;++f){
                ok(raw->SetVertexShaderConstantF(0,constants,256));ok(raw->BeginScene());normal=cache.acquire(source,model);ok(raw->EndScene());
                ok(raw->GetVertexShaderConstantF(0,restored,256));assert(!std::memcmp(constants,restored,sizeof constants));
                if(normal&&cold)MaterialDiskGpuTestAccess::busy(cache,busy);
                cache.endFrame();ok(raw->Present(nullptr,nullptr,nullptr,nullptr));std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            assert(normal);const auto actual=pixels(raw,normal.get());
            if(actual!=expected){size_t count=0,first=actual.size();unsigned peak=0;for(size_t i=0;i<actual.size();++i)if(actual[i]!=expected[i]){++count;first=std::min(first,i);peak=std::max(peak,unsigned(std::abs(int(actual[i])-expected[i])));}std::fprintf(stderr,"DISK_PIXEL_MISMATCH mode=%s model=%u generated=%u count=%zu first=%zu actual=%u expected=%u peak=%u\n",mode.c_str(),unsigned(model),meter.generated-before,count,first,unsigned(actual[first]),unsigned(expected[first]),peak);std::ofstream a(root/"actual.bin",std::ios::binary),e(root/"expected.bin",std::ios::binary);a.write(reinterpret_cast<const char*>(actual.data()),actual.size());e.write(reinterpret_cast<const char*>(expected.data()),expected.size());}assert(actual==expected);++checked;
            const auto work=meter.generated-before;
            std::printf("MATERIAL_DISK_GPU_MAP mode=%s model=%u generated=%u exactBytes=%zu\n",mode.c_str(),unsigned(model),work,actual.size());
            assert(work==(cold?1u:0u)&&meter.generatorCreates-beforeShaders==(cold?1u:0u));{const auto got=mipSamples(raw,normal.get());if(got!=expectedMips){for(unsigned n=0;n<5;++n){unsigned differences=0;for(unsigned j=0;j<1024;++j)differences+=got[n*1024+j]!=expectedMips[n*1024+j];std::fprintf(stderr,"MIP_MISMATCH mode=%s model=%u level=%u differences=%u actual=%u expected=%u\n",mode.c_str(),unsigned(model),n,differences,unsigned(got[n*1024]),unsigned(expectedMips[n*1024]));}}assert(got==expectedMips);}
            normal={};
            if(cold){
                const auto polls=busy.polls;
                for(unsigned f=0;f<8;++f){cache.endFrame();ok(raw->Present(nullptr,nullptr,nullptr,nullptr));}assert(busy.polls==polls+8&&!busy.locks);
                busy.query=false;
                for(unsigned f=0;f<8;++f){cache.endFrame();ok(raw->Present(nullptr,nullptr,nullptr,nullptr));std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(busy.locks>0);
                busy.lock=false;
            }
            // Finish asynchronous readback/write without waiting in production.
            for(unsigned f=0;f<60;++f){cache.endFrame();ok(raw->Present(nullptr,nullptr,nullptr,nullptr));std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            cache.report();cache.reset();cache.endFrame();
        }
        assert(meter.readbacks==(meter.queuedReadback?expectedGenerations:0u));assert(meter.failedUploads==expectedFailures);
        cache.reset();reference.reset();ok(raw->Reset(&pp));const auto beforeReset=meter.generated;
        NorthlightAutoNormalCache::Lease afterReset;
        for(unsigned f=0;f<1200&&!afterReset;++f){ok(raw->BeginScene());afterReset=cache.acquire(source,true);ok(raw->EndScene());cache.endFrame();ok(raw->Present(nullptr,nullptr,nullptr,nullptr));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(afterReset&&meter.generated==beforeReset&&pixels(raw,afterReset.get())==lastExpected&&mipSamples(raw,afterReset.get())==lastMips);afterReset={};drop(source);
    }
    drop(raw);drop(api);FreeLibrary(module);DestroyWindow(window);
    std::printf("MATERIAL_DISK_GPU_PASS mode=%s maps=%u blockingReadbacks=0 vsConstantsRestored=1 resetReload=1 mipLevels=5 busyBackoff=1 uploadRetry=1\n",mode.c_str(),checked);
}
