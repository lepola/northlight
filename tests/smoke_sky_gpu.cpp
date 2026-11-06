// Production renderer methods and HDR helpers on a hidden, independent device.
// Native geometry/alpha/depth are real D3D9; no game process or data is modified.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_color.h"
#include "hdr_frame.h"
#include "hdr_sky_foreground.h"
#include "effect_coverage.h"
#include "world_draw_domain.h"
#include "world_compiled_shaders.h"
#include "forwarders.h"
#include "device_mirror.h"
#include "smoke_sky_native.h"
static void checked(HRESULT hr,const char* operation,unsigned line){if(FAILED(hr)){std::fprintf(stderr,"line=%u operation=%s HRESULT=%08lx\n",line,operation,(unsigned long)hr);std::abort();}}
#define ok(operation) checked((operation),#operation,__LINE__)

struct FaultDevice:ForwardIDirect3DDevice9 {
    unsigned fault=0;IDirect3DSurface9* sky=nullptr;
    explicit FaultDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{const auto hr=real->GetDeviceCaps(caps);if(fault==1)caps->PrimitiveMiscCaps&=~D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS;return hr;}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{if(fault==2&&n&&p==sky){fault=0;return E_FAIL;}return real->SetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fault==3){fault=0;return E_FAIL;}return real->SetPixelShader(p);}
};

