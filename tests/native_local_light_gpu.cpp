// Real client VS/PS programs: single-source endpoints preserve native material,
// saturation, fog, alpha test, blend and MSAA. No WoW execution.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_renderer.h"
#include "forwarders.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <chrono>
static int failAllocation=-1;
void* operator new(std::size_t n){if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}if(failAllocation>0)--failAllocation;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static void must(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT %08lx\n",(unsigned long)hr);std::abort();}}
static std::vector<DWORD> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f&&f.tellg()%4==0);std::vector<DWORD> out(size_t(f.tellg())/4);f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size()*4);assert(f);return out;}
static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* rt,unsigned size){IDirect3DSurface9* copy=nullptr;must(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;must(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(size*size*4);for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size*4;++x)out[y*size*4+x]=NorthlightActorDeformation::half(reinterpret_cast<const uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x]);must(copy->UnlockRect());copy->Release();return out;}
static void save(const std::string& path,const std::vector<float>& image,unsigned size){std::ofstream f(path,std::ios::binary);unsigned h[]={0x31524746,size,size,116,16};f.write(reinterpret_cast<char*>(h),20);f.write(reinterpret_cast<const char*>(image.data()),image.size()*4);assert(f);}
#include "native_local_light_final.inc"
struct FaultDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned sets=0,failAt=0;bool failPrivatePS=false,failOriginalVS=false;IDirect3DVertexShader9* originalVS=nullptr;IDirect3DPixelShader9* originalPS=nullptr;
    bool fail(){return ++sets==failAt;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{if(fail()||(failOriginalVS&&p==originalVS))return E_FAIL;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fail())return E_FAIL;if(failPrivatePS&&p!=originalPS){failPrivatePS=false;return E_FAIL;}return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* p,UINT n)override{if(fail())return E_FAIL;return real->SetPixelShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* p)override{if(fail())return E_FAIL;return real->SetTexture(stage,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD value)override{if(fail())return E_FAIL;return real->SetSamplerState(stage,key,value);}
};
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==7);constexpr unsigned Size=256;const bool wmo=std::strcmp(argv[5],"wmo")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightNativeLocalTest";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto v=words(argv[2]),p=words(argv[3]);IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(v.data(),&vs));must(d->CreatePixelShader(p.data(),&ps));
    unsigned slots[]={0,1};std::vector<uint32_t> negative;NorthlightNativeLocalPatch::VertexInfo vi;assert(!NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots,2,negative,vi));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,28,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));
    struct Vertex {float x,y,z;DWORD index;float nx,ny,nz;DWORD color;float u,v;};const Vertex plane[]={{-8,8,8,0,0,0,-1,0xb3ffffff,0,0},{8,8,8,0,0,0,-1,0xb3ffffff,1,0},{-8,-8,8,0,0,0,-1,0xb3ffffff,0,1},{8,-8,8,0,0,0,-1,0xb3ffffff,1,1}};
    IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock;must(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=((x+y)%2?0xff000000:0x00000000)|0x00b39980;must(texture->UnlockRect(0));
    IDirect3DCubeTexture9* cube=nullptr;must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    auto fillCube=[&](unsigned mode){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=mode==0?1.f:mode==1?0.f:x<128?1.f:0.f;must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}};
    NorthlightNativeLocalDraw light;light.cube=cube;light.position[2]=4;light.range=12;for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;
    NorthlightNativeLocalLighting renderer(d);unsigned cases=0;float worst=0;double coldMs=0;std::vector<float> native,blocked,edge;std::vector<std::vector<float>> fixture;IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    for(unsigned ms:{0u,2u}){IDirect3DSurface9* target=nullptr;if(ms){if(FAILED(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&target,nullptr)))continue;}else{target=resolved;target->AddRef();}
    for(unsigned mode=0;mode<3;++mode)for(float fog:{1.f,.35f})for(unsigned context:{0u,1u}){
        must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FOGENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_ALPHABLENDENABLE,mode==2},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,mode==1},{D3DRS_ALPHAFUNC,D3DCMP_GREATER},{D3DRS_ALPHAREF,128},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}};for(auto state:states)must(d->SetRenderState(state.first,state.second));
        float constants[256][4]{};constants[2][0]=constants[3][1]=1;constants[4][2]=64.f/63.9f;constants[4][3]=-.1f*constants[4][2];constants[5][2]=1;constants[6][0]=constants[7][1]=1;
        for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;light.position[0]=light.position[1]=0;light.position[2]=4;
        if(context){const float angle=.3f,pitch=.2f,s=std::sin(angle),c=std::cos(angle),sp=std::sin(pitch),cp=std::cos(pitch);
            const float view[]={c,s,0,0,-s*cp,c*cp,sp,0,s*sp,-c*sp,cp,0,-8574.044922f,997.463379f,103.869629f,1};std::memcpy(light.inverseView,view,64);
            for(unsigned k=0;k<3;++k)light.position[k]=view[12+k]+4*view[8+k];constants[2][0]=1.13f;constants[3][1]=1.27f;constants[2][2]=.07f;constants[3][2]=-.05f;
        }
        for(unsigned k=0;k<3;++k){constants[10][k]=.2f;constants[11][k]=.1f;constants[28][k]=1;}constants[12][2]=1;constants[28][3]=.7f;
        constants[17][0]=12;constants[17][1]=.4f;constants[17][2]=.1f;constants[18][0]=.15f;constants[18][1]=.8f;constants[18][2]=.2f;
        constants[21][2]=4;constants[22][0]=-2;constants[22][1]=2;constants[22][2]=5;
        constants[25][0]=constants[25][1]=1;constants[26][0]=constants[26][1]=.2f;constants[27][0]=constants[27][1]=.01f;constants[30][1]=fog;constants[30][2]=1;constants[31][0]=constants[32][1]=constants[33][2]=1;
        must(d->SetVertexShaderConstantF(0,constants[0],256));float pixelConstants[224][4]{};pixelConstants[2][0]=.05f;pixelConstants[2][1]=.1f;pixelConstants[2][2]=.2f;must(d->SetPixelShaderConstantF(0,pixelConstants[0],224));must(d->SetTexture(0,texture));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,key,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));must(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        auto render=[&](bool patched,bool zero){float color[4];std::memcpy(color,constants[17],16);if(zero)color[0]=color[1]=color[2]=0;must(d->SetVertexShaderConstantF(17,color,1));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};const bool cold=patched&&!renderer.coldPairs;const auto start=std::chrono::steady_clock::now();must(patched?renderer.draw(light,vs,ps,submit):submit());if(cold)coldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();must(d->EndScene());if(ms)must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));return pixels(d,resolved,Size);};
        fillCube(0);native=render(false,false);const auto clear=render(true,false);assert(renderer.draws>0&&renderer.coldPairs==1);
        assert(native==clear); // Includes nonidentity projection clipping and all MSAA edge pixels.
        fillCube(1);const auto without=render(false,true);blocked=render(true,false);
        light.gain=0;const auto gain0=render(true,false);assert(gain0==native);light.gain=.5f;const auto fade=render(true,false);light.gain=1;
        float removed=0;for(unsigned i=0;i<native.size();i+=4)removed=std::max(removed,native[i]-without[i]);assert(removed>.03f); // Never accept an empty/cutout/transparent fixture.
        for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned channel=0;channel<4;++channel){unsigned i=(y*Size+x)*4+channel;assert(fade[i]>=std::min(native[i],blocked[i])-.001f&&fade[i]<=std::max(native[i],blocked[i])+.001f);const float error=std::max(std::fabs(native[i]-clear[i]),std::fabs(without[i]-blocked[i]));worst=std::max(worst,error);if(error>.001f){std::printf("ENDPOINT_FAIL ms=%u mode=%u fog=%g x=%u y=%u c=%u native=%g clear=%g without=%g blocked=%g\n",ms,mode,fog,x,y,channel,native[i],clear[i],without[i],blocked[i]);std::abort();}}
        fillCube(2);edge=render(true,false);for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<4;++k){unsigned i=(y*Size+x)*4+k;assert(edge[i]>=std::min(native[i],without[i])-.001f&&edge[i]<=std::max(native[i],without[i])+.001f);}
        if(!ms&&!mode&&fog==1&&!context){fillCube(1);const auto zero=render(true,true);assert(zero==without);fixture={native,clear,blocked,edge,without,zero,gain0,fade};save(std::string(argv[4])+"/opaque-native.fgr",native,Size);save(std::string(argv[4])+"/opaque-blocked.fgr",blocked,Size);save(std::string(argv[4])+"/opaque-edge.fgr",edge,Size);}
        ++cases;
    }
    target->Release();}
    assert(cases>=6&&renderer.coldPairs==1);save(std::string(argv[4])+"/native.fgr",native,Size);save(std::string(argv[4])+"/blocked.fgr",blocked,Size);save(std::string(argv[4])+"/edge.fgr",edge,Size);
    // Every changed setter can fail during setup or restore. A single transient
    // failure must still submit exactly once and restore the entire native state.
    FaultDevice fault(d);fault.originalVS=vs;fault.originalPS=ps;NorthlightNativeLocalLighting checked(&fault);
    const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
    const DWORD previous[]={D3DTADDRESS_WRAP,D3DTADDRESS_MIRROR,D3DTADDRESS_WRAP,D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,TRUE};
    must(d->SetTexture(1,texture));for(unsigned i=0;i<7;++i)must(d->SetSamplerState(1,keys[i],previous[i]));must(d->SetRenderTarget(0,resolved));
    float savedConstants[224][4];must(d->GetPixelShaderConstantF(0,savedConstants[0],224));
    auto checkState=[&]{IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;IDirect3DBaseTexture9* t=nullptr;float c[224][4];must(d->GetVertexShader(&v));must(d->GetPixelShader(&p));must(d->GetTexture(1,&t));must(d->GetPixelShaderConstantF(0,c[0],224));assert(v==vs&&p==ps&&t==texture&&!std::memcmp(c,savedConstants,sizeof c));v->Release();p->Release();t->Release();for(unsigned i=0;i<7;++i){DWORD value=0;must(d->GetSamplerState(1,keys[i],&value));assert(value==previous[i]);}};
    auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};
    must(d->BeginScene());must(checked.draw(light,vs,ps,submit));must(d->EndScene());checkState();const unsigned setters=fault.sets;assert(setters>=18);
    for(unsigned fail=1;fail<=setters;++fail){fault.sets=0;fault.failAt=fail;unsigned submits=0;must(d->BeginScene());must(checked.draw(light,vs,ps,[&]{++submits;return submit();}));must(d->EndScene());assert(submits==1);fault.failAt=0;checkState();}
    fault.sets=0;fault.failAt=0;fault.failPrivatePS=true;fault.failOriginalVS=true;unsigned forbidden=0;must(d->BeginScene());assert(FAILED(checked.draw(light,vs,ps,[&]{++forbidden;return submit();})));must(d->EndScene());assert(!forbidden);fault.failOriginalVS=false;must(d->SetVertexShader(vs));checkState();
    NorthlightNativeLocalLighting allocation(d);failAllocation=0;unsigned allocationFallback=0;must(allocation.draw(light,vs,ps,[&]{++allocationFallback;return S_OK;}));assert(failAllocation==-1&&allocationFallback==1&&allocation.fallbacks==1&&!allocation.draws);checkState();
    // Unsupported pairs consume the same one-attempt frame budget as supported
    // pairs, and their negative cache does not keep consuming later frames.
    const DWORD unsupported[]={0xffff0200u,0x02000001u,0x800f0800u,0xa0e40000u,0xffffu};IDirect3DPixelShader9 *bad=nullptr,*second=nullptr;must(d->CreatePixelShader(unsupported,&bad));must(d->CreatePixelShader(p.data(),&second));
    NorthlightNativeLocalLighting budget(d);light.rendererFrame=10;must(d->SetPixelShader(bad));must(budget.draw(light,vs,bad,[]{return S_OK;}));assert(budget.coldPairs==1);must(d->SetPixelShader(ps));must(budget.draw(light,vs,ps,[]{return S_OK;}));assert(budget.coldPairs==1&&budget.deferredPairs==1);
    light.rendererFrame=11;must(d->SetPixelShader(bad));must(budget.draw(light,vs,bad,[]{return S_OK;}));must(d->SetPixelShader(ps));must(budget.draw(light,vs,ps,[]{return S_OK;}));assert(budget.coldPairs==2);must(d->SetPixelShader(second));must(budget.draw(light,vs,second,[]{return S_OK;}));assert(budget.coldPairs==2);
    light.rendererFrame=12;must(d->SetPixelShader(second));must(budget.draw(light,vs,second,[]{return S_OK;}));assert(budget.coldPairs==3);must(d->SetPixelShader(ps));budget.reset();must(budget.draw(light,vs,ps,[]{return S_OK;}));assert(budget.coldPairs==3);light.rendererFrame=13;must(budget.draw(light,vs,ps,[]{return S_OK;}));assert(budget.coldPairs==4);budget.reset();bad->Release();second->Release();checked.reset();
    // Paired warm native draw versus the exact handoff, at the same 256-square
    // receiver and resources. GPU timestamp intervals exclude query readback.
    for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE})must(d->SetRenderState(state,FALSE));fillCube(2);
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    auto drain=[&]{must(event->Issue(D3DISSUE_END));HRESULT hr;const DWORD at=GetTickCount();while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-at)<10000);SwitchToThread();}must(hr);};
    auto batch=[&](bool patched){must(d->BeginScene());for(unsigned i=0;i<64;++i)must(patched?renderer.draw(light,vs,ps,submit):submit());must(d->EndScene());};
    std::vector<double> gpu[2],cpu[2];std::ofstream timing(std::string(argv[4])+"/timings.tsv");
    for(unsigned block=0;block<20;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;batch(mode);drain();must(disjoint->Issue(D3DISSUE_BEGIN));must(start->Issue(D3DISSUE_END));const auto at=std::chrono::steady_clock::now();batch(mode);const double submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-at).count()/64;must(end->Issue(D3DISSUE_END));must(frequency->Issue(D3DISSUE_END));must(disjoint->Issue(D3DISSUE_END));drain();UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);const double ms=double(b-a)/double(f)*1000/64;if(block>=4){gpu[mode].push_back(ms);cpu[mode].push_back(submitMs);timing<<mode<<'\t'<<ms<<'\t'<<submitMs<<'\n';}}
    auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());assert(values.size()==8);return (values[3]+values[4])*.5;};
    std::printf("NATIVE_LOCAL_COST family=%s receiver=256x256 samplesPerBranch=8 drawsPerSample=64 nativeGpuMs=%.6f shadowGpuMs=%.6f nativeCpuMs=%.6f shadowCpuMs=%.6f firstColdCpuMs=%.6f warmShaderCreation=0 gameFPS=unmeasured\n",wmo?"wmo":"m2",median(gpu[0]),median(gpu[1]),median(cpu[0]),median(cpu[1]),coldMs);
    for(auto* query:{event,start,end,frequency,disjoint})query->Release();finalContract(d,argv[6],fixture,Size);
    std::printf("NATIVE_LOCAL_GPU_PASS cases=%u endpointMaxError=%g nativeDraws=%llu coldPairs=%u addedPixelSlots=%u warmShaderCreation=0 injectedSetFailures=%u persistentRestoreBlocksFallback=1 coldAttemptsPerFrame=1 sameFrameResetBudget=preserved negativeCache=1 allocationFailure=fail-open selectionGain0=exact-baseline selectionGainHalf=bounded selectionGain1=exact-endpoints nonidentityProjectionViewCamera=1 sourceRemoval=exact-native otherLightAmbientFogAlpha=preserved msaa=same-native-draw\n",cases,worst,(unsigned long long)renderer.draws,renderer.coldPairs,renderer.shaderSlots,setters);
    renderer.reset();for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);cube->Release();texture->Release();declaration->Release();vs->Release();ps->Release();resolved->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(hwnd);
}
