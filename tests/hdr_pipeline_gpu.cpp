// Standalone composition graph: production HDR helpers/shaders, deterministic
// native inputs, native MSAA, material pass, projected effects and final UI.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_composite_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include "forwarders.h"
#include "hdr_frame.h"
#include "hdr_color.h"
#include "world_compiled_shaders.h"
#include "name_stencil.h"
#include <functional>

struct Calls {unsigned draws=0,resolves=0,targets=0,allocations=0;};
struct DeviceMeter:ForwardIDirect3DDevice9 {
    Calls calls;
    explicit DeviceMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s)override{++calls.draws;return real->DrawPrimitiveUP(t,n,p,s);}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* b,IDirect3DSurface9* c,const RECT* e,D3DTEXTUREFILTERTYPE f)override{++calls.resolves;return real->StretchRect(a,b,c,e,f);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{++calls.targets;return real->SetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* x)override{++calls.allocations;return real->CreateTexture(w,h,l,u,f,p,t,x);}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL l,IDirect3DSurface9** p,HANDLE* x)override{++calls.allocations;return real->CreateRenderTarget(w,h,f,m,q,l,p,x);}
};
template<class T>static void dropGraph(T*& p){if(p){p->Release();p=nullptr;}}
struct Surface {
    IDirect3DTexture9* texture=nullptr;IDirect3DSurface9* surface=nullptr;
    ~Surface(){dropGraph(surface);dropGraph(texture);}
    void make(IDirect3DDevice9* d,unsigned w,unsigned h,D3DFORMAT format,D3DMULTISAMPLE_TYPE samples,bool sampled=false){
        if(sampled){assert(samples==D3DMULTISAMPLE_NONE);ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&texture,nullptr));ok(texture->GetSurfaceLevel(0,&surface));}
        else ok(d->CreateRenderTarget(w,h,format,samples,0,FALSE,&surface,nullptr));
    }
};
struct Scenario {const char* name;unsigned w,h,samples,sprites,ui;bool material,sky,expansion,mask,automatic,bloom,mirror;bool combinedAllowed=true;};
struct Graph {
    DeviceMeter* d;Scenario c;NorthlightStateBlockPool pool;
    NorthlightHdr::Frame hdr;NorthlightHdr::Color color;NorthlightHdr::Settings settings;
    Surface native,legacy,output,material,materialSdr,sky;
    IDirect3DTexture9 *source=nullptr,*depth=nullptr,*lighting=nullptr,*baseline=nullptr,*fog=nullptr,*normal=nullptr,*distance=nullptr,*black=nullptr,*white=nullptr,*height=nullptr;
    IDirect3DSurface9* stencil=nullptr;
    IDirect3DPixelShader9 *copy=nullptr,*constant=nullptr,*combined=nullptr,*world[2]{},*detail[2]{};
    float bank[79][4]{};bool eligible=false;unsigned decodeDraws=0;
    Graph(DeviceMeter* device,const Scenario& scenario,const std::string& capture):d(device),c(scenario),pool(device){
        const auto ms=D3DMULTISAMPLE_TYPE(c.samples);
        native.make(d,c.w,c.h,D3DFMT_A8R8G8B8,ms);legacy.make(d,c.w,c.h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,true);
        output.make(d,c.w,c.h,D3DFMT_A16B16G16R16F,ms);materialSdr.make(d,c.w,c.h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,true);material.make(d,c.w,c.h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,true);
        if(c.sky)sky.make(d,c.w,c.h,D3DFMT_A16B16G16R16F,ms);
        if(c.mask)ok(d->CreateDepthStencilSurface(c.w,c.h,D3DFMT_D24S8,ms,0,TRUE,&stencil,nullptr));
        auto solid=[&](float r,float g,float b,float a){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float v[]={r,g,b,a};std::memcpy(im.pixels.data(),v,16);return texture(d,im);};
        black=solid(0,0,0,0);white=solid(1,1,1,1);baseline=solid(.5f,.5f,.5f,1);lighting=solid(2,1,.5f,1);
        fog=solid(.01f,.02f,.04f,.92f);normal=solid(0,0,1,1);height=solid(.7f,.6f,.1998f,1);distance=solid(.1998f,0,0,1);
        Image im{c.w,c.h,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(std::size_t(c.w)*c.h*4)};
        Image z{c.w,c.h,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(c.w)*c.h*4)};
        for(unsigned y=0;y<c.h;++y)for(unsigned x=0;x<c.w;++x){
            auto* p=im.pixels.data()+(std::size_t(y)*c.w+x)*4;p[0]=32+x*191/c.w;p[1]=16+y*223/c.h;p[2]=64+(x^y)%160;p[3]=255;
            float v=x<c.w/3?1.f:x<2*c.w/3?.9985f:.47f;std::memcpy(z.pixels.data()+(std::size_t(y)*c.w+x)*4,&v,4);
        }
        if(!capture.empty()){im=image(capture+"scene.fgr");z=image(capture+"depth.fgr");assert(im.width==c.w&&im.height==c.h&&im.bytes==4&&z.format==D3DFMT_R32F);}
        source=texture(d,im);depth=texture(d,z);
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copy));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
        ok(d->CreatePixelShader(kWorldCompositeShader,&world[0]));ok(d->CreatePixelShader(kWorldCompositeHdrShader,&world[1]));
        ok(d->CreatePixelShader(kMaterialCompositeShader,&detail[0]));ok(d->CreatePixelShader(kMaterialCompositeHdrShader,&detail[1]));
        D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));if(c.combinedAllowed&&!c.expansion&&caps.MaxPixelShader30InstructionSlots>=kWorldMaterialCompositeHdrShaderSlots)ok(d->CreatePixelShader(kWorldMaterialCompositeHdrShader,&combined));
        bank[0][0]=1.f/c.w;bank[0][1]=1.f/c.h;bank[0][2]=.1f;bank[0][3]=100;
        bank[1][0]=bank[1][1]=bank[1][2]=1;bank[2][0]=1/.94f;
        for(unsigned i=0;i<4;++i)bank[3+i][i]=1;
        bank[20][2]=bank[30][2]=1;bank[33][0]=c.w;bank[33][1]=c.h;bank[33][2]=c.w/2;bank[33][3]=c.h/2;
        bank[73][0]=.75f;bank[73][1]=48;bank[74][2]=1;bank[76][0]=.15f;bank[76][1]=.1f;bank[76][2]=.05f;
        settings.autoExposure=c.automatic;settings.bloom=c.bloom?1:0;settings.legacyExpansion=c.expansion?2:0;
        eligible=!c.sky&&!c.expansion&&!c.mask;
    }
    ~Graph(){
        hdr.reset();color.reset();pool.clear();
        for(auto* p:{source,depth,lighting,baseline,fog,normal,distance,black,white,height})if(p)p->Release();
        for(auto* p:{copy,constant,combined,world[0],world[1],detail[0],detail[1]})if(p)p->Release();dropGraph(stencil);
    }
    void quad(IDirect3DSurface9* target,IDirect3DPixelShader9* shader,IDirect3DTexture9* input){
        ok(NorthlightHdr::quadState(d,target,c.w,c.h,16));ok(d->SetPixelShader(shader));ok(d->SetTexture(0,input));
    }
    void rectangle(float x,float y,float w,float h){
        const Vertex q[]={{x-.5f,y-.5f,0,1,0,0},{x+w-.5f,y-.5f,0,1,1,0},{x-.5f,y+h-.5f,0,1,0,1},{x+w-.5f,y+h-.5f,0,1,1,1}};
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));
    }
    void maskBind(){
        if(!c.mask)return;ok(d->SetDepthStencilSurface(stencil));
        for(const auto& state:NorthlightNameStencil::kComposite)ok(d->SetRenderState(D3DRENDERSTATETYPE(state.state),state.value));
    }
    // mode 0: SDR composition; 1: previous full FP16 decode; 2: optimized HDR.
    void frame(unsigned mode,bool oracle=false){
        const bool useHdr=mode!=0,inlineBase=mode==2&&eligible;
        ok(d->BeginScene());quad(native.surface,copy,source);ok(NorthlightHdr::drawQuad(d,c.w,c.h));
        if(c.mask){
            ok(d->SetDepthStencilSurface(stencil));ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));
            for(const auto& state:NorthlightNameStencil::kMark)ok(d->SetRenderState(D3DRENDERSTATETYPE(state.state),state.value));
            rectangle(4,4,40,16);ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetDepthStencilSurface(nullptr));
        }
        ok(d->SetTexture(0,nullptr));ok(d->StretchRect(native.surface,nullptr,legacy.surface,nullptr,D3DTEXF_NONE));
        if(useHdr&&c.sky){
            ok(color.convertWorld(d,native.surface,sky.surface,legacy.texture,legacy.surface,c.w,c.h,80,80,&pool));
            quad(sky.surface,constant,nullptr);const float sun[]={6,4,2,1};ok(d->SetPixelShaderConstantF(0,sun,1));rectangle(12,40,48,48);
        }
        if(useHdr){
            const unsigned before=d->calls.draws;
            ok(hdr.prepareLegacy(d,legacy.texture,depth,c.sky?sky.surface:nullptr,c.w,c.h,0,.94f,settings,&pool,!inlineBase));
            decodeDraws=d->calls.draws-before;
            if(c.mask)ok(hdr.copyBase(d,output.surface,&pool));
        }
        auto* target=useHdr?output.surface:native.surface;
        const bool combinedMaterial=mode==2&&c.material&&combined;
        quad(c.material&&!combinedMaterial?(useHdr?material.surface:materialSdr.surface):target,combinedMaterial?combined:world[useHdr],legacy.texture);
        for(unsigned i=1;i<16;++i)ok(d->SetTexture(i,black));
        ok(d->SetTexture(1,depth));ok(d->SetTexture(2,useHdr?(inlineBase?legacy.texture:hdr.texture()):black));
        ok(d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,inlineBase));
        ok(d->SetTexture(8,lighting));ok(d->SetTexture(9,fog));ok(d->SetTexture(10,white));ok(d->SetTexture(12,baseline));ok(d->SetTexture(14,normal));ok(d->SetTexture(15,distance));
        if(combinedMaterial){ok(d->SetTexture(4,fog));ok(d->SetTexture(5,legacy.texture));ok(d->SetTexture(6,white));ok(d->SetTexture(9,height));}
        ok(d->SetPixelShaderConstantF(0,bank[0],79));if(!c.material||combinedMaterial)maskBind();ok(NorthlightHdr::drawQuad(d,c.w,c.h));
        if(c.material&&!combinedMaterial){
            quad(target,detail[useHdr],useHdr?material.texture:materialSdr.texture);ok(d->SetTexture(1,depth));ok(d->SetTexture(4,fog));ok(d->SetTexture(5,legacy.texture));
            ok(d->SetTexture(6,white));ok(d->SetTexture(8,lighting));ok(d->SetTexture(9,height));ok(d->SetTexture(10,white));ok(d->SetTexture(11,black));ok(d->SetTexture(12,baseline));ok(d->SetTexture(14,normal));
            ok(d->SetPixelShaderConstantF(0,bank[0],79));maskBind();ok(NorthlightHdr::drawQuad(d,c.w,c.h));
        }
        quad(target,constant,nullptr);ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        const float sprite[]={.7f,.3f,.1f,.2f};ok(d->SetPixelShaderConstantF(0,sprite,1));
        for(unsigned n=0;n<c.sprites;++n){auto draw=[&]{rectangle(float((n*113)%(c.w-70)),float((n*53)%(c.h-70)),64,64);return S_OK;};if(useHdr)ok(color.draw(d,80,draw));else ok(draw());}
        if(useHdr){ok(hdr.finish(d,target,output.surface,settings,1.f/60,true,oracle,.18f,&pool));if(c.mirror)ok(hdr.mirrorLegacy(d,native.surface,&pool));}
        quad(target,constant,nullptr);const float uiColor[]={.95f,.75f,.1f,1};ok(d->SetPixelShaderConstantF(0,uiColor,1));
        for(unsigned n=0;n<c.ui;++n){auto draw=[&]{rectangle(8+float(n%8)*42,float(c.h-40-n/8*22),40,20);return S_OK;};if(useHdr)ok(color.draw(d,160,draw));else ok(draw());}
        // Native fixed-function cooldown/dimming masks use only source alpha.
        // Force the former compatibility layer as the reference, including its
        // unchanged destination-alpha policy; candidate uses original geometry.
        quad(target,nullptr,nullptr);ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_TEXTUREFACTOR,0x40000000));
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TFACTOR));
        ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR));
        ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        for(unsigned n=0;n<3;++n){auto draw=[&]{rectangle(8+float(n)*64,8,48,48);return S_OK;};
            if(mode==1)ok(color.legacyLayer(d,160,draw));else if(useHdr)ok(color.draw(d,160,draw));else ok(draw());}
        quad(target,constant,nullptr);ok(d->SetPixelShaderConstantF(0,uiColor,1));
        // A final opaque counter strip deliberately crosses clear sky/horizon.
        auto counter=[&]{rectangle(float(c.w/6-24),float(c.h/2),float(c.w/3),12);return S_OK;};
        if(useHdr)ok(color.draw(d,160,counter));else ok(counter());ok(d->EndScene());
    }
    std::vector<std::uint16_t> pixels(){
        Surface resolved;resolved.make(d,c.w,c.h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE);
        ok(d->StretchRect(output.surface,nullptr,resolved.surface,nullptr,D3DTEXF_NONE));
        IDirect3DSurface9* readback=nullptr;ok(d->CreateOffscreenPlainSurface(c.w,c.h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        ok(d->GetRenderTargetData(resolved.surface,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<std::uint16_t> out(std::size_t(c.w)*c.h*4);for(unsigned y=0;y<c.h;++y)std::memcpy(out.data()+std::size_t(y)*c.w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,c.w*8);
        ok(readback->UnlockRect());readback->Release();return out;
    }
};
struct Timing {double gpu=0,cpu=0;};
static Timing timeGraph(IDirect3DDevice9* d,const std::function<void()>& submit,unsigned count){
    IDirect3DQuery9 *first=nullptr,*last=nullptr,*frequency=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&first));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&last));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    for(unsigned n=0;n<4;++n)submit();ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(first->Issue(D3DISSUE_END));
    LARGE_INTEGER a{},b{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);for(unsigned n=0;n<count;++n)submit();QueryPerformanceCounter(&b);ok(last->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));
    UINT64 start=0,end=0,freq=0;BOOL bad=TRUE;const auto deadline=GetTickCount64()+30000;
    while(last->GetData(&end,sizeof end,D3DGETDATA_FLUSH)==S_FALSE){assert(GetTickCount64()<deadline);SwitchToThread();}
    ok(first->GetData(&start,sizeof start,0));ok(frequency->GetData(&freq,sizeof freq,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&freq&&end>start);
    first->Release();last->Release();frequency->Release();disjoint->Release();return {double(end-start)*1000/freq/count,double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart/count};
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);const std::string capture=argc==3?argv[2]:"";
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    D3DCAPS9 caps{};ok(raw->GetDeviceCaps(&caps));std::printf("{\"event\":\"caps\",\"ps30_slots\":%lu}\n",(unsigned long)caps.MaxPixelShader30InstructionSlots);
    DeviceMeter meter(raw);
    std::vector<Scenario> scenarios={
        {"world-1080",1920,1080,0,0,16,false,false,false,false,true,true,false},
        {"world-4k-msaa4",3840,2160,4,0,16,false,false,false,false,true,true,false},
        {"material-particles-4k-msaa8",3840,2160,8,96,64,true,false,false,false,true,true,false},
        {"manual-no-bloom-4k",3840,2160,0,0,0,true,false,false,false,false,false,false},
        {"sky-4k-msaa8",3840,2160,8,32,16,true,true,false,false,true,true,false},
        {"expansion-1080",1920,1080,4,32,16,true,false,true,false,true,true,false},
        {"stencil-1080",1920,1080,8,16,16,true,false,false,true,true,true,false},
        {"sdr-copy-4k-msaa8",3840,2160,8,32,16,true,false,false,false,true,true,true},
        {"material-fallback-1080",1920,1080,0,0,16,true,false,false,false,true,true,false,false}};
    if(!capture.empty()){const auto im=image(capture+"scene.fgr");scenarios.push_back({"captured-native-input",im.width,im.height,8,96,32,true,false,false,false,true,true,false});}
    for(const auto& scenario:scenarios){
        Graph graph(&meter,scenario,std::strcmp(scenario.name,"captured-native-input")==0?capture:"");
        graph.frame(1,true);auto reference=graph.pixels();graph.frame(2,true);auto candidate=graph.pixels();assert(reference.size()==candidate.size());
        if(scenario.material){
            const float gain=graph.bank[73][0];graph.bank[73][0]=0;graph.frame(1,true);const auto withoutMaterial=graph.pixels();graph.bank[73][0]=gain;
            float contribution=0;for(std::size_t i=0;i<reference.size();++i)if(i%4!=3)contribution=std::max(contribution,std::fabs(half(reference[i])-half(withoutMaterial[i])));
            assert(contribution>.001f); // Dropping material work cannot pass equality.
        }
        double error=0;unsigned nonfinite=0,alpha=0;
        for(std::size_t i=0;i<reference.size();++i){const double delta=std::fabs(double(half(reference[i]))-half(candidate[i]));nonfinite+=!std::isfinite(delta);error=std::max(error,delta);alpha+=i%4==3&&reference[i]!=candidate[i];}
        assert(!nonfinite&&!alpha&&error<.012);
        const std::size_t counter=(std::size_t(scenario.h/2+4)*scenario.w+scenario.w/6)*4;
        assert(half(candidate[counter])>1.7f&&half(candidate[counter+1])>1);
        // Warm both paths, then check exact production-helper work, not estimates.
        graph.frame(2);meter.calls={};graph.frame(2);const Calls work=meter.calls;assert(!work.allocations);
        const unsigned optimizedDecode=graph.decodeDraws;assert(!graph.eligible||optimizedDecode==0);
        meter.calls={};graph.frame(1);const Calls oldWork=meter.calls;
        assert(oldWork.draws==work.draws+3+(graph.eligible?1:0)+(scenario.material&&graph.combined?1:0));if(graph.eligible)assert(graph.decodeDraws==1);
        std::vector<double> gpu[3],cpu[3];
        for(unsigned block=0;block<6;++block)for(unsigned n=0;n<3;++n){const unsigned mode=block%2?2-n:n;const auto t=timeGraph(raw,[&]{graph.frame(mode);},12);gpu[mode].push_back(t.gpu);cpu[mode].push_back(t.cpu);}
        double medGpu[3],medCpu[3];for(unsigned n=0;n<3;++n){std::sort(gpu[n].begin(),gpu[n].end());std::sort(cpu[n].begin(),cpu[n].end());medGpu[n]=(gpu[n][2]+gpu[n][3])*.5;medCpu[n]=(cpu[n][2]+cpu[n][3])*.5;}
        std::printf("{\"event\":\"scenario\",\"name\":\"%s\",\"width\":%u,\"height\":%u,\"samples\":%u,\"sprites\":%u,\"ui\":%u,\"inline_base\":%s,\"sdr_gpu_ms\":%.6f,\"reference_gpu_ms\":%.6f,\"optimized_gpu_ms\":%.6f,\"sdr_cpu_submit_ms\":%.6f,\"reference_cpu_submit_ms\":%.6f,\"optimized_cpu_submit_ms\":%.6f,\"max_abs_scrgb\":%.9g,\"alpha_changes\":%u,\"nonfinite\":%u,\"draws_reference\":%u,\"draws_optimized\":%u,\"resolves\":%u,\"allocations_warm\":%u,\"counter_over_sky\":true}\n",scenario.name,scenario.w,scenario.h,scenario.samples,scenario.sprites,scenario.ui,graph.eligible?"true":"false",medGpu[0],medGpu[1],medGpu[2],medCpu[0],medCpu[1],medCpu[2],error,alpha,nonfinite,oldWork.draws,work.draws,work.resolves,work.allocations);std::fflush(stdout);
    }
    raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);std::puts("HDR_PIPELINE_GPU_PASS");
}
