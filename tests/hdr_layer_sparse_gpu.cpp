#define main northlight_unused_composite
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_compiled_shaders.h"
#include "hdr_layer_reference.h"
static const std::uint32_t* layerProgram=kHdrLayer;
#define kHdrLayer layerProgram
#include "hdr_color.h"
#undef kHdrLayer
#include "forwarders.h"

struct CompositeSamples:ForwardIDirect3DDevice9 {
    IDirect3DQuery9* query=nullptr;
    const std::uint32_t* expected=nullptr;unsigned expectedBytes=0;
    explicit CompositeSamples(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query));}
    ~CompositeSamples(){query->Release();}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT primitives,const void* vertices,UINT stride)override{
        IDirect3DPixelShader9* ps=nullptr;ok(real->GetPixelShader(&ps));UINT bytes=0;ok(ps->GetFunction(nullptr,&bytes));std::vector<unsigned char> code(bytes);ok(ps->GetFunction(code.data(),&bytes));ps->Release();assert(bytes==expectedBytes&&!std::memcmp(code.data(),expected,bytes));
        // Both controlled source alphas (0 and .25) pass alpha != 255. This
        // forces post-shader query counting on warmed DXVK2 pipelines without
        // rejecting a pixel; only the production shader's discard removes work.
        DWORD enabled=0,function=0,reference=0;ok(real->GetRenderState(D3DRS_ALPHATESTENABLE,&enabled));ok(real->GetRenderState(D3DRS_ALPHAFUNC,&function));ok(real->GetRenderState(D3DRS_ALPHAREF,&reference));
        ok(real->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(real->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_NOTEQUAL));ok(real->SetRenderState(D3DRS_ALPHAREF,255));
        ok(query->Issue(D3DISSUE_BEGIN));const auto hr=real->DrawPrimitiveUP(type,primitives,vertices,stride);ok(query->Issue(D3DISSUE_END));
        ok(real->SetRenderState(D3DRS_ALPHATESTENABLE,enabled));ok(real->SetRenderState(D3DRS_ALPHAFUNC,function));ok(real->SetRenderState(D3DRS_ALPHAREF,reference));return hr;
    }
    DWORD samples(){DWORD result=0;HRESULT hr;while((hr=query->GetData(&result,sizeof result,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);return result;}
};

int main(int argc,char** argv){
    assert(argc==3||argc==4);const auto samples=D3DMULTISAMPLE_TYPE(std::atoi(argv[2]));const bool coverageOnly=argc==4&&!std::strcmp(argv[3],"--coverage-only"),benchmark=argc==4&&!coverageOnly;
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* flat=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&flat));
    auto quad=[&](float x,float y,float width,float height){const Vertex q[]={{x-.5f,y-.5f,.3f,1,0,0},{x+width-.5f,y-.5f,.3f,1,1,0},{x-.5f,y+height-.5f,.3f,1,0,1},{x+width-.5f,y+height-.5f,.3f,1,1,1}};return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));};
    constexpr unsigned W=64,H=32;
    constexpr unsigned parityCases=432;
    IDirect3DSurface9 *target=nullptr,*resolve=nullptr,*stage=nullptr,*atlas[2]{};
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,samples,0,FALSE,&target,nullptr));
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolve,nullptr));
    if(!coverageOnly){for(auto*& surface:atlas)ok(d->CreateRenderTarget(W,H*parityCases,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&surface,nullptr));ok(d->CreateOffscreenPlainSurface(W,H*parityCases,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&stage,nullptr));}
    auto pixels=[&](unsigned mode){ok(d->GetRenderTargetData(atlas[mode],stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(W*H*parityCases*8);for(unsigned y=0;y<H*parityCases;++y)std::memcpy(result.data()+y*W*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,W*8);ok(stage->UnlockRect());return result;};
    const DWORD blends[][3]={{FALSE,D3DBLEND_ONE,D3DBLEND_ZERO},{TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA},{TRUE,D3DBLEND_ONE,D3DBLEND_INVSRCALPHA},{TRUE,D3DBLEND_ONE,D3DBLEND_ONE},{TRUE,D3DBLEND_SRCALPHA,D3DBLEND_ONE},{TRUE,D3DBLEND_DESTCOLOR,D3DBLEND_ZERO},{TRUE,D3DBLEND_ZERO,D3DBLEND_SRCCOLOR},{TRUE,D3DBLEND_ZERO,D3DBLEND_INVSRCALPHA}};
    const float sources[][4]={{0,0,0,0},{1,.5f,.2f,0},{0,0,0,1},{.8f,.3f,.1f,.25f},{.1f,.7f,.9f,.75f},{1,1,1,1}};
    NorthlightHdr::Color transfers[2];NorthlightStateBlockPool states(d);
    unsigned cases=0;
    if(!coverageOnly)
    for(const auto& blend:blends)for(const auto& source:sources)for(float alpha:{0.f,.375f,1.f})for(unsigned writes:{7u,8u,15u}){
        for(unsigned mode=0;mode<2;++mode){
            layerProgram=mode?kHdrLayer:kHdrLayerReference;auto& color=transfers[mode];
            ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(flat));const float background[]={2.f,.375f,.125f,alpha};ok(d->SetPixelShaderConstantF(0,background,1));ok(quad(0,0,W,H));
            ok(d->SetPixelShaderConstantF(0,source,1));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend[0]));ok(d->SetRenderState(D3DRS_SRCBLEND,blend[1]));ok(d->SetRenderState(D3DRS_DESTBLEND,blend[2]));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,writes));
            ok(color.legacyLayer(d,80,[&]{return quad(5.25f,3.75f,19.5f,11.25f);},&states));ok(d->EndScene());
            ok(d->StretchRect(target,nullptr,resolve,nullptr,D3DTEXF_NONE));const RECT region={0,LONG(cases*H),W,LONG((cases+1)*H)};ok(d->StretchRect(resolve,nullptr,atlas[mode],&region,D3DTEXF_NONE));
        }++cases;
    }
    if(!coverageOnly){assert(cases==parityCases);const auto reference=pixels(0),actual=pixels(1);if(actual!=reference){const auto mismatch=std::mismatch(actual.begin(),actual.end(),reference.begin()).first;std::fprintf(stderr,"LAYER_PARITY_ERROR case=%u byte=%u\n",unsigned((mismatch-actual.begin())/(W*H*8)),unsigned(mismatch-actual.begin()));return 3;}}
    IDirect3DSurface9* guardStage=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&guardStage,nullptr));
    for(bool dense:{false,true}){
        CompositeSamples meters[2]={CompositeSamples(d),CompositeSamples(d)};DWORD work[2]{};std::vector<unsigned char> reference;
        for(unsigned mode=0;mode<2;++mode){auto& meter=meters[mode];layerProgram=mode?kHdrLayer:kHdrLayerReference;meter.expected=layerProgram;meter.expectedBytes=mode?sizeof(kHdrLayer):sizeof(kHdrLayerReference);auto& color=transfers[mode];
            ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(flat));const float background[]={2,.375f,.125f,.375f},source[]={.8f,.3f,.1f,.25f};ok(d->SetPixelShaderConstantF(0,background,1));ok(quad(0,0,W,H));
            ok(d->SetPixelShaderConstantF(0,source,1));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            ok(color.legacyLayer(&meter,80,[&]{return quad(dense?0:5,dense?0:3,dense?W:16,dense?H:8);},&states));ok(d->EndScene());work[mode]=meter.samples();
            ok(d->StretchRect(target,nullptr,resolve,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolve,guardStage));D3DLOCKED_RECT lock{};ok(guardStage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> actual(W*H*8);for(unsigned y=0;y<H;++y)std::memcpy(actual.data()+y*W*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,W*8);ok(guardStage->UnlockRect());if(mode)assert(actual==reference);else reference=std::move(actual);
        }
        assert(work[0]==W*H*std::max(1u,unsigned(samples))&&work[1]==(dense?W*H:128)*std::max(1u,unsigned(samples)));
        std::printf("HDR_LAYER_SPARSE_WORK samples=%u dense=%u before_composite_samples=%lu after_composite_samples=%lu rgba16f_bit_exact=1\n",unsigned(samples),unsigned(dense),work[0],work[1]);
    }
    if(cases)std::printf("HDR_LAYER_SPARSE_GPU_PASS samples=%u cases=%u rgba16f_bit_exact=1 factor_controls=3 opaque_control=1\n",unsigned(samples),cases);
    else std::printf("HDR_LAYER_SPARSE_WORK_ONLY_PASS samples=%u\n",unsigned(samples));std::fflush(stdout);
    if(benchmark){
        constexpr unsigned BW=3840,BH=2160,draws=220;
        IDirect3DSurface9* big=nullptr;ok(d->CreateRenderTarget(BW,BH,D3DFMT_A16B16G16R16F,samples,0,FALSE,&big,nullptr));
        layerProgram=kHdrLayerReference;NorthlightHdr::Color before;layerProgram=kHdrLayer;NorthlightHdr::Color after;NorthlightStateBlockPool timingStates(d);
        auto frame=[&](unsigned mode){layerProgram=mode?kHdrLayer:kHdrLayerReference;auto& color=mode?after:before;ok(d->BeginScene());ok(NorthlightHdr::quadState(d,big,BW,BH));ok(d->SetPixelShader(flat));const float source[]={.8f,.3f,.1f,.25f};ok(d->SetPixelShaderConstantF(0,source,1));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            for(unsigned n=0;n<draws;++n)ok(color.legacyLayer(d,80,[&]{return quad(float((n*113)%(BW-128)),float((n*73)%(BH-128)),128,128);},&timingStates));ok(d->EndScene());};
        for(unsigned n=0;n<4;++n)frame(n%2);
        double measurements[2][6]{};
        for(unsigned block=0;block<6;++block)for(unsigned order=0;order<2;++order){const unsigned mode=(block+order)%2;IDirect3DQuery9 *first=nullptr,*last=nullptr,*frequency=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&first));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&last));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(frequency->Issue(D3DISSUE_END));ok(first->Issue(D3DISSUE_END));for(unsigned n=0;n<4;++n)frame(mode);ok(last->Issue(D3DISSUE_END));UINT64 a=0,b=0,hz=0;while(last->GetData(&b,sizeof b,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();ok(first->GetData(&a,sizeof a,0));ok(frequency->GetData(&hz,sizeof hz,0));assert(hz&&b>a);measurements[mode][block]=double(b-a)*1000/hz/4;first->Release();last->Release();frequency->Release();}
        double deltas[6];for(unsigned block=0;block<6;++block){deltas[block]=measurements[1][block]-measurements[0][block];std::printf("HDR_LAYER_SPARSE_PAIR block=%u before_ms=%.5f after_ms=%.5f delta_ms=%.5f\n",block,measurements[0][block],measurements[1][block],deltas[block]);}std::sort(deltas,deltas+6);std::sort(measurements[0],measurements[0]+6);std::sort(measurements[1],measurements[1]+6);
        std::printf("HDR_LAYER_SPARSE_PERF width=%u height=%u samples=%u draws=%u before_ms=%.5f after_ms=%.5f paired_delta_ms=%.5f clear_resolve_quality_unchanged=1\n",BW,BH,unsigned(samples),draws,measurements[0][3],measurements[1][3],deltas[3]);
    }
}
