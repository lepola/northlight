// Authored transport only. The EXE and transparent test DLL use this one TU.
// This does not execute the imported client image or establish game quality/FPS.
#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef NORTHLIGHT_TRANSPORT_PROXY_DLL
#include "forwarders.h"
#include "game_snapshot.h"
#include "native_snapshot_capture.h"
#include "backend_loader.h"

namespace {
HMODULE ownModule=nullptr,activeBackend=nullptr;
std::filesystem::path environmentPath(const wchar_t* name){
    wchar_t text[4096];const DWORD size=GetEnvironmentVariableW(name,text,4096);
    if(!size||size>=4096)throw std::runtime_error("Missing authored transport path");
    std::filesystem::path value(std::wstring(text,size));
    if(!value.is_absolute())throw std::runtime_error("Authored transport paths must be absolute");
    return value;
}
std::filesystem::path modulePath(HMODULE module){
    wchar_t text[4096];const DWORD size=GetModuleFileNameW(module,text,4096);
    if(!size||size>=4096)throw std::runtime_error("Cannot identify authored transport module");
    return std::filesystem::path(std::wstring(text,size));
}
std::string hash(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot hash transport input");
    NorthlightBackendLoader::Sha256 sha;char bytes[16384];
    while(input.read(bytes,sizeof bytes)||input.gcount())sha.update(bytes,std::size_t(input.gcount()));
    if(!input.eof())throw std::runtime_error("Transport input read failed");return sha.hex();
}
std::string json(const std::filesystem::path& path){
    std::string out="\"";for(unsigned char c:path.u8string()){
        if(c<32)throw std::runtime_error("Control character in transport path");
        if(c=='\\'||c=='\"')out+='\\';out+=char(c);
    }return out+'\"';
}
class Capture {
    using Marker=void(WINAPI*)(D3DCOLOR,LPCWSTR);
    NorthlightStream::NativeSnapshotCapture capture_;
    HMODULE wrapper_=nullptr;Marker marker_=nullptr;
    NorthlightStream::GameSnapshot snapshot_;
    static bool marker(void* context,const wchar_t* text,unsigned length) noexcept {
        auto& self=*static_cast<Capture*>(context);
        if(!text||!length||length>=NorthlightStream::NativeSnapshotCapture::MarkerChars||text[length])return false;
        self.marker_(0x4e4c0001,text);return true;
    }
public:
    Capture(){try{
        const HMODULE outer=GetModuleHandleW(L"d3d9.dll");
        if(!outer||outer==ownModule)throw std::runtime_error("Official outer wrapper is required");
        const auto procedure=GetProcAddress(outer,"D3DPERF_SetMarker");
        if(!procedure||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(procedure),&wrapper_)||wrapper_!=outer)
            throw std::runtime_error("Cannot pin the outer marker implementation");
        static_assert(sizeof marker_==sizeof procedure,"Win32 function pointer representation");
        std::memcpy(&marker_,&procedure,sizeof procedure);
        const auto root=environmentPath(L"NORTHLIGHT_TRANSPORT_SNAPSHOT_DIR");
        if(!capture_.open(root,"authored-transport",&Capture::marker,this))throw std::runtime_error("Cannot open authored snapshot sink");
        const auto producer=modulePath(nullptr),wrapper=modulePath(wrapper_),proxy=modulePath(ownModule);
        const auto client=environmentPath(L"NORTHLIGHT_TRANSPORT_CLIENT_PE"),trace=environmentPath(L"TRACE_FILE");
        const auto downstream=environmentPath(L"APITRACE_FORCE_MODULE_PATH");
        std::ofstream metadata(root/L"capture-host.json",std::ios::binary);
        if(!metadata)throw std::runtime_error("Cannot save authored transport identity");
        metadata<<"{\"format\":1,\"session\":\"authored-transport\",\"capture_layer\":\"above-northlight\",\"diagnostic\":true,\"fps_eligible\":false,\"trace_join_validated\":false,\"authored_transport\":true,"
            <<"\"producer_kind\":\"transparent-authored-proxy\",\"wrapper_path\":"<<json(wrapper)<<",\"wrapper_sha256\":\""<<hash(wrapper)
            <<"\",\"renderer_path\":"<<json(proxy)<<",\"renderer_sha256\":\""<<hash(proxy)
            <<"\",\"client_path\":"<<json(producer)<<",\"client_sha256\":\""<<hash(producer)
            <<"\",\"producer_exe_path\":"<<json(producer)<<",\"producer_exe_sha256\":\""<<hash(producer)
            <<"\",\"imported_client_pe_path\":"<<json(client)<<",\"imported_client_pe_sha256\":\""<<hash(client)
            <<"\",\"trace_path\":"<<json(trace)<<",\"force_module_path\":"<<json(downstream)<<"}\n";
        metadata.close();if(!metadata)throw std::runtime_error("Authored metadata write failed");
    }catch(...){capture_.finish();if(wrapper_){FreeLibrary(wrapper_);wrapper_=nullptr;}throw;}}
    ~Capture(){capture_.finish();if(wrapper_)FreeLibrary(wrapper_);}
    void draw(std::uintptr_t device,std::uint64_t frame,std::uint32_t epoch,std::uint64_t ordinal,std::uint32_t texture){
        snapshot_.clear();snapshot_.triggerKind=1;snapshot_.triggerDraw=ordinal;
        // These are authored read outcomes, never reads of either process at
        // these numeric addresses. The actual pure readers determine the FIFO.
        auto read=[&](std::uintptr_t address,void* output,std::size_t bytes){
            constexpr std::uint32_t handle=0x30000000,gx=0x30000100;
            bool ok=false;
            if(address==0xd38e38&&bytes==4){std::memcpy(output,&handle,4);ok=true;}
            else if(address==handle+0x20&&bytes==0x40){std::memset(output,0,bytes);std::memcpy(static_cast<unsigned char*>(output)+0x24,&gx,4);ok=true;}
            else if(address==gx+0x38&&bytes==4){std::memcpy(output,&texture,4);ok=true;}
            if(!snapshot_.record(address,output,bytes,ok))throw std::runtime_error("Authored snapshot capacity exceeded");return ok;
        };
        std::uint32_t failed=0;read(0xb7436c,&failed,4); // readMapAndCamera's independently recorded failure.
        NorthlightStream::captureReaders(read);
        NorthlightWeatherMemory::Reading weather;NorthlightWeatherMemory::readState(read,weather);
        const auto tid=GetCurrentThreadId();
        NorthlightStream::NativeSnapshotCapture::completed(&capture_,snapshot_,device,frame,epoch,tid);
        NorthlightStream::NativeSnapshotCapture::observed(&capture_,snapshot_,device,frame,epoch,tid);
    }
};
class Factory;
class Device final:public ForwardIDirect3DDevice9 {
    LONG refs_=1;Factory* parent_;std::shared_ptr<Capture> capture_;
    std::uint64_t frame_=0,draw_=0;std::uint32_t epoch_=0;bool first_=true;
    void beforeDraw(){
        ++draw_;if(!first_)return;first_=false;IDirect3DBaseTexture9* texture=nullptr;
        if(FAILED(real->GetTexture(0,&texture))||!texture)throw std::runtime_error("Authored Draw lacks its actual source texture");
        try{capture_->draw(reinterpret_cast<std::uintptr_t>(this),frame_,epoch_,draw_,std::uint32_t(reinterpret_cast<std::uintptr_t>(texture)));}
        catch(...){texture->Release();throw;}texture->Release();
    }
public:
    Device(IDirect3DDevice9* real,Factory* parent,std::shared_ptr<Capture> capture);
    ~Device();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output) override {
        if(!output)return E_POINTER;*output=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DDevice9)){*output=this;AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{const auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** output) override;
    HRESULT STDMETHODCALLTYPE Present(const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty) override{
        const auto hr=real->Present(source,destination,window,dirty);if(SUCCEEDED(hr)){++frame_;first_=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override{const auto hr=real->Reset(pp);if(SUCCEEDED(hr)){++epoch_;first_=true;}return hr;}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type,UINT first,UINT count) override{beforeDraw();return real->DrawPrimitive(type,first,count);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT first,UINT count) override{beforeDraw();return real->DrawIndexedPrimitive(type,base,min,vertices,first,count);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* vertices,UINT stride) override{beforeDraw();return real->DrawPrimitiveUP(type,count,vertices,stride);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride) override{beforeDraw();return real->DrawIndexedPrimitiveUP(type,min,vertices,count,indices,format,data,stride);}
};
class Factory final:public ForwardIDirect3D9 {
    LONG refs_=1;HMODULE backend_;std::shared_ptr<Capture> capture_;
public:
    Factory(IDirect3D9* real,HMODULE backend,std::shared_ptr<Capture> capture):ForwardIDirect3D9(real),backend_(backend),capture_(std::move(capture)){}
    ~Factory(){real->Release();capture_.reset();activeBackend=nullptr;FreeLibrary(backend_);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output) override{
        if(!output)return E_POINTER;*output=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3D9)){*output=this;AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{const auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** output) override{
        if(!output)return E_POINTER;*output=nullptr;IDirect3DDevice9* device=nullptr;
        const auto hr=real->CreateDevice(adapter,type,focus,flags,pp,&device);if(FAILED(hr))return hr;
        try{*output=new Device(device,this,capture_);return hr;}catch(...){device->Release();return E_OUTOFMEMORY;}
    }
};
Device::Device(IDirect3DDevice9* real,Factory* parent,std::shared_ptr<Capture> capture):ForwardIDirect3DDevice9(real),parent_(parent),capture_(std::move(capture)){parent_->AddRef();}
Device::~Device(){real->Release();parent_->Release();}
HRESULT STDMETHODCALLTYPE Device::GetDirect3D(IDirect3D9** output){if(!output)return E_POINTER;*output=parent_;parent_->AddRef();return S_OK;}
}
extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk){
    HMODULE backend=nullptr;IDirect3D9* api=nullptr;
    try{
        if(activeBackend)throw std::runtime_error("Only one authored factory is supported");
        backend=LoadLibraryExW(environmentPath(L"NORTHLIGHT_TRANSPORT_BACKEND").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!backend)throw std::runtime_error("Cannot load private transport backend");
        const auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(backend,"Direct3DCreate9"));
        if(!create||(api=create(sdk))==nullptr)throw std::runtime_error("Private transport factory failed");
        auto capture=std::make_shared<Capture>();auto* factory=new Factory(api,backend,std::move(capture));activeBackend=backend;return factory;
    }catch(const std::exception& error){std::fprintf(stderr,"AUTHORED_TRANSPORT_FACTORY %s\n",error.what());if(api)api->Release();if(backend)FreeLibrary(backend);return nullptr;}
}
extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR color,LPCWSTR text){
    using Marker=void(WINAPI*)(D3DCOLOR,LPCWSTR);
    const auto marker=activeBackend?reinterpret_cast<Marker>(GetProcAddress(activeBackend,"D3DPERF_SetMarker")):nullptr;
    if(!marker){std::fputs("AUTHORED_TRANSPORT marker backend unavailable\n",stderr);std::exit(EXIT_FAILURE);}marker(color,text);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)ownModule=module;return TRUE;}