namespace NorthlightBackend {static bool isPackagedDxvk(int){return true;}}
namespace NorthlightSpellLayer {enum class Family {None,Particle,ParticleUnlit,Model,Projected,Ripples};}
struct Fixture {
    DeviceMirror mirror;
    ExtensionDevice device;ExtensionDevice* ext;
    NorthlightHdr::Color hdrColor;
    NorthlightHdr::SkyForeground hdrSkyForeground;
    NorthlightStateBlockPool stateBlocks;
    NorthlightEffectCoverage::Capture effectCoverage;
    struct {bool active=true;} hdr;
    bool hdrSkyValid=true,hdrUiPhase=false,hdrAtUiBoundary=false,enabled=true,failed=false,applied=false;
    bool effectCoverageNative=true,terrain=true,projectionValid=true,hdrSkyReplaying=false;
    bool spellQueryUnknown=false,depthBound=true,backTarget=true,fullViewport=true;
    unsigned debugMode=0,worldDebug=0,spellQueryN=0;
    int selectedBackend=0;
    unsigned width=16,height=8;
    static constexpr unsigned kUnlitTag=256,kFamilyShift=4;
    std::unordered_map<IDirect3DVertexShader9*,unsigned> vsTags;
    float worldMinDepth=0,worldMaxDepth=.94f;
    IDirect3DSurface9* hdrPresentBack;
    struct Discs {bool cleared=false;void setHdrSkyTarget(void*){cleared=true;}} discs;
    Discs* celestialDiscs=&discs;
    Fixture(IDirect3DDevice9* d,IDirect3DSurface9* sky):device(d,&mirror),ext(&device),stateBlocks(&device),hdrPresentBack(sky){D3DSURFACE_DESC desc{};ok(sky->GetDesc(&desc));width=desc.Width;height=desc.Height;vsTags[nullptr]=kUnlitTag;}
    template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
    DWORD drawRenderState(D3DRENDERSTATETYPE state){DWORD value=0;ok(ext->GetRenderState(state,&value));return value;}
    bool nameMaskFullViewport(const D3DVIEWPORT9& vp){return fullViewport&&vp.X==0&&vp.Y==0&&vp.Width==width&&vp.Height==height;}
    bool worldDepthBound(){return depthBound;}
    bool hdrBackTarget(){return backTarget;}
    bool diagnostics(){return false;}
    void hdrLayerDiagnostic(const char*){}
    template<class F> void extensionWork(const char*,F work){work();}
    HRESULT hdrBindBack(IDirect3DSurface9* target){D3DVIEWPORT9 vp{};ok(ext->GetViewport(&vp));ok(ext->SetRenderTarget(0,target));return ext->SetViewport(&vp);}
#include "smoke_sky_methods.inc"
};
static float decode(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
static IDirect3DTexture9* solid(IDirect3DDevice9* d,float r,float g,float b,float a){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return texture(d,im);
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);constexpr unsigned W=16,H=8;const unsigned samples=unsigned(std::atoi(argv[2]));const auto ms=D3DMULTISAMPLE_TYPE(samples);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DTexture9 *scene=nullptr,*sky=nullptr;IDirect3DSurface9 *native=nullptr,*linearSky=nullptr,*zbuffer=nullptr,*output=nullptr,*stage=nullptr,*nativeStage=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scene,nullptr));ok(scene->GetSurfaceLevel(0,&native));
    IDirect3DSurface9* nativeResolve=native;if(samples)ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,ms,0,FALSE,&native,nullptr));
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&sky,nullptr));ok(sky->GetSurfaceLevel(0,&linearSky));
    if(samples)ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,ms,0,FALSE,&linearSky,nullptr));
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,ms,0,TRUE,&zbuffer,nullptr));
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&nativeStage,nullptr));
    IDirect3DPixelShader9 *flat=nullptr,*flatShaders[2]{},*world=nullptr;
    const DWORD flat2[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0xffff};ok(d->CreatePixelShader(flat2,&flatShaders[0]));
    ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&flatShaders[1]));ok(d->CreatePixelShader(kWorldCompositeHdrShader,&world));
    auto quad=[&](float z,bool left=false){const Vertex q[]={{-.5f,-.5f,z,1,0,0},{float(left?W/2:W)-.5f,-.5f,z,1,1,0},{-.5f,H-.5f,z,1,0,1},{float(left?W/2:W)-.5f,H-.5f,z,1,1,1}};return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));};
    auto rgba=[&](IDirect3DSurface9* source){D3DSURFACE_DESC desc{};ok(source->GetDesc(&desc));IDirect3DSurface9* resolved=nullptr;if(desc.MultiSampleType){ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->StretchRect(source,nullptr,resolved,nullptr,D3DTEXF_NONE));}ok(d->GetRenderTargetData(resolved?resolved:source,stage));if(resolved)resolved->Release();D3DLOCKED_RECT l{};ok(stage->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> v(W*H*4);for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W*4;++x)v[y*W*4+x]=half(static_cast<const std::uint16_t*>(static_cast<const void*>(static_cast<const char*>(l.pBits)+y*l.Pitch))[x]);ok(stage->UnlockRect());return v;};
    auto nativePixels=[&]{if(samples)ok(d->StretchRect(native,nullptr,nativeResolve,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(nativeResolve,nativeStage));D3DLOCKED_RECT l{};ok(nativeStage->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> v(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(v.data()+y*W*4,static_cast<const char*>(l.pBits)+y*l.Pitch,W*4);ok(nativeStage->UnlockRect());return v;};
    auto reset=[&]{for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(NorthlightHdr::quadState(d,native,W,H));ok(d->SetDepthStencilSurface(zbuffer));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));ok(d->SetPixelShader(flat));};
    Image depthImage{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=x<W/2?.47f:1;std::memcpy(depthImage.pixels.data()+(y*W+x)*4,&z,4);}auto depth=texture(d,depthImage);
    auto zero=solid(d,0,0,0,0),white=solid(d,1,1,1,1),fog=solid(d,0,0,0,1);
    IDirect3DVertexShader9* foregroundVs=nullptr;ok(d->CreateVertexShader(kForegroundVs3,&foregroundVs));
    const D3DVERTEXELEMENT9 foregroundElements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* foregroundDecl=nullptr;ok(d->CreateVertexDeclaration(foregroundElements,&foregroundDecl));
    auto* foregroundTexture=solid(d,.7f,.5f,.3f,.75f);
    unsigned nativeCases=0;float nativeWorst=0;
    for(const auto& program:kNativeForeground)for(bool fogEnabled:{false,true})for(unsigned control=0;control<4;++control){
        IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(program.code,&ps));
        auto setup=[&]{
            ok(d->SetVertexDeclaration(foregroundDecl));
            ok(d->SetVertexShader(foregroundVs));ok(d->SetPixelShader(ps));
            const float viewport[]={2.f/W,-2.f/H,0,0},tint[]={.8f,.3f,.1f,.5f},amount[]={.35f,0,0,0};
            ok(d->SetVertexShaderConstantF(0,viewport,1));ok(d->SetVertexShaderConstantF(1,tint,1));ok(d->SetVertexShaderConstantF(2,amount,1));
            float constants[14][4];for(auto& row:constants)for(auto& value:row)value=.2f;
            constants[2][0]=.03f;constants[2][1]=.07f;constants[2][2]=.11f;
            ok(d->SetPixelShaderConstantF(0,constants[0],14));
            for(unsigned slot:{0u,1u,4u}){ok(d->SetTexture(slot,foregroundTexture));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,key,D3DTEXF_POINT));}
            ok(d->SetRenderState(D3DRS_FOGENABLE,fogEnabled));ok(d->SetRenderState(D3DRS_FOGCOLOR,0xff102030));
            ok(d->SetRenderState(D3DRS_FOGTABLEMODE,D3DFOG_NONE));ok(d->SetRenderState(D3DRS_FOGVERTEXMODE,D3DFOG_NONE));
        };
        flat=flatShaders[1];reset();ok(d->BeginScene());setup();ok(quad(.3f));ok(d->EndScene());const auto source=nativePixels();
        std::vector<unsigned char> reference;
        for(unsigned mode=0;mode<2;++mode){
            Fixture f(d,linearSky);f.terrain=control!=0;reset();ok(d->BeginScene());
            const float background[]={.2f,.3f,.4f,1},base[]={2.f,.6f,.3f,1};
            ok(d->SetPixelShaderConstantF(0,background,1));ok(quad(1));
            {SavedState save(d);ok(d->SetRenderTarget(0,linearSky));ok(d->SetPixelShaderConstantF(0,base,1));ok(quad(1));}
            setup();const D3DVIEWPORT9 vp={0,0,W,H,0,.94f};ok(d->SetViewport(&vp));
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            f.hdr.active=mode!=0;f.vsTags[foregroundVs]=Fixture::kUnlitTag;
            if(control>=2){const float black[]={0,0,0,0};ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(flat));ok(d->SetPixelShaderConstantF(0,black,1));
                ok(d->SetRenderState(D3DRS_FOGENABLE,FALSE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));f.mirror.invalidate();
                ok(f.effectCoverageDraw([&]{return quad(.3f);},true));
                ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));f.mirror.invalidate();
                ok(f.effectCoverageDraw([&]{return quad(.3f);},true));setup();}
            f.mirror.invalidate();f.effectCoverageNative=control!=3;unsigned submits=0;const auto before=f.hdrSkyForeground.draws;
            ok(f.effectCoverageDraw([&]{++submits;return quad(.3f);},control==3));f.effectCoverage.endBatch();ok(d->EndScene());
            if(submits!=1){std::fprintf(stderr,"NATIVE_FOREGROUND_REPLAY sm=%u variant=%u fog=%u mode=%u control=%u submits=%u layers=%llu\n",program.major,program.index,unsigned(fogEnabled),mode,control,submits,(unsigned long long)f.hdrColor.layers);std::abort();}
            if(mode){assert(f.hdrSkyForeground.draws==before+1&&f.hdrColor.layers==0);}
            const auto pixels=nativePixels();if(!mode)reference=pixels;else{
                assert(pixels==reference);const auto actual=rgba(linearSky);const unsigned at=(4*W+12)*4;const float alpha=source[at+3]/255.f;
                for(unsigned c=0;c<3;++c){const float expected=base[c]*(1-alpha)+decode(source[at+2-c]/255.f)*alpha;
                    const float error=std::fabs(actual[at+c]-expected);nativeWorst=std::max(nativeWorst,error);
                    if(error>=.012f){std::fprintf(stderr,"NATIVE_FOREGROUND_ERROR sm=%u variant=%u fog=%u channel=%u expected=%g actual=%g source=%u alpha=%g\n",program.major,program.index,unsigned(fogEnabled),c,expected,actual[at+c],unsigned(source[at+2-c]),alpha);std::abort();}
                }
            }
        }
        ps->Release();++nativeCases;
    }
    std::printf("NATIVE_FOREGROUND_GPU_PASS cases=%u alpha_emission_early_drain_controls=4 native_rgba_unchanged=1 max_linear_error=%g\n",nativeCases,nativeWorst);
    unsigned cases=0,negative=0,mrt=0;float worst=0;
    for(unsigned sm=0;sm<2;++sm)for(unsigned blend=0;blend<4;++blend)for(float alpha:{0.f,.25f,.75f,1.f})for(unsigned control=0;control<5;++control){
        flat=flatShaders[sm];
        const bool premult=blend==1,additive=blend>=2;const DWORD src=premult||blend==3?D3DBLEND_ONE:D3DBLEND_SRCALPHA,dst=additive?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA;
        const float smoke[]={.8f*(premult?alpha:1),.3f*(premult?alpha:1),.1f*(premult?alpha:1),alpha};
        std::vector<unsigned char> nativeReference;std::vector<float> oldSky,final;DWORD samplesReference=0;
        for(unsigned mode=0;mode<3;++mode){
            Fixture f(d,linearSky);f.effectCoverage.allowMultisample(true);reset();ok(d->BeginScene());
            const float nativeBackground[]={.2f,.3f,.4f,1},ground[]={.1f,.15f,.2f,1},hdrBackground[]={2.f,.6f,.3f,1};
            ok(d->SetPixelShaderConstantF(0,nativeBackground,1));ok(quad(1));
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetPixelShaderConstantF(0,ground,1));ok(quad(.47f,true));
            {SavedState save(d);ok(d->SetRenderTarget(0,linearSky));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetPixelShaderConstantF(0,hdrBackground,1));ok(quad(1));}
            D3DVIEWPORT9 worldViewport={0,0,W,H,0,.94f};ok(d->SetViewport(&worldViewport));
            ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,src));ok(d->SetRenderState(D3DRS_DESTBLEND,dst));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetPixelShaderConstantF(0,smoke,1));
            if(control==2){ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));}
            if(control==3){ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_INCRSAT));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));}
            f.hdr.active=mode==2; // Mode 0 = native reference; 1 = old HDR capture; 2 = candidate.
            const bool earlyDrain=control==4;f.effectCoverageNative=!earlyDrain;
            IDirect3DQuery9* query=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query));ok(query->Issue(D3DISSUE_BEGIN));
            unsigned submits=0;ok(f.effectCoverageDraw([&]{++submits;return quad(control==1?.75f:.3f);},earlyDrain));f.effectCoverage.endBatch();
            ok(query->Issue(D3DISSUE_END));
            assert(submits==1);if(mode==2){assert(f.hdrSkyForeground.draws==1);++mrt;}
            ok(d->EndScene());const auto n=nativePixels();if(!mode)nativeReference=n;else assert(n==nativeReference);
            DWORD samples=0;HRESULT queryStatus;while((queryStatus=query->GetData(&samples,sizeof samples,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(queryStatus);query->Release();if(!mode)samplesReference=samples;else assert(samples==samplesReference);
            if(mode==1)oldSky=rgba(linearSky);
            if(mode==2){
                const auto actual=rgba(linearSky);const float a=control==2&&alpha<=128.f/255?0:alpha;
                for(unsigned channel=0;channel<3;++channel){
                    const float decoded=premult?decode(smoke[channel]/std::max(alpha,.000001f))*alpha:decode(smoke[channel]);
                    const float expected=hdrBackground[channel]*(additive?1:1-a)+(control==2&&alpha<=128.f/255?0:decoded*(premult||blend==3?1:a));
                    const auto index=(4*W+12)*4+channel;worst=std::max(worst,std::fabs(actual[index]-expected));assert(std::fabs(actual[index]-expected)<.004f);
                    if(std::fabs(expected-hdrBackground[channel])>.02f){assert(std::fabs(oldSky[index]-expected)>.02f);++negative;}
                }
                NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;ok(d->BeginScene());ok(frame.prepareLegacy(d,scene,depth,linearSky,W,H,0,.94f,settings));ok(d->EndScene());final=rgba(frame.surface());
                for(unsigned channel=0;channel<3;++channel){const auto i=(4*W+12)*4+channel;assert(std::fabs(final[i]-actual[i])<.001f);}
                for(unsigned channel=0;channel<3;++channel){const auto i=(4*W+4)*4+channel,nativeIndex=(4*W+4)*4+(2-channel);assert(std::fabs(final[i]-decode(n[nativeIndex]/255.f))<.001f);}
                if(control==3){
                    // Equality passes only if the native draw incremented stencil once.
                    SavedState save(d);ok(d->SetRenderTarget(0,native));ok(d->SetDepthStencilSurface(zbuffer));ok(d->SetPixelShader(flat));
                    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));
                    ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILREF,1));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
                    IDirect3DQuery9* probe=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&probe));ok(d->BeginScene());ok(probe->Issue(D3DISSUE_BEGIN));ok(quad(.8f));ok(probe->Issue(D3DISSUE_END));ok(d->EndScene());
                    DWORD hits=0;while((queryStatus=probe->GetData(&hits,sizeof hits,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(queryStatus);assert(hits==samplesReference);probe->Release();
                }
            }
        }
        ++cases;
    }
    // Exercise the actual wrapper's family admission, including model/projected
    // ground effects. Lit alpha models, ripples and unknown shaders stay receivers.
    unsigned familyCases=0;
    for(unsigned family=0;family<7;++family)for(unsigned blend=0;blend<4;++blend)for(bool replay:{false,true}){
        Fixture f(d,linearSky);f.hdr.active=false;f.effectCoverageNative=!replay;
        f.vsTags[nullptr]=family==6?Fixture::kUnlitTag:family<<Fixture::kFamilyShift;
        flat=flatShaders[1];reset();
        ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->SetRenderState(D3DRS_SRCBLEND,blend==1||blend==3?D3DBLEND_ONE:D3DBLEND_SRCALPHA));
        ok(d->SetRenderState(D3DRS_DESTBLEND,blend>=2?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
        const D3DVIEWPORT9 vp={0,0,W,H,0,.94f};ok(d->SetViewport(&vp));
        const float color[]={.2f,.1f,.05f,.5f};ok(d->SetPixelShaderConstantF(0,color,1));
        unsigned submits=0;ok(d->BeginScene());ok(f.effectCoverageDraw([&]{++submits;return quad(.3f);},replay));f.effectCoverage.endBatch();ok(d->EndScene());
        const bool expected=family==1||family==2||family==6||(blend>=2&&(family==3||family==4));
        assert(submits==1&&f.effectCoverage.active()==expected);
        assert((f.effectCoverage.texture(d,native)!=nullptr)==expected);
        assert(f.effectCoverage.hasEmission()==(expected&&blend>=2));++familyCases;
    }
    std::printf("SPELL_FAMILY_GPU_PASS cases=%u particle_model_projected_unlit=1 lit_alpha_ripple_unknown_controls=1\n",familyCases);
    // Admission guards keep native-only, UI, opaque and unrelated targets out of the saved sky.
    for(unsigned control=0;control<14;++control){
        Fixture f(d,linearSky);reset();ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));D3DVIEWPORT9 vp={0,0,W,H,0,.94f};ok(d->SetViewport(&vp));
        if(control==0)f.hdr.active=false;if(control==1)f.hdrSkyValid=false;if(control==2)f.hdrUiPhase=true;if(control==3)f.applied=true;if(control==4)f.backTarget=false;if(control==5)f.depthBound=false;if(control==6)f.fullViewport=false;if(control==7)ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));if(control==8)ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS));if(control==9)f.spellQueryN=1;if(control==10)f.spellQueryUnknown=true;if(control==11)f.enabled=false;if(control==12)f.effectCoverageNative=false;if(control==13)f.failed=true;
        unsigned submits=0;ok(d->BeginScene());ok(f.effectCoverageDraw([&]{++submits;return S_OK;}));ok(d->EndScene());assert(submits==1);
    }
    // Failed caps, binding, shader activation and dimension/depth checks restore
    // every native MRT and write mask. Samplers are never changed by this path.
    unsigned restoration=0;
    for(unsigned fault=1;fault<=6;++fault){
        reset();IDirect3DSurface9* alternate=nullptr;ok(d->CreateRenderTarget(fault==4?W/2:W,H,D3DFMT_A16B16G16R16F,ms,0,FALSE,&alternate,nullptr));
        if(fault==5)ok(d->SetDepthStencilSurface(nullptr));
        IDirect3DSurface9* occupied[3]{};
        if(fault==6)for(unsigned slot=1;slot<4;++slot){ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,ms,0,FALSE,&occupied[slot-1],nullptr));ok(d->SetRenderTarget(slot,occupied[slot-1]));}
        for(auto key:{D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3})ok(d->SetRenderState(key,2));
        ok(d->SetSamplerState(7,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR));
        SavedState before(d);FaultDevice meter(d);meter.fault=fault;meter.sky=linearSky;NorthlightHdr::SkyForeground capture;
        unsigned submits=0;HRESULT result=S_OK;
        ok(d->BeginScene());assert(!capture.draw(&meter,fault==4?alternate:linearSky,true,[&]{++submits;return S_OK;},result));ok(d->EndScene());assert(!submits);
        SavedState after(d);assert(after.depth==before.depth&&after.viewport.MinZ==before.viewport.MinZ&&after.viewport.MaxZ==before.viewport.MaxZ);
        for(unsigned slot=0;slot<4;++slot)assert(after.targets[slot]==before.targets[slot]);
        IDirect3DPixelShader9* ps=nullptr;ok(d->GetPixelShader(&ps));assert(ps==flat);ps->Release();
        for(auto key:{D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3}){DWORD value=0;ok(d->GetRenderState(key,&value));assert(value==2);}
        DWORD sampler=0;ok(d->GetSamplerState(7,D3DSAMP_ADDRESSU,&sampler));assert(sampler==D3DTADDRESS_MIRROR);
        for(unsigned slot=1;slot<4;++slot){ok(d->SetRenderTarget(slot,nullptr));if(occupied[slot-1])occupied[slot-1]->Release();}alternate->Release();++restoration;
    }
    // A failed MRT activation still retains smoke via the existing HDR transfer.
    for(unsigned fault:{1u,2u,3u,4u,5u}){
        reset();FaultDevice meter(d);meter.fault=fault>=4?1:fault;meter.sky=linearSky;Fixture f(&meter,linearSky);f.terrain=false;f.spellQueryN=fault==4;f.spellQueryUnknown=fault==5;
        ok(d->BeginScene());const float base[]={2.f,.6f,.3f,1},smoke[]={.8f,.3f,.1f,.25f};
        {SavedState save(d);ok(d->SetRenderTarget(0,linearSky));ok(d->SetPixelShaderConstantF(0,base,1));ok(quad(1));}
        D3DVIEWPORT9 vp={0,0,W,H,0,.94f};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetPixelShaderConstantF(0,smoke,1));
        unsigned submits=0;ok(f.effectCoverageDraw([&]{++submits;return quad(.3f);}));f.effectCoverage.endBatch();ok(d->EndScene());assert(submits==(fault>=4?1u:2u)&&!f.hdrSkyForeground.draws);
        if(fault>=4){
            assert(!f.hdrSkyValid&&f.discs.cleared);const auto n=nativePixels();NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;ok(d->BeginScene());ok(frame.prepareLegacy(d,scene,depth,nullptr,W,H,0,.94f,settings));ok(d->EndScene());const auto actual=rgba(frame.surface());
            for(unsigned c=0;c<3;++c)assert(std::fabs(actual[(4*W+12)*4+c]-decode(n[(4*W+12)*4+2-c]/255.f))<.001f);
        }else{const auto actual=rgba(linearSky);for(unsigned c=0;c<3;++c)assert(std::fabs(actual[(4*W+12)*4+c]-(base[c]*.75f+decode(smoke[c])*.25f))<.002f);}
    }
    if(argc==4){
        constexpr unsigned BW=3840,BH=2160;const unsigned draws=std::atoi(argv[3])>0?unsigned(std::atoi(argv[3])):220;
        IDirect3DSurface9 *bigNative=nullptr,*bigSky=nullptr,*bigDepth=nullptr;
        ok(d->CreateRenderTarget(BW,BH,D3DFMT_A8R8G8B8,ms,0,FALSE,&bigNative,nullptr));ok(d->CreateRenderTarget(BW,BH,D3DFMT_A16B16G16R16F,ms,0,FALSE,&bigSky,nullptr));ok(d->CreateDepthStencilSurface(BW,BH,D3DFMT_D24S8,ms,0,TRUE,&bigDepth,nullptr));
        Fixture f(d,bigSky);f.width=BW;f.height=BH;
        auto frame=[&](bool candidate){
            f.hdr.active=candidate;f.effectCoverage.endFrame();for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(NorthlightHdr::quadState(d,bigNative,BW,BH));ok(d->SetDepthStencilSurface(bigDepth));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
            ok(d->BeginScene());ok(d->SetPixelShader(flatShaders[1]));const float smoke[]={.8f,.3f,.1f,.25f};ok(d->SetPixelShaderConstantF(0,smoke,1));D3DVIEWPORT9 vp={0,0,BW,BH,0,.94f};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            f.mirror.invalidate();for(unsigned n=0;n<draws;++n){const float x=float((n*113)%(BW-128)),y=float((n*73)%(BH-128));const Vertex q[]={{x-.5f,y-.5f,.3f,1,0,0},{x+127.5f,y-.5f,.3f,1,1,0},{x-.5f,y+127.5f,.3f,1,0,1},{x+127.5f,y+127.5f,.3f,1,1,1}};ok(f.effectCoverageDraw([&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));}));}
            f.effectCoverage.endBatch();ok(d->EndScene());
        };
        for(unsigned n=0;n<8;++n)frame(n%2!=0);
        std::vector<double> gpu[2],cpu[2];
        for(unsigned block=0;block<6;++block)for(unsigned order=0;order<2;++order){const unsigned mode=(order+block)%2;IDirect3DQuery9 *first=nullptr,*last=nullptr,*frequency=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&first));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&last));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(frequency->Issue(D3DISSUE_END));ok(first->Issue(D3DISSUE_END));
            LARGE_INTEGER a{},b{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);for(unsigned n=0;n<12;++n)frame(mode!=0);QueryPerformanceCounter(&b);ok(last->Issue(D3DISSUE_END));UINT64 begin=0,end=0,rate=0;while(last->GetData(&end,sizeof end,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();ok(first->GetData(&begin,sizeof begin,0));ok(frequency->GetData(&rate,sizeof rate,0));assert(rate&&end>begin);gpu[mode].push_back(double(end-begin)*1000/rate/12);cpu[mode].push_back(double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart/12);first->Release();last->Release();frequency->Release();}
        std::vector<double> pairedGpu,pairedCpu;
        for(unsigned block=0;block<6;++block){pairedGpu.push_back(gpu[1][block]-gpu[0][block]);pairedCpu.push_back(cpu[1][block]-cpu[0][block]);std::printf("SMOKE_SKY_PERF_PAIR block=%u native_gpu_ms=%.5f mrt_gpu_ms=%.5f gpu_delta_ms=%.5f native_submit_ms=%.5f mrt_submit_ms=%.5f\n",block,gpu[0][block],gpu[1][block],pairedGpu.back(),cpu[0][block],cpu[1][block]);}
        for(unsigned mode=0;mode<2;++mode){std::sort(gpu[mode].begin(),gpu[mode].end());std::sort(cpu[mode].begin(),cpu[mode].end());}std::sort(pairedGpu.begin(),pairedGpu.end());std::sort(pairedCpu.begin(),pairedCpu.end());
        std::printf("SMOKE_SKY_PERF width=%u height=%u msaa=%u transparent_draws=%u native_gpu_ms=%.5f mrt_gpu_ms=%.5f paired_gpu_delta_ms=%.5f native_submit_ms=%.5f mrt_submit_ms=%.5f paired_submit_delta_ms=%.5f extra_draws=0 extra_images=0\n",BW,BH,samples,draws,gpu[0][3],gpu[1][3],pairedGpu[3],cpu[0][3],cpu[1][3],pairedCpu[3]);
        f.effectCoverage.reset();f.hdrSkyForeground.reset();bigNative->Release();bigSky->Release();bigDepth->Release();
    }
    std::printf("SMOKE_SKY_GPU_PASS msaa=%u cases=%u admission_guards=14 negative_controls=%u max_linear_error=%.8g native_rgba_unchanged=1 early_spell_drains=1 same_draw_mrt=%u restoration=%u fallback=5\n",samples,cases,negative,worst,mrt,restoration);
    return 0;
}
