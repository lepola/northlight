// Compare presentation from native MSAA with an explicit resolve. No game DLL.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_hdr_present_helpers
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_output.h"
#include "hdr_frame.h"
#include "hdr_color.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-exception-spec"
#include "forwarders.h"
#pragma clang diagnostic pop
namespace ReferenceHdr {using namespace NorthlightHdr;}
#define NorthlightHdr ReferenceHdr
#include "hdr_output_before.h"
#undef NorthlightHdr

static std::vector<unsigned char> pixels(IDirect3DDevice9* d,IDirect3DSurface9* source){
    D3DSURFACE_DESC desc{};ok(source->GetDesc(&desc));assert(desc.MultiSampleType==D3DMULTISAMPLE_NONE);
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->GetRenderTargetData(source,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> data(size_t(desc.Width)*desc.Height*8);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(data.data()+size_t(y)*desc.Width*8,static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,desc.Width*8);
    ok(staging->UnlockRect());staging->Release();return data;
}
struct ResolveSpy:ForwardIDirect3DDevice9 {
    bool capture=false,fail=false;unsigned resolves=0;std::vector<unsigned char> data;
    explicit ResolveSpy(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE f)override{
        ++resolves;if(fail)return E_FAIL;const auto hr=real->StretchRect(a,ar,b,br,f);
        if(capture&&SUCCEEDED(hr))data=pixels(real,b);return hr;
    }
};
static void nativeUi(IDirect3DDevice9* d,IDirect3DSurface9* target,unsigned w,unsigned h,IDirect3DPixelShader9* shader){
    ok(NorthlightHdr::quadState(d,target,w,h));ok(d->SetPixelShader(shader));
    const float base[]={12.5f,.2f,1,1};ok(d->SetPixelShaderConstantF(0,base,1));ok(NorthlightHdr::drawQuad(d,w,h));
    const float ui[]={.5f,4.f,.3f,.375f};ok(d->SetPixelShaderConstantF(0,ui,1));
    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    const Vertex triangle[]={{.1875f,.3125f,0,1,0,0},{float(w)-.8125f,.9375f,0,1,1,0},{float(w)*.4f,float(h)-.1875f,0,1,.4f,1}};
    ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,sizeof(Vertex)));
}
template<class Chain> static std::pair<double,double> benchmark(IDirect3DDevice9* d,NorthlightHdr::Presentation state,HWND window,IDirect3DPixelShader9* shader,IDirect3DTexture9* input,IDirect3DSurface9* composed,unsigned w,unsigned h){
    IDirect3DSurface9* previous=nullptr;ok(d->GetRenderTarget(0,&previous));
    Chain chain;IDirect3DSurface9* target=nullptr;ok(chain.create(d,state,&target));assert(state.active&&target);
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=true;settings.bloom=1;
    ok(d->BeginScene());ok(frame.prepareLegacy(d,input,nullptr,nullptr,w,h,0,1,settings,nullptr,false));ok(d->EndScene());
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    LARGE_INTEGER a{},b{},clock{};QueryPerformanceFrequency(&clock);
    constexpr unsigned N=48;
    for(unsigned i=0;i<N+12;++i){
        if(i==12){ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));QueryPerformanceCounter(&a);}
        ok(d->BeginScene());ok(frame.finish(d,composed,target,settings,1.f/60,true,true));
        ok(d->SetPixelShader(shader));const float ui[]={.1f,.8f,.3f,.5f};ok(d->SetPixelShaderConstantF(0,ui,1));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        const Vertex q[]={{0,0,0,1,0,0},{64,0,0,1,1,0},{0,64,0,1,0,1},{64,64,0,1,1,1}};
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());ok(chain.present(nullptr,nullptr,window,nullptr));
    }
    QueryPerformanceCounter(&b);ok(end->Issue(D3DISSUE_END));UINT64 first=0,last=0,hz=0;
    while(end->GetData(&last,sizeof last,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();ok(start->GetData(&first,sizeof first,0));ok(frequency->GetData(&hz,sizeof hz,0));assert(hz&&last>first);
    start->Release();end->Release();frequency->Release();ok(d->SetRenderTarget(0,previous));previous->Release();target->Release();chain.reset();frame.reset();
    return {double(last-first)*1000/hz/N,double(b.QuadPart-a.QuadPart)*1000/clock.QuadPart/N};
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const bool bench=argc==4;assert(!bench||!std::strcmp(argv[3],"--benchmark"));
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight HDR presentation oracle",WS_POPUP,0,0,bench?3840:128,bench?2160:96,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    if(bench)ShowWindow(window,SW_SHOWNOACTIVATE);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    const bool prepared=NorthlightHdr::unlockFormats(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferCount=1;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));ResolveSpy spy(d);
    IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&shader));
    IDirect3DSurface9* legacy=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&legacy));
    unsigned cases=0,resolveCases=0,edgeCases=0;
    for(auto size:{std::pair<unsigned,unsigned>{17,19},{128,96}})for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES,D3DMULTISAMPLE_4_SAMPLES,D3DMULTISAMPLE_8_SAMPLES})for(unsigned buffers:{1u,2u}){
        const auto w=size.first,h=size.second;NorthlightHdr::Presentation state;state.logical=pp;state.logical.BackBufferWidth=w;state.logical.BackBufferHeight=h;state.logical.MultiSampleType=samples;state.logical.BackBufferCount=buffers;state.stream=state.dxvk=state.settings.requested=true;state.formatsPrepared=prepared;
        NorthlightHdr::OutputChain chain;IDirect3DSurface9* target=nullptr;ok(chain.create(&spy,state,&target));assert(state.active&&target);
        D3DSURFACE_DESC desc{};ok(target->GetDesc(&desc));assert(desc.MultiSampleType==samples&&desc.Format==D3DFMT_A16B16G16R16F);
        D3DPRESENT_PARAMETERS physical{};ok(chain.get()->GetPresentParameters(&physical));assert((physical.MultiSampleType==D3DMULTISAMPLE_NONE||physical.MultiSampleType==samples)&&physical.BackBufferCount==buffers);
        const bool explicitResolve=samples!=D3DMULTISAMPLE_NONE&&physical.MultiSampleType==D3DMULTISAMPLE_NONE;
        ok(d->BeginScene());nativeUi(d,target,w,h,shader);ok(d->EndScene());
        IDirect3DSurface9* expectedTarget=nullptr;ok(d->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&expectedTarget,nullptr));ok(d->StretchRect(target,nullptr,expectedTarget,nullptr,D3DTEXF_NONE));const auto expected=pixels(d,expectedTarget);expectedTarget->Release();
        if(samples){unsigned mixed=0;for(size_t pixel=0;pixel<expected.size()/8;++pixel){std::uint16_t a;std::memcpy(&a,expected.data()+pixel*8+6,2);const float alpha=half(a);mixed+=alpha>.77f&&alpha<.999f;}assert(mixed);++edgeCases;}
        spy.resolves=0;spy.capture=true;spy.data.clear();
        ok(chain.present(nullptr,nullptr,window,nullptr));assert(spy.resolves==unsigned(explicitResolve));if(explicitResolve)assert(spy.data==expected);
        spy.capture=false;
        if(explicitResolve){spy.fail=true;assert(chain.present(nullptr,nullptr,window,nullptr)==E_FAIL);spy.fail=false;++resolveCases;}
        ok(d->SetRenderTarget(0,legacy));target->Release();chain.reset();assert(!chain.exists()&&chain.present(nullptr,nullptr,nullptr,nullptr)==D3DERR_INVALIDCALL);++cases;
    }
    std::printf("HDR_PRESENT_NUMERICAL_PASS cases=%u native_msaa_edge_cases=%u explicit_resolve_failure_cases=%u reset_release=1\n",cases,edgeCases,resolveCases);std::fflush(stdout);
    if(bench){
        const auto im=image(argv[2]);assert(im.format==D3DFMT_A16B16G16R16F);auto* input=texture(d,im);IDirect3DSurface9* composed=nullptr;
        ok(d->CreateRenderTarget(im.width,im.height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_8_SAMPLES,0,FALSE,&composed,nullptr));
        IDirect3DPixelShader9* copy=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copy));
        ok(d->BeginScene());ok(NorthlightHdr::quadState(d,composed,im.width,im.height));ok(d->SetPixelShader(copy));ok(d->SetTexture(0,input));ok(NorthlightHdr::drawQuad(d,im.width,im.height));ok(d->EndScene());
        NorthlightHdr::Presentation state;state.logical=pp;state.logical.BackBufferWidth=im.width;state.logical.BackBufferHeight=im.height;state.logical.MultiSampleType=D3DMULTISAMPLE_8_SAMPLES;state.stream=state.dxvk=state.settings.requested=true;state.formatsPrepared=prepared;
        for(unsigned round=0;round<5;++round){
            std::pair<double,double> old,now;
            if(round%2){now=benchmark<NorthlightHdr::OutputChain>(d,state,window,shader,input,composed,im.width,im.height);old=benchmark<ReferenceHdr::OutputChain>(d,state,window,shader,input,composed,im.width,im.height);}
            else{old=benchmark<ReferenceHdr::OutputChain>(d,state,window,shader,input,composed,im.width,im.height);now=benchmark<NorthlightHdr::OutputChain>(d,state,window,shader,input,composed,im.width,im.height);}
            std::printf("{\"round\":%u,\"reference_gpu_ms\":%.6f,\"candidate_gpu_ms\":%.6f,\"reference_wall_ms\":%.6f,\"candidate_wall_ms\":%.6f,\"scope\":\"post_ui_present_with_present_wait_and_cpu_gaps_not_pure_gpu\"}\n",round,old.first,now.first,old.second,now.second);std::fflush(stdout);
        }
        ok(d->SetTexture(0,nullptr));composed->Release();copy->Release();input->Release();
    }
    ok(d->SetRenderTarget(0,legacy));legacy->Release();shader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