#else
// Reuse the exact saved-image/query/shader helpers; the old main and its failed
// six-frame Reset gate remain unchanged and are never called by this fixture.
#define main originalSceneTraceMain
#include "native_scene_trace_smoke.cpp"
#undef main

static thread_local HWND resizingWindow=nullptr;
static thread_local SIZE resizingOuter{};
static LRESULT CALLBACK transportWindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    const auto result=DefWindowProcA(window,message,wParam,lParam);
    if(message==WM_GETMINMAXINFO&&window==resizingWindow){
        auto& limits=*reinterpret_cast<MINMAXINFO*>(lParam);
        std::fprintf(stderr,"CLIENT_RESIZE_LIMITS requestedOuter=%ldx%ld defaultMin=%ldx%ld defaultMax=%ldx%ld\n",resizingOuter.cx,resizingOuter.cy,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,limits.ptMaxTrackSize.x,limits.ptMaxTrackSize.y);
        // Allow this owned hidden fixture's exact size, only during its explicit resize.
        if(limits.ptMinTrackSize.x>resizingOuter.cx)limits.ptMinTrackSize.x=resizingOuter.cx;
        if(limits.ptMinTrackSize.y>resizingOuter.cy)limits.ptMinTrackSize.y=resizingOuter.cy;
        std::fprintf(stderr,"CLIENT_RESIZE_LIMITS appliedMin=%ldx%ld\n",limits.ptMinTrackSize.x,limits.ptMinTrackSize.y);
        return 0;
    }
    return result;
}
static void windowGeometry(HWND window,const char* stage){
    RECT outer{},client{};SetLastError(0);const BOOL gotOuter=GetWindowRect(window,&outer);const auto outerError=GetLastError();
    SetLastError(0);const BOOL gotClient=GetClientRect(window,&client);const auto clientError=GetLastError();
    std::fprintf(stderr,"CLIENT_GEOMETRY %s style=%08lx exstyle=%08lx window=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld rectOK=%u/%u rectError=%lu/%lu systemMinTrack=%dx%d menu=%u\n",stage,(unsigned long)GetWindowLongPtrW(window,GWL_STYLE),(unsigned long)GetWindowLongPtrW(window,GWL_EXSTYLE),outer.left,outer.top,outer.right,outer.bottom,client.left,client.top,client.right,client.bottom,unsigned(gotOuter),unsigned(gotClient),(unsigned long)outerError,(unsigned long)clientError,GetSystemMetrics(SM_CXMINTRACK),GetSystemMetrics(SM_CYMINTRACK),unsigned(GetMenu(window)!=nullptr));
    assert(gotOuter&&gotClient);
}
static void resizeClient(HWND window,unsigned width,unsigned height){
    windowGeometry(window,"before-resize");
    RECT desired{0,0,LONG(width),LONG(height)};SetLastError(0);
    const BOOL adjusted=AdjustWindowRectEx(&desired,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),GetMenu(window)!=nullptr,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)));const auto adjustError=GetLastError();
    std::fprintf(stderr,"CLIENT_RESIZE_ADJUST requestedClient=%ux%u adjusted=%u error=%lu outer=%ld,%ld,%ld,%ld\n",width,height,unsigned(adjusted),(unsigned long)adjustError,desired.left,desired.top,desired.right,desired.bottom);assert(adjusted);
    assert(!resizingWindow);resizingOuter={desired.right-desired.left,desired.bottom-desired.top};assert(resizingOuter.cx>0&&resizingOuter.cy>0);resizingWindow=window;SetLastError(0);
    const BOOL moved=SetWindowPos(window,nullptr,0,0,resizingOuter.cx,resizingOuter.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);const auto moveError=GetLastError();resizingWindow=nullptr;
    std::fprintf(stderr,"CLIENT_RESIZE_SET result=%u error=%lu requestedOuter=%ldx%ld\n",unsigned(moved),(unsigned long)moveError,resizingOuter.cx,resizingOuter.cy);
    windowGeometry(window,"after-resize");assert(moved);
    RECT actual{};assert(GetClientRect(window,&actual));assert(actual.right==LONG(width)&&actual.bottom==LONG(height));
}
static void verifyMarkerState(IDirect3DDevice9* device,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DTexture9* texture,const D3DVIEWPORT9& viewport){
    IDirect3DVertexShader9* activeVS=nullptr;IDirect3DPixelShader9* activePS=nullptr;IDirect3DBaseTexture9* activeTexture=nullptr;
    ok(device->GetVertexShader(&activeVS));ok(device->GetPixelShader(&activePS));ok(device->GetTexture(0,&activeTexture));
    assert(activeVS==vs&&activePS==ps&&activeTexture==texture);activeVS->Release();activePS->Release();activeTexture->Release();
    D3DVIEWPORT9 activeViewport{};ok(device->GetViewport(&activeViewport));assert(!std::memcmp(&activeViewport,&viewport,sizeof viewport));
    DWORD zEnable=0,zWrite=0;ok(device->GetRenderState(D3DRS_ZENABLE,&zEnable));ok(device->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite));assert(zEnable==TRUE&&zWrite==TRUE);
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);const std::string output=argv[1];const bool resized=argc==3&&!std::strcmp(argv[2],"reset-resize");
    const bool precopy=argc==3&&!std::strcmp(argv[2],"reset-precopy-readback");
    const bool reset=resized||precopy||(argc==3&&!std::strcmp(argv[2],"reset-unresized"));assert(argc==2||reset);
    WNDCLASSA cls{};cls.lpfnWndProc=reset?transportWindowProc:DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="NorthlightAuthoredTransport";assert(RegisterClassA(&cls));
    HWND window=CreateWindowA(cls.lpszClassName,"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);assert(window&&!IsWindowVisible(window));
    HMODULE module=LoadLibraryA("d3d9.dll");assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=96;pp.BackBufferHeight=80;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const auto loaded=modules();unsigned images=0,occlusions=0;
    for(unsigned round=0;round<(reset?2u:1u);++round){
        if(round){pp.BackBufferWidth=112;pp.BackBufferHeight=96;windowGeometry(window,"before-reset-control");if(resized)resizeClient(window,112,96);parameters(resized?"authored-client-resized-before-reset":"authored-client-unresized-before-reset",d,window,pp);ok(d->Reset(&pp));}
        const unsigned w=pp.BackBufferWidth,h=pp.BackBufferHeight;IDirect3DSurface9 *target=nullptr,*depth=nullptr,*back=nullptr;IDirect3DTexture9* tex=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DQuery9 *event=nullptr,*occlusion=nullptr;
        ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));ok(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->CreateTexture(2,2,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,nullptr));
        const Vertex quad[]={{-.93f,.87f,.4f,1,0,0},{.91f,.87f,.4f,1,1,0},{-.93f,-.89f,.4f,1,0,1},{.91f,-.89f,.4f,1,1,1}};const WORD indices[]={0,1,2,2,1,3};
        ok(d->CreateVertexBuffer(sizeof quad,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr));ok(d->CreateIndexBuffer(sizeof indices,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
        void* data=nullptr;ok(vb->Lock(0,sizeof quad,&data,D3DLOCK_DISCARD));std::memcpy(data,quad,sizeof quad);ok(vb->Unlock());ok(ib->Lock(0,sizeof indices,&data,D3DLOCK_DISCARD));std::memcpy(data,indices,sizeof indices);ok(ib->Unlock());
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        ok(d->CreateVertexDeclaration(elements,&decl));ok(d->CreateVertexShader(vsCode,&vs));ok(d->CreatePixelShader(psCode,&ps));ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&occlusion));
        for(unsigned frame=0;frame<3;++frame){
            D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));const DWORD color=D3DCOLOR_ARGB(255,64+frame*35,112+round*20,192-frame*25);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&color,4);ok(tex->UnlockRect(0));
            ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 viewport{0,0,w,h,0,1};ok(d->SetViewport(&viewport));
            ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_DITHERENABLE,FALSE));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
            ok(d->SetTexture(0,tex));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
            ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));const float white[]={1,1,1,1};ok(d->SetPixelShaderConstantF(0,white,1));ok(d->SetStreamSource(0,vb,0,sizeof(Vertex)));ok(d->SetIndices(ib));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0));ok(d->BeginScene());ok(occlusion->Issue(D3DISSUE_BEGIN));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
            verifyMarkerState(d,vs,ps,tex,viewport);ok(occlusion->Issue(D3DISSUE_END));ok(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));
            Vertex farQuad[4];std::memcpy(farQuad,quad,sizeof quad);for(auto& v:farQuad)v.z=.95f;const float green[]={0,1,0,1};ok(d->SetPixelShaderConstantF(0,green,1));
            ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,farQuad,sizeof(Vertex)));ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,farQuad,sizeof(Vertex)));ok(d->EndScene());
            DWORD samples=0;wait(occlusion,&samples,sizeof samples);assert(samples);++occlusions;ok(event->Issue(D3DISSUE_END));BOOL finished=FALSE;wait(event,&finished,sizeof finished);assert(finished);
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,back));
            if(precopy&&round==1&&frame==0){
                std::fprintf(stderr,"PRECOPY_TARGET_READBACK_BEGIN diagnosticOnly=1 round=%u frame=%u\n",round,frame);
                save(d,target,w,h,color,output+"/precopy-target.ppm",0,round,frame,nullptr);
                std::fputs("PRECOPY_TARGET_READBACK_PASS originalResetGatePassed=0\n",stderr);
            }
            ok(d->StretchRect(target,nullptr,back,nullptr,D3DTEXF_NONE));
            char name[64];std::snprintf(name,sizeof name,"/frame%06u.ppm",images);save(d,back,w,h,color,output+name,0,round,frame,target);++images;ok(d->Present(nullptr,nullptr,nullptr,nullptr));
        }
        ok(d->SetTexture(0,nullptr));ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));ok(d->SetVertexDeclaration(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));
        event->Release();occlusion->Release();vs->Release();ps->Release();decl->Release();vb->Release();ib->Release();tex->Release();target->Release();depth->Release();back->Release();
    }
    assert(images==(reset?6u:3u)&&occlusions==images);assert(d->Release()==0);api->Release();DestroyWindow(window);FreeLibrary(module);
    std::ofstream report(output+"/authored-source.json");report<<"{\"format\":1,\"authored_transport\":true,\"game_launched\":false,\"fps_eligible\":false,\"full_scene_ready\":false,\"raw_depth_equivalence_validated\":false,\"frames\":"<<images<<",\"draws\":"<<images*4<<",\"nonzero_occlusions\":"<<occlusions<<",\"event_completions\":"<<images<<",\"reset_requested\":"<<(reset?"true":"false")<<",\"reset_client_resize_control\":"<<(resized?"true":"false")<<",\"precopy_target_readback_control\":"<<(precopy?"true":"false")<<",\"modules\":"<<loaded<<"}\n";report.close();assert(report);
    std::puts("PASS authored transport producer: actual Draw/query/color/state sequence; original game quality/FPS unproven");
}
#endif
