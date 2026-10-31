// Same hidden-device/readback utilities used by the production shader oracles.
#undef NDEBUG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_replay_unused
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "hdr_presentation.h"
#include "hdr_color.h"

static double decode(double x){x=std::max(x,0.);return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
struct UiVertex {float x,y,z,w;float r,g,b,a;float u,v;};
static void vertices(UiVertex* v,const float* color){
    const float xy[][2]={{-1,1},{1,1},{-1,-1},{1,-1}};
    for(unsigned i=0;i<4;++i)v[i]={xy[i][0],xy[i][1],0,1,color[0],color[1],color[2],color[3],.5f,.5f};
}
static void pixel(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* staging,float* out,bool fp16=false){
    ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    // The fixture's clip-space quad has partial MSAA coverage at its outer
    // corners. Uniform-colour checks use an interior texel; the edge test below
    // separately checks every pixel's resolved coverage.
    const auto* at=static_cast<const char*>(lock.pBits)+2*lock.Pitch+2*(fp16?8:16);
    if(fp16){for(unsigned i=0;i<4;++i){std::uint16_t h;std::memcpy(&h,at+2*i,2);out[i]=half(h);}}
    else std::memcpy(out,at,16);ok(staging->UnlockRect());
}
static void pixel8(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* staging,float* out){
    D3DSURFACE_DESC desc{};ok(target->GetDesc(&desc));IDirect3DSurface9* resolved=nullptr;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){ok(d->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));target=resolved;}
    ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    const auto* p=static_cast<const unsigned char*>(lock.pBits)+2*lock.Pitch+8;out[0]=p[2]/255.f;out[1]=p[1]/255.f;out[2]=p[0]/255.f;out[3]=1;ok(staging->UnlockRect());
    if(resolved)resolved->Release();
}
static void patchChecks(const std::vector<unsigned char>& bytes){
    const auto* input=reinterpret_cast<const std::uint32_t*>(bytes.data());const auto n=bytes.size()/4;
    const auto* transfer=input[0]==0xffff0200?kHdrUiPs2:kHdrUiPs3;const auto tn=input[0]==0xffff0200?sizeof(kHdrUiPs2)/4:sizeof(kHdrUiPs3)/4;
    std::vector<std::uint32_t> out;NorthlightHdrUi::Info info;
    assert(NorthlightHdrUi::patch(input,n,transfer,tn,160,false,out,&info));
    assert(info.captureTemp<(info.major==2?12u:32u)&&info.scaleConstant<(info.major==2?32u:224u));
    for(std::size_t i=0;i<n;++i)assert(!NorthlightHdrUi::patch(input,i,transfer,tn,160,false,out));
    auto malformed=std::vector<std::uint32_t>(input,input+n);malformed.back()=0;assert(!NorthlightHdrUi::patch(malformed.data(),n,transfer,tn,160,false,out));
    assert(!NorthlightHdrUi::patch(input,n,transfer,tn,NAN,false,out));
    assert(!NorthlightHdrUi::patch(input,n,transfer,tn,0,false,out));
}
static LONG WINAPI crash(EXCEPTION_POINTERS* e){HMODULE module=nullptr;char name[MAX_PATH*4]={};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<const char*>(e->ExceptionRecord->ExceptionAddress),&module);GetModuleFileNameA(module,name,sizeof name);std::fprintf(stderr,"CRASH exception=%08lx module=%s RVA=%llx address=%p\n",e->ExceptionRecord->ExceptionCode,name,(unsigned long long)(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(module)),e->ExceptionRecord->ExceptionAddress);std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;}
int main(int argc,char** argv){
    SetUnhandledExceptionFilter(&crash);
    assert(argc==4||argc==5);const bool native=argc==5&&!std::strcmp(argv[4],"--native");const bool proxy=argc==5&&!native;
    const bool performance=proxy&&!std::strcmp(argv[4],"--perf-proxy");
    const bool small=proxy&&!std::strcmp(argv[4],"--small-proxy");
    const bool full4k=proxy&&!std::strcmp(argv[4],"--msaa2-4k-proxy");
    const bool msaa2=full4k||(proxy&&(!std::strcmp(argv[4],"--msaa2-proxy")||!std::strcmp(argv[4],"--msaa2-triple-proxy")));
    const bool msaa4=proxy&&(!std::strcmp(argv[4],"--msaa4-proxy")||!std::strcmp(argv[4],"--msaa4-graded-proxy"));
    const bool msaa8=proxy&&(!std::strcmp(argv[4],"--msaa8-proxy")||!std::strcmp(argv[4],"--msaa8-triple-proxy"));
    const bool graded=proxy&&(!std::strcmp(argv[4],"--graded-proxy")||!std::strcmp(argv[4],"--msaa4-graded-proxy"));
    const bool triple=full4k||graded||(proxy&&(!std::strcmp(argv[4],"--triple-proxy")||!std::strcmp(argv[4],"--msaa2-triple-proxy")||!std::strcmp(argv[4],"--msaa8-triple-proxy")));
    const auto samples=msaa2?D3DMULTISAMPLE_2_SAMPLES:msaa4?D3DMULTISAMPLE_4_SAMPLES:msaa8?D3DMULTISAMPLE_8_SAMPLES:D3DMULTISAMPLE_NONE;
    const bool hdrExpected=!native&&(!proxy||small||triple||msaa2||msaa4||msaa8||performance||!std::strcmp(argv[4],"--proxy"));
    assert(!proxy||hdrExpected||!std::strcmp(argv[4],"--sdr-proxy"));
    std::vector<std::vector<unsigned char>> programs={read(std::string(argv[2])+"/HdrFixturePS-2.bin"),read(std::string(argv[2])+"/HdrFixturePS-3.bin"),read(argv[3])};
    for(const auto& code:programs)patchChecks(code);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightHdrIntegrationOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    const unsigned backWidth=full4k?3840:small?320:640,backHeight=full4k?2160:small?180:360;
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=backWidth;pp.BackBufferHeight=backHeight;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    if(triple)pp.BackBufferCount=2;
    pp.MultiSampleType=samples;pp.EnableAutoDepthStencil=samples!=D3DMULTISAMPLE_NONE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    NorthlightHdr::Presentation presentation;
    if(!proxy&&!native)presentation.formatsPrepared=NorthlightHdr::unlockFormats(api);
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|D3DCREATE_MULTITHREADED,&pp,&d));assert(pp.BackBufferFormat==D3DFMT_X8R8G8B8);
    if(!proxy&&!native){presentation.settings.requested=true;presentation.logical=pp;presentation.stream=presentation.dxvk=true;assert(presentation.activate(d,d)==S_OK&&presentation.active);}
    IDirect3DSwapChain9* swap=nullptr;ok(d->GetSwapChain(0,&swap));D3DPRESENT_PARAMETERS exposed{};ok(swap->GetPresentParameters(&exposed));
    assert(exposed.BackBufferFormat==(!proxy&&hdrExpected?D3DFMT_A16B16G16R16F:D3DFMT_X8R8G8B8));swap->Release();
    assert(exposed.MultiSampleType==samples);
    if(triple){assert(exposed.BackBufferCount==2);IDirect3DSurface9* second=nullptr;ok(d->GetBackBuffer(0,1,D3DBACKBUFFER_TYPE_MONO,&second));D3DSURFACE_DESC desc{};ok(second->GetDesc(&desc));assert(desc.Format==D3DFMT_X8R8G8B8&&desc.MultiSampleType==samples);second->Release();}
    IDirect3DSurface9* initialBack=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&initialBack));D3DSURFACE_DESC physical{};ok(initialBack->GetDesc(&physical));initialBack->Release();
    // CommandStream exposes the game's original backbuffer description.
    assert(physical.Format==(!proxy&&hdrExpected?D3DFMT_A16B16G16R16F:D3DFMT_X8R8G8B8));
    assert(physical.MultiSampleType==samples);
    if(samples!=D3DMULTISAMPLE_NONE){IDirect3DSurface9* depth=nullptr;ok(d->GetDepthStencilSurface(&depth));D3DSURFACE_DESC desc{};ok(depth->GetDesc(&desc));assert(desc.MultiSampleType==samples);depth->Release();}
    constexpr unsigned size=8;
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    IDirect3DPixelShader9* ps[3]{};IDirect3DVertexShader9* vs[2]{};
    for(unsigned i=0;i<3;++i)ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(programs[i].data()),&ps[i]));
    for(unsigned i=0;i<2;++i){auto code=read(std::string(argv[2])+"/HdrFixtureVS-"+std::to_string(i+2)+".bin");ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vs[i]));}
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(elements,&declaration));ok(d->SetVertexDeclaration(declaration));
    float texel[]={.5f,.7f,.9f,.8f};Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};std::memcpy(im.pixels.data(),texel,16);auto* tex=texture(d,im);ok(d->SetTexture(0,tex));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
    const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};ok(d->SetVertexShaderConstantF(0,matrix,4));
    const float font[]={1,1,1,1},sentinel[]={.31f,.47f,.62f,.89f};ok(d->SetPixelShaderConstantF(222,font,1));ok(d->SetPixelShaderConstantF(223,sentinel,1));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
    NorthlightHdr::Color color;unsigned cases=0,failures=0;double maximum=0;
    for(unsigned program=0;program<3;++program)for(float value:{.02f,.04045f,.5f,1.f})for(float alpha:{0.f,.25f,.5f,1.f})for(unsigned blend=0;blend<4;++blend){
        ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 viewport={0,0,size,size,0,1};ok(d->SetViewport(&viewport));ok(d->SetVertexShader(vs[program==0?0:1]));ok(d->SetPixelShader(ps[program]));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        float input[]={value,value*.7f,value*.3f,alpha};UiVertex quad[4];vertices(quad,input);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,51,102,153),1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
        float original[4];pixel(d,target,staging,original);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,51,102,153),1,0));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));ok(d->SetRenderState(D3DRS_SRCBLEND,blend==1?D3DBLEND_SRCALPHA:(blend==3?D3DBLEND_DESTCOLOR:D3DBLEND_ONE)));ok(d->SetRenderState(D3DRS_DESTBLEND,blend==3?D3DBLEND_ZERO:D3DBLEND_INVSRCALPHA));
        ok(d->BeginScene());ok(color.draw(d,160,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex));}));ok(d->EndScene());float actual[4];pixel(d,target,staging,actual);
        for(unsigned c=0;c<4;++c){double a=original[3],expected;
            if(c==3)expected=blend==0?a:(blend==1?a*a+1-a:(blend==2?1:a));
            else{double background=(c+1)*.2,source=decode(std::min(1.,blend==2?original[c]/std::max(a,.000001):original[c]))*(blend==3?1:2);
                if(blend==2)source*=a;expected=blend==0?source:(blend==1?source*a+background*(1-a):(blend==2?source+background*(1-a):source*background));}
            double error=std::fabs(actual[c]-expected);maximum=std::max(maximum,error);
            if(!std::isfinite(actual[c])||error>3e-5){if(failures++<4)std::printf("FAIL shader=%u blend=%u alpha=%.2f channel=%u got=%.9g want=%.9g\n",program,blend,alpha,c,actual[c],expected);}
        }
        IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==ps[program]);restored->Release();float check[4];ok(d->GetPixelShaderConstantF(223,check,1));assert(!std::memcmp(check,sentinel,16));++cases;
    }
    assert(color.rejected==0);
    // The display peak limits world content while preserving hue, alpha and UI white.
    IDirect3DTexture9* worldCopy=nullptr;IDirect3DSurface9 *worldCopySurface=nullptr,*worldSource=nullptr;
    ok(d->CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&worldCopy,nullptr));ok(worldCopy->GetSurfaceLevel(0,&worldCopySurface));
    ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&worldSource,nullptr));
    for(float paper:{80.f,203.f,400.f})for(float peak:{80.f,160.f,1000.f}){
        ok(d->SetRenderTarget(0,worldSource));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(128,255,128,64),1,0));
        ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
        ok(d->BeginScene());ok(color.convertWorld(d,worldSource,target,worldCopy,worldCopySurface,size,size,paper,peak));ok(d->EndScene());
        float actual[4];pixel(d,target,staging,actual);
        for(unsigned c=0;c<3;++c){const double encoded=c==0?1:c==1?128./255:64./255;const double expected=decode(encoded)*std::min(paper,peak)/80.;const double error=std::fabs(actual[c]-expected);maximum=std::max(maximum,error);if(!std::isfinite(actual[c])||error>3e-5)++failures;}
        assert(std::fabs(actual[3]-128./255)<.00001);++cases;
    }
    for(float contrast:{.5f,1.f,2.f})for(float exposure:{-4.f,-1.25f,0.f,1.f,4.f})for(float peak:{80.f,1000.f}){
        ok(d->SetRenderTarget(0,worldSource));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(128,255,128,64),1,0));
        ok(d->SetRenderTarget(0,target));ok(d->BeginScene());ok(color.convertWorld(d,worldSource,target,worldCopy,worldCopySurface,size,size,203,peak,nullptr,contrast,exposure));ok(d->EndScene());
        float actual[4];pixel(d,target,staging,actual);const double green=decode(128./255),blue=decode(64./255);
        const double luminance=.2126+.7152*green+.0722*blue;
        const double scale=std::min(std::pow(luminance/.18,contrast-1)*std::exp2(exposure)*203/80,peak/80.);
        for(unsigned c=0;c<3;++c){const double expected=(c==0?1:c==1?green:blue)*scale;const double error=std::fabs(actual[c]-expected);maximum=std::max(maximum,error);if(!std::isfinite(actual[c])||error>.0001)++failures;}
        assert(std::fabs(actual[3]-128./255)<.00001);++cases;
    }
    worldSource->Release();worldCopySurface->Release();worldCopy->Release();
    if(proxy&&samples!=D3DMULTISAMPLE_NONE){
        IDirect3DSurface9 *back=nullptr,*resolved=nullptr,*legacyRead=nullptr,*hdrRead=nullptr;
        ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
        ok(d->CreateRenderTarget(backWidth,backHeight,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->CreateOffscreenPlainSurface(backWidth,backHeight,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&legacyRead,nullptr));
        ok(d->CreateOffscreenPlainSurface(backWidth,backHeight,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&hdrRead,nullptr));
        ok(d->SetRenderTarget(0,back));D3DVIEWPORT9 vp={0,0,backWidth,backHeight,0,1};ok(d->SetViewport(&vp));
        ok(d->SetVertexShader(vs[1]));ok(d->SetPixelShader(ps[1]));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
        ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        float white[]={2,2,2,1};UiVertex quad[4];vertices(quad,white);
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,quad,sizeof(UiVertex)));ok(d->EndScene());
        ok(d->StretchRect(back,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,legacyRead));
        D3DLOCKED_RECT bytes{};ok(legacyRead->LockRect(&bytes,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> red(backWidth*backHeight);
        unsigned partial=0;for(unsigned y=0;y<backHeight;++y)for(unsigned x=0;x<backWidth;++x){
            const auto value=static_cast<const unsigned char*>(bytes.pBits)[y*bytes.Pitch+x*4+2];red[y*backWidth+x]=value;partial+=value>3&&value<250;
        }ok(legacyRead->UnlockRect());assert(partial>20);
        // Transparent UI crosses the real HDR boundary without changing world radiance.
        ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        float transparent[]={1,1,1,0};vertices(quad,transparent);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
        ok(d->GetRenderTargetData(back,hdrRead));D3DLOCKED_RECT hdrPixels{};ok(hdrRead->LockRect(&hdrPixels,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<backHeight;++y)for(unsigned x=0;x<backWidth;++x){
            std::uint16_t encoded;std::memcpy(&encoded,static_cast<const char*>(hdrPixels.pBits)+y*hdrPixels.Pitch+x*8,2);
            const double linear=decode(red[y*backWidth+x]/255.),grade=graded?std::pow(std::max(linear,.000001)/.18,.35)*std::exp2(-1.25):1;
            const double error=std::fabs(half(encoded)-linear*grade*203/80);maximum=std::max(maximum,error);if(error>.003)++failures;
        }ok(hdrRead->UnlockRect());ok(d->Present(nullptr,nullptr,nullptr,nullptr));++cases;
        std::printf("MSAA edge coverage samples=%u fractionalPixels=%u HDR curve preserved=1\n",unsigned(samples),partial);
        hdrRead->Release();legacyRead->Release();resolved->Release();back->Release();
    }
    // Full Northlight + CommandStream path: legacy game format stays visible,
    // first screen-space UI draw converts the world and preserves UI alpha.
    if(proxy&&hdrExpected){
        IDirect3DSurface9 *back=nullptr,*readback=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));ok(d->CreateOffscreenPlainSurface(backWidth,backHeight,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        IDirect3DSurface9 *reference=nullptr,*referenceRead=nullptr;ok(d->CreateRenderTarget(backWidth,backHeight,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&reference,nullptr));ok(d->CreateOffscreenPlainSurface(backWidth,backHeight,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&referenceRead,nullptr));
        // A full-size UI texture is not the primary backbuffer or its HDR boundary.
        IDirect3DSurface9* offscreenUi=nullptr;ok(d->CreateRenderTarget(backWidth,backHeight,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&offscreenUi,nullptr));
        ok(d->SetRenderTarget(0,back));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(51,102,153),1,0));
        ok(d->SetRenderTarget(0,offscreenUi));D3DVIEWPORT9 offscreenVp={0,0,backWidth,backHeight,0,1};ok(d->SetViewport(&offscreenVp));ok(d->SetVertexShader(vs[1]));ok(d->SetPixelShader(ps[1]));
        float offscreenColor[]={1,1,1,1};UiVertex offscreenQuad[4];vertices(offscreenQuad,offscreenColor);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,offscreenQuad,sizeof(UiVertex)));ok(d->EndScene());
        float unchanged[4];pixel8(d,back,referenceRead,unchanged);assert(std::fabs(unchanged[0]-.2)<.0001);
        ok(d->SetRenderTarget(0,back));offscreenUi->Release();
        if(!performance){
            // A native death/glow scene quad may use the same ortho VS as UI.
            // Keep its render-target input in the legacy domain until atlas UI:
            // the game's following scene copy must observe this draw, not the old clear.
            IDirect3DTexture9* sceneInput=nullptr;IDirect3DSurface9* sceneInputSurface=nullptr;
            ok(d->CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&sceneInput,nullptr));
            ok(sceneInput->GetSurfaceLevel(0,&sceneInputSurface));ok(d->ColorFill(sceneInputSurface,nullptr,D3DCOLOR_XRGB(204,51,102)));
            ok(d->SetTexture(0,sceneInput));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            float sceneWhite[]={1,1,1,1};UiVertex sceneQuad[4];vertices(sceneQuad,sceneWhite);
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,sceneQuad,sizeof(UiVertex)));ok(d->EndScene());
            ok(d->StretchRect(back,nullptr,reference,nullptr,D3DTEXF_NONE));
            float copiedScene[4];pixel8(d,reference,referenceRead,copiedScene);
            for(unsigned c=0;c<3;++c){const double expected=c==0?.8:c==1?.2:.4,error=std::fabs(copiedScene[c]-expected);maximum=std::max(maximum,error);
                if(!std::isfinite(copiedScene[c])||error>.004){if(failures++<4)std::printf("FAIL native scene copy before HDR UI channel=%u got=%.8f want=%.8f\n",c,copiedScene[c],expected);}}
            ++cases;ok(d->SetTexture(0,tex));sceneInputSurface->Release();sceneInput->Release();
        }
        for(unsigned program:{1u,2u}){
            ok(d->SetRenderTarget(0,back));D3DVIEWPORT9 viewport={0,0,backWidth,backHeight,0,1};ok(d->SetViewport(&viewport));ok(d->SetVertexShader(vs[1]));ok(d->SetPixelShader(ps[program]));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,51,102,153),1,0));
            ok(d->SetRenderTarget(0,reference));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(51,102,153),1,0));ok(d->SetRenderTarget(0,back));
            if(program==2){
                ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetPixelShader(ps[1]));
                for(auto* legacyTarget:{back,reference}){
                    ok(d->SetRenderTarget(0,legacyTarget));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
                    float bright[]={3,2,1,1};UiVertex worldQuad[4];vertices(worldQuad,bright);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,worldQuad,sizeof(UiVertex)));ok(d->EndScene());
                    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO));
                    float modulation[]={1,1,1,1};vertices(worldQuad,modulation);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,worldQuad,sizeof(UiVertex)));ok(d->EndScene());
                }
                ok(d->SetRenderTarget(0,back));ok(d->SetPixelShader(ps[program]));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
                ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
            }
            float before[4];pixel8(d,reference,referenceRead,before);
            float input[]={.5f,.35f,.15f,.5f};UiVertex quad[4];vertices(quad,input);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
            // Switching away and back after the HDR UI boundary must retain Device interception.
            ok(d->SetRenderTarget(0,reference));ok(d->SetRenderTarget(0,back));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
            float actual[4];pixel(d,back,readback,actual,true);
            // Re-evaluate the original UI on the separate floating-point target.
            ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 small={0,0,size,size,0,1};ok(d->SetViewport(&small));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());float original[4];pixel(d,target,staging,original);
            const double luminance=std::max(.2126*decode(before[0])+.7152*decode(before[1])+.0722*decode(before[2]),.000001);
            const double worldGrade=graded?std::pow(luminance/.18,.35)*std::exp2(-1.25):1;
            for(unsigned c=0;c<3;++c){double expected=decode(before[c])*worldGrade*(203.0/80)*std::pow(1-original[3],2)+decode(original[c])*2*(1-std::pow(1-original[3],2));double error=std::fabs(actual[c]-expected);maximum=std::max(maximum,error);if(!std::isfinite(actual[c])||error>.002){if(failures++<4)std::printf("FAIL proxy UI shader=%u channel=%u got=%.8f want=%.8f\n",program,c,actual[c],expected);}}
            ok(d->SetRenderTarget(0,back));
            if(program==1){
                // Rebinding the logical backbuffer after an offscreen draw must retain the HDR UI target.
                D3DVIEWPORT9 full={0,0,backWidth,backHeight,0,1};ok(d->SetViewport(&full));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
                ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
                float rebound[4];pixel(d,back,readback,rebound,true);
                for(unsigned c=0;c<3;++c){const double expected=decode(original[c])*2,error=std::fabs(rebound[c]-expected);maximum=std::max(maximum,error);if(!std::isfinite(rebound[c])||error>.002){if(failures++<4)std::printf("FAIL HDR UI target rebind channel=%u got=%.8f want=%.8f\n",c,rebound[c],expected);}}
                ++cases;
            }
            if(program==1){D3DVIEWPORT9 inset={4,4,backWidth-8,backHeight-8,0,1};RECT clip={4,4,LONG(backWidth-4),LONG(backHeight-4)};ok(d->SetViewport(&inset));ok(d->SetScissorRect(&clip));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));}
            if(program==1)ok(d->Present(nullptr,nullptr,nullptr,nullptr));
            else{IDirect3DSwapChain9* primary=nullptr;ok(d->GetSwapChain(0,&primary));ok(primary->Present(nullptr,nullptr,nullptr,nullptr,0));primary->Release();}
            if(program==1){
                ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));float white[]={1,1,1,1};vertices(quad,white);
                ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
                float outside[4];pixel8(d,back,referenceRead,outside);assert(std::fabs(outside[0]-.2)<.0001);ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            }
            ++cases;
        }
        // Alternate complete frames without rebinding RT0. Present must return
        // rendering to the logical target, including SwapChain::Present.
        ok(d->SetRenderTarget(0,back));ok(d->SetViewport(&offscreenVp));
        ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->SetVertexShader(vs[1]));ok(d->SetPixelShader(ps[1]));
        ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        for(unsigned frame=0;frame<12;++frame){
            const unsigned red=frame%2?204:51;ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(red,102,153),1,0));
            float input[]={frame%2?.15f:.7f,.35f,.15f,.5f};UiVertex quad[4];vertices(quad,input);
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
            float actual[4];pixel(d,back,readback,actual,true);
            const double background[]={red/255.,102/255.,153/255.};
            const double luminance=std::max(.2126*decode(background[0])+.7152*decode(background[1])+.0722*decode(background[2]),.000001);
            const double worldGrade=graded?std::pow(luminance/.18,.35)*std::exp2(-1.25):1;
            const double alpha=input[3]*texel[3];
            for(unsigned c=0;c<3;++c){const double expected=decode(background[c])*worldGrade*(203./80)*(1-alpha)+decode(input[c]*texel[c])*2*alpha;
                const double error=std::fabs(actual[c]-expected);maximum=std::max(maximum,error);
                if(!std::isfinite(actual[c])||error>.002){if(failures++<4)std::printf("FAIL repeated HDR frame=%u channel=%u got=%.8f want=%.8f\n",frame,c,actual[c],expected);}}
            if(frame%2){IDirect3DSwapChain9* primary=nullptr;ok(d->GetSwapChain(0,&primary));ok(primary->Present(nullptr,nullptr,nullptr,nullptr,0));primary->Release();}
            else ok(d->Present(nullptr,nullptr,nullptr,nullptr));
            ++cases;
        }
        // Common atlas-only picture in both revisions; one admitted boundary per frame.
        // CPU submission is command recording; completion includes replay and GPU work.
        if(performance){
            IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
            ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));
            ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            ok(d->SetRenderTarget(0,back));ok(d->SetViewport(&offscreenVp));ok(d->SetVertexShader(vs[1]));ok(d->SetPixelShader(ps[1]));ok(d->SetTexture(0,tex));
            float input[]={.5f,.35f,.15f,.5f};UiVertex quad[4];vertices(quad,input);
            std::vector<double> submitTimes,gpuTimes,wallTimes;LARGE_INTEGER ticks{};QueryPerformanceFrequency(&ticks);
            constexpr unsigned warmFrames=32,measuredFrames=96,uiDraws=128;unsigned discarded=0;
            for(unsigned frame=0;frame<warmFrames+measuredFrames;++frame){
                wait(event);LARGE_INTEGER before{},submitted{},finished{};QueryPerformanceCounter(&before);
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));
                ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(51+frame%2,102,153),1,0));
                ok(d->BeginScene());for(unsigned draw=0;draw<uiDraws;++draw)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(UiVertex)));ok(d->EndScene());
                QueryPerformanceCounter(&submitted);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));
                ok(d->Present(nullptr,nullptr,nullptr,nullptr));wait(event);QueryPerformanceCounter(&finished);
                UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;
                const bool ready=start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&hz,sizeof hz,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK;
                if(!ready||bad||b<a||!hz){if(frame>=warmFrames)++discarded;continue;}
                if(frame>=warmFrames){submitTimes.push_back(double(submitted.QuadPart-before.QuadPart)*1e6/ticks.QuadPart);gpuTimes.push_back(double(b-a)*1e6/hz);wallTimes.push_back(double(finished.QuadPart-before.QuadPart)*1e6/ticks.QuadPart);}
            }
            assert(gpuTimes.size()>=measuredFrames*5/6);
            auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());const auto middle=values.size()/2;return values.size()%2?values[middle]:(values[middle-1]+values[middle])*.5;};
            std::printf("HDR_GUARD_PERF frames=%u discarded=%u uiDraws=%u width=%u height=%u submitMedianUs=%.6f gpuMedianUs=%.6f completionMedianUs=%.6f\n",unsigned(gpuTimes.size()),discarded,uiDraws,backWidth,backHeight,median(submitTimes),median(gpuTimes),median(wallTimes));
            for(auto* query:{event,start,end,frequency,disjoint})query->Release();
        }
        referenceRead->Release();reference->Release();readback->Release();back->Release();
    }
    IDirect3DSurface9* resetBack=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&resetBack));ok(d->SetRenderTarget(0,resetBack));resetBack->Release();
    color.reset();ok(d->SetTexture(0,nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetVertexDeclaration(nullptr));
    for(auto* p:ps)p->Release();for(auto* p:vs)p->Release();declaration->Release();tex->Release();staging->Release();target->Release();
    pp.BackBufferWidth=800;pp.BackBufferHeight=450;
    if(proxy)ok(d->Reset(&pp));else ok(presentation.reset(d,d,&pp));assert(pp.BackBufferFormat==D3DFMT_X8R8G8B8);
    IDirect3DSurface9* resized=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&resized));ok(resized->GetDesc(&physical));resized->Release();
    assert(physical.Width==800&&physical.Height==450&&physical.Format==(!proxy&&hdrExpected?D3DFMT_A16B16G16R16F:D3DFMT_X8R8G8B8));
    assert(physical.MultiSampleType==samples);
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(51,102,153),1,0));ok(d->Present(nullptr,nullptr,nullptr,nullptr));
    if(proxy&&samples!=D3DMULTISAMPLE_NONE)for(auto next:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES,D3DMULTISAMPLE_4_SAMPLES,samples}){
        pp.MultiSampleType=next;ok(d->Reset(&pp));
        IDirect3DSurface9 *back=nullptr,*depth=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
        ok(back->GetDesc(&physical));assert(physical.MultiSampleType==next&&physical.Format==D3DFMT_X8R8G8B8);back->Release();
        ok(d->GetDepthStencilSurface(&depth));D3DSURFACE_DESC desc{};ok(depth->GetDesc(&desc));assert(desc.MultiSampleType==next);depth->Release();
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_XRGB(51,102,153),1,0));ok(d->Present(nullptr,nullptr,nullptr,nullptr));++cases;
    }
    std::printf("HDR integration GPU proxy=%u hdr=%u msaa=%u cases=%u failures=%u maxError=%.9g originalShaderRestored=1 constants223Unchanged=1 uiCapturedShaderPreserved=1 resetResize=1 present=1\n",unsigned(proxy),unsigned(hdrExpected),unsigned(samples),cases,failures,maximum);
    d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
