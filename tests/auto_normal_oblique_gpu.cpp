// Grazing-angle material demonstration using the production cache, capture and final shader.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
#include "material_pixel_audit.h"
#include "patch_terrain_shadow_coverage.h"
#include "forwarders.h"
#include "mirror_resource_forwarders.h"
#include "northlight_mem.h"
// Match the renderer's strong memcmp implementation in submission benchmarks.
extern "C" __attribute__((no_builtin("memcmp"),no_builtin("bcmp"))) int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}
struct MaterialDepthMeter:ForwardIDirect3DDevice9 {
    unsigned reads=0,allocations=0,gpuDraws=0,clears=0,pixelQueries=0,vertexCreates=0;bool failDeferred=false,failResident=false;explicit MaterialDepthMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** shader)override{++vertexCreates;return real->CreateVertexShader(code,shader);}
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT bytes,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared)override{if(failResident){*out=nullptr;return E_OUTOFMEMORY;}return real->CreateVertexBuffer(bytes,usage,fvf,pool,out,shared);}
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ps)override{++pixelQueries;return real->GetPixelShader(ps);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* vs)override{if(failDeferred){failDeferred=false;return E_FAIL;}return real->SetVertexShader(vs);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{++clears;return real->Clear(n,rect,flags,color,z,stencil);}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** s)override{++reads;return real->GetDepthStencilSurface(s);}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{const auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr)){if(std::getenv("NORTHLIGHT_TEST_CPU_DISPLACEMENT"))caps->VertexTextureFilterCaps=0;if(std::getenv("NORTHLIGHT_TEST_NO_SHARED_POM"))caps->NumSimultaneousRTs=2;}return hr;}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** t,HANDLE* shared)override{++allocations;return real->CreateTexture(w,h,levels,usage,format,pool,t,shared);}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** shader)override{
        if(std::getenv("NORTHLIGHT_TEST_SHARED_SHADER_FAILURE"))for(const auto* shared:{kBumpSharedOpaqueShader,kBumpSharedLayerShader,kBumpSharedWallShader,kBumpSharedGrassShader,kBumpSharedWallOpaqueShader,kBumpSharedCompositeShader})if(code==shared){*shader=nullptr;return E_FAIL;}
        return real->CreatePixelShader(code,shader);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* v,UINT stride)override{++gpuDraws;return real->DrawPrimitiveUP(type,n,v,stride);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT low,UINT vertices,UINT n,const void* indices,D3DFORMAT format,const void* v,UINT stride)override{++gpuDraws;return real->DrawIndexedPrimitiveUP(type,low,vertices,n,indices,format,v,stride);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT low,UINT vertices,UINT first,UINT n)override{++gpuDraws;return real->DrawIndexedPrimitive(type,base,low,vertices,first,n);}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type,UINT first,UINT n)override{++gpuDraws;return real->DrawPrimitive(type,first,n);}
};
struct MaterialCaptureStateTestAccess {
    static unsigned queued(const NorthlightAutoBump& b){return b.queued;}
    static unsigned flushed(const NorthlightAutoBump& b){return b.flushed;}
    static unsigned captured(const NorthlightAutoBump& b,unsigned domain){return b.draws[domain-1];}
    static size_t sourceReadBytes(const NorthlightAutoBump& b){return b.deferredSnapshots.bytesRead();}
    static bool released(const NorthlightAutoBump& b){return b.deferred.empty()&&!b.deferredBytes&&!b.pendingTextures()&&!b.deferredTextureBytes&&!b.deferredSnapshots.snapshotCacheBytes()
#ifndef NORTHLIGHT_TEST_CAPTURE_BASELINE
        &&!b.deferredGPU.bytes()
#endif
        ;}
    static void residency(const NorthlightAutoBump& b,bool expected){
#ifndef NORTHLIGHT_TEST_CAPTURE_BASELINE
        std::printf("MATERIAL_RESIDENT_PASS resident=%u up=%u uploaded=%zu upBytes=%llu cacheBytes=%zu\n",b.residentDraws,b.uploadDraws,b.deferredGPU.uploaded(),(unsigned long long)b.uploadDrawBytes,b.deferredGPU.bytes());
        if(expected)assert(b.residentDraws&& !b.uploadDraws&&!b.deferredGPU.uploaded()&&b.deferredGPU.bytes());
        else assert(!b.residentDraws&&b.uploadDraws&&!b.deferredGPU.bytes());
#endif
    }
    static void stateReuse(const NorthlightAutoBump& b){
#ifndef NORTHLIGHT_TEST_CAPTURE_BASELINE
        std::printf("MATERIAL_STATE_REUSE_PASS issued=%u skipped=%u\n",b.replaySetters,b.replaySkipped);assert(b.replaySetters&&b.replaySkipped);
#endif
    }
    static void mutation(const NorthlightAutoBump& b,const void* vertices,size_t bytes){if(b.deferred.empty())return;const auto& p=*b.deferred.back();assert(p.mesh->streams[0].bytes.size()==bytes&&!std::memcmp(p.mesh->streams[0].bytes.data(),vertices,bytes));float bank[1024];ok(b.d->GetVertexShaderConstantF(0,bank,256));assert(!std::memcmp(bank,p.vertexConstants,sizeof bank));}
    static void replayContent(const NorthlightAutoBump& b,unsigned long long& resident,unsigned long long& up,unsigned long long& reads,unsigned long long& hits){resident+=b.residentDraws;up+=b.uploadDraws;reads+=b.deferredSnapshots.bytesRead();
#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
        hits+=b.deferredSnapshots.replayContentHits();
#else
        (void)hits;
#endif
    }
    static unsigned rejected(const NorthlightAutoBump& b){return b.rangeRejected;}
    static uint64_t clears(const NorthlightAutoBump& b){return b.sharedPOM.clearCount();}
    static uint64_t produced(const NorthlightAutoBump& b){return b.sharedPOM.producedCount();}
    static uint64_t consumed(const NorthlightAutoBump& b){return b.sharedPOM.nativeCount();}
    static bool usingShared(const NorthlightAutoBump& b){return b.parallax.shared();}
    static void wrap(NorthlightAutoBump& b){b.sharedPOM.nextTag=0xffffff;}
    static bool clearHalf(NorthlightAutoBump& b,IDirect3DDevice9* d){
        if(!b.sharedPOM.available())return false;D3DSURFACE_DESC desc{};ok(b.sharedPOM.target(0)->GetDesc(&desc));const D3DRECT rect={0,0,LONG(desc.Width/2),LONG(desc.Height)};return b.sharedPOM.clearTags(&rect);
    }
};
struct DisplacementStateTestAccess {
#ifdef NORTHLIGHT_TEST_LAZY_VARIANT_CURRENT
    static bool ensure(NorthlightAutoDisplacement& g,IDirect3DVertexShader9* vs){auto i=g.shaders.find(vs);return i!=g.shaders.end()&&g.ensureVariant(i->second);}
#endif
    static unsigned rangeHits(const NorthlightAutoDisplacement& g){return g.rangeSnapshots.trackedHits();}
    static size_t rangeBytes(const NorthlightAutoDisplacement& g){return g.rangeSnapshots.bytesRead();}
    static void fullRangeLayout(NorthlightAutoDisplacement& g){g.rangeSnapshots.setLayoutProvider(&g.declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});}
    static void fullColor(NorthlightAutoDisplacement& g,IDirect3DDevice9* d,IDirect3DVertexDeclaration9* decl,const NorthlightAutoDisplacement::Args& args,DWORD expected){
        NorthlightDrawSnapshot::Mesh mesh;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared;assert(g.snapshots.read(d,decl,args.draw,mesh,nullptr,false,&shared));
        const auto& color=(shared?*shared:mesh).streams[1].bytes;assert(color.size()>=4&&!std::memcmp(color.data(),&expected,4));
    }
};
#ifdef NORTHLIGHT_MATERIAL_MIRROR
#include "device_mirror.h"
struct MaterialAllocationMeter:ForwardIDirect3DDevice9 {
    bool fail;explicit MaterialAllocationMeter(IDirect3DDevice9* d,bool inject):ForwardIDirect3DDevice9(d),fail(inject){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** result,HANDLE* shared)override{
        if(fail&&format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)){fail=false;*result=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return real->CreateTexture(w,h,l,usage,format,pool,result,shared);
    }
};
struct MaterialConstantMeter:ForwardIDirect3DDevice9 {
    unsigned calls=0,registers=0,builds=0;explicit MaterialConstantMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* values,UINT count)override{++calls;registers+=count;return real->GetVertexShaderConstantF(first,values,count);}
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT bytes,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** result,HANDLE* shared)override{++builds;return real->CreateVertexBuffer(bytes,usage,fvf,pool,result,shared);}
};
#endif
#ifndef NORTHLIGHT_OBLIQUE_WIDTH
#define NORTHLIGHT_OBLIQUE_WIDTH 1280
#define NORTHLIGHT_OBLIQUE_HEIGHT 800
#endif
static constexpr UINT W=NORTHLIGHT_OBLIQUE_WIDTH,H=NORTHLIGHT_OBLIQUE_HEIGHT;
static IDirect3DTexture9* solid(IDirect3DDevice9* d,float x,float y,float z,float a){Image i{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float data[]={x,y,z,a};std::memcpy(i.pixels.data(),data,16);return texture(d,i);}
static void save(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::string& path){
    if(std::getenv("NORTHLIGHT_TEST_SHARED_TIMING"))return;
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));
    const UINT bytes=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:4;std::ofstream f(path,std::ios::binary);UINT header[]={0x31524746,desc.Width,desc.Height,UINT(desc.Format),bytes};f.write((char*)header,20);D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(UINT y=0;y<desc.Height;++y)f.write((char*)lock.pBits+y*lock.Pitch,desc.Width*bytes);stage->UnlockRect();drop(stage);
}
#ifndef NORTHLIGHT_OBLIQUE_ENTRY
#define NORTHLIGHT_OBLIQUE_ENTRY main
#endif
int NORTHLIGHT_OBLIQUE_ENTRY(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc>=3);std::string folder=argv[2];const bool model=argc>5&&std::strstr(argv[5],"model"),grass=argc>5&&std::strcmp(argv[5],"grass")==0;
    const auto samples=static_cast<D3DMULTISAMPLE_TYPE>(std::getenv("NORTHLIGHT_TEST_MSAA")?std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA")):0);
    const bool pressure=std::getenv("NORTHLIGHT_TEST_PRESSURE")!=nullptr;
    const bool pomOnly=std::getenv("NORTHLIGHT_TEST_POM_ONLY")!=nullptr;
    const bool resolveMSDepth=samples&&std::strstr(argv[1],"SysWOW64")==nullptr;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NormalOblique";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.MultiSampleType=samples;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    MaterialAllocationMeter meter(d,pressure);DeviceMirror mirror;mirror.gate.exclusive=true;ExtensionDevice extension(&meter,&mirror);d=&extension;
    {DeviceMirror escape;struct Probe {DeviceMirror* mirror;unsigned calls=0;} probe{&escape};escape.disableContext=&probe;
     escape.beforeDisable=[](void* context){auto& p=*static_cast<Probe*>(context);assert(p.mirror->enabled&&p.mirror->heldByThisThread());++p.calls;};
     escape.disable("material escape test");escape.disable("second escape");assert(probe.calls==1&&!escape.enabled&&!escape.beforeDisable);}
#endif
    IDirect3DSurface9 *baseSurface=nullptr,*depthSurface=nullptr,*zbuffer=nullptr,*output=nullptr;
    IDirect3DTexture9 *base=nullptr,*depth=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&base,nullptr));base->GetSurfaceLevel(0,&baseSurface);
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth,nullptr));depth->GetSurfaceLevel(0,&depthSurface);
    ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&zbuffer,nullptr));
    IDirect3DSurface9 *drawSurface=baseSurface,*drawDepth=depthSurface;
    if(samples){
        drop(zbuffer);ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,samples,0,FALSE,&zbuffer,nullptr));
        ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,samples,0,FALSE,&drawSurface,nullptr));
        drawDepth=nullptr;
        if(resolveMSDepth){drop(depth);ok(d->CreateTexture(W,H,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&depth,nullptr));}
    }
    const auto alphaPattern=[](Image& im){if(std::getenv("NORTHLIGHT_TEST_MATERIAL_ALPHA_TEST"))for(unsigned y=0;y<im.height;++y)for(unsigned x=0;x<im.width;++x)im.pixels[(size_t(y)*im.width+x)*4+3]=((x/std::max(1u,im.width/8)+y/std::max(1u,im.height/8))&1)?255:0;};
    auto first=image(folder+"source.fgr");alphaPattern(first);IDirect3DTexture9* diffuse=nullptr;ok(d->CreateTexture(first.width,first.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&diffuse,nullptr));
    for(unsigned level=0;level<diffuse->GetLevelCount();++level){auto im=image(folder+"source.fgr."+std::to_string(level));alphaPattern(im);D3DLOCKED_RECT lock{};ok(diffuse->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);diffuse->UnlockRect(level);}
    auto vsBytes=read(folder+"terrain-vs.bin"),psBytes=read(folder+"terrain-ps.bin");std::vector<std::uint32_t> patched;assert(!NorthlightWaterShaderPatch::patch((std::uint32_t*)vsBytes.data(),vsBytes.size()/4,patched));
    IDirect3DVertexShader9 *vs=nullptr,*forwardVS=nullptr;IDirect3DPixelShader9 *ps=nullptr,*beauty=nullptr,*material=nullptr;
    ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreateVertexShader((DWORD*)patched.data(),&forwardVS));ok(d->CreatePixelShader((DWORD*)psBytes.data(),&ps));
    auto beautyBytes=read(folder+"PreviewBase.bin");ok(d->CreatePixelShader((DWORD*)beautyBytes.data(),&beauty));ok(d->CreatePixelShader(kMaterialCompositeShader,&material));
    const float cameraHeight=argc>4?std::atof(argv[4]):.7f;const float angle=argc>3?std::atof(argv[3])*.01745329252f:.20943951f,s=std::sin(angle),c=std::cos(angle),xs=1.7320508f*H/W,ys=1.7320508f;
    float bank[79][4]={};bank[0][0]=1;bank[1][1]=s;bank[1][2]=-c;bank[2][1]=c;bank[2][2]=s;bank[3][1]=-cameraHeight*c+4*s;bank[3][2]=-cameraHeight*s-4*c;bank[3][3]=1;
    bank[4][0]=xs;bank[5][1]=ys;bank[6][2]=100.f/(.1f-100.f);bank[6][3]=-1;bank[7][2]=.1f*100/(.1f-100);
    if(const char* distance=std::getenv("NORTHLIGHT_TEST_MATERIAL_DEPTH"))bank[3][2]-=std::atof(distance)-4;
    bank[18][0]=bank[18][1]=.25f;bank[13][0]=bank[13][1]=.5f;
    float light[]={-.4f,.3f,.8660254f};
    if(std::getenv("NORTHLIGHT_TEST_MSAA")){light[0]=-.94f;light[1]=.2f;light[2]=.28f;}
    if(std::getenv("NORTHLIGHT_TEST_MATERIAL_DEMO")){light[0]=-.9f;light[1]=.42f;light[2]=.12f;}
    const float energy=light[1]*c+light[2]*s;
    bank[78][0]=.2f+energy;bank[78][1]=.22f+.95f*energy;bank[78][2]=.26f+.85f*energy;
    if(argc>5&&!grass){const float projectionRows[4][4]={{xs,0,0,0},{0,ys,0,0},{0,0,bank[6][2],bank[7][2]},{0,0,-1,0}};std::memcpy(bank[2],projectionRows,sizeof projectionRows);const float viewRows[3][4]={{1,0,0,0},{0,s,c,-cameraHeight*c+4*s},{0,-c,s,-cameraHeight*s-4*c}};std::memcpy(bank[31],viewRows,sizeof viewRows);}
    if(argc>5&&!grass)if(const char* distance=std::getenv("NORTHLIGHT_TEST_MATERIAL_DEPTH"))bank[33][3]-=std::atof(distance)-4;
    if(grass){bank[9][0]=0;bank[9][1]=1;}
    const float macro[]={bank[78][0],bank[78][1],bank[78][2]};auto* baseline=solid(d,macro[0],macro[1],macro[2],1);auto* normal=solid(d,0,c,s,1);IDirect3DTexture9* white=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT wl{};ok(white->LockRect(0,&wl,nullptr,0));*static_cast<DWORD*>(wl.pBits)=0xffffffff;ok(white->UnlockRect(0));
    struct V{float x,y,z,nx,ny,nz,u,v;DWORD color,blend,weights=255,bones=0;};V floor[]={{-4,-3.4f,0,0,0,1,-1,-.85f,0xffffffff,0xffffffff},{4,-3.4f,0,0,0,1,1,-.85f,0xffffffff,0xffffffff},{-4,4.6f,0,0,0,1,-1,1.15f,0xffffffff,0xffffffff},{4,4.6f,0,0,0,1,1,1.15f,0xffffffff,0xffffffff}};
    if(std::getenv("NORTHLIGHT_TEST_VERTEX_ALPHA_ZERO"))for(auto& vertex:floor)vertex.color&=0x00ffffff;
    if(model){for(unsigned i=0;i<4;++i){floor[i].weights=std::strcmp(argv[5],"model4")==0?0x3f404040:255;floor[i].bones=0x100;}bank[6][0]=1;bank[6][1]=bank[6][2]=bank[6][3]=0;bank[7][1]=1;bank[7][0]=bank[7][2]=bank[7][3]=0;bank[10][0]=bank[10][1]=bank[10][2]=.7f;bank[11][0]=bank[11][1]=bank[11][2]=.3f;bank[12][0]=.4f;bank[12][1]=-.3f;bank[12][2]=-.8660254f;for(unsigned j=0;j<4;++j)bank[28][j]=1;bank[30][0]=0;bank[30][1]=bank[30][2]=1;for(unsigned r=0;r<3;++r)std::memcpy(bank[34+r],bank[31+r],16);bank[34][3]+=.15f;if(std::strcmp(argv[5],"model4")==0){for(auto& v:floor){v.weights=0x3f404040;v.bones=0x03020100;}for(unsigned bone=2;bone<4;++bone){for(unsigned r=0;r<3;++r)std::memcpy(bank[31+bone*3+r],bank[31+r],16);bank[31+bone*3][3]+=.05f*bone;}}}
    if(model&&std::getenv("NORTHLIGHT_TEST_MATERIAL_ALPHA_TEST"))bank[10][3]=bank[11][3]=1;
    const bool unsafeSkin=argc>5&&std::strcmp(argv[5],"modelunsafe")==0;
    if(unsafeSkin){for(unsigned r=0;r<3;++r){bank[34+r][0]=.8660254f*bank[31+r][0]-.5f*bank[31+r][2];bank[34+r][2]=.5f*bank[31+r][0]+.8660254f*bank[31+r][2];}for(unsigned i=0;i<4;++i){floor[i].bones=0x03020100;floor[i].weights=(i&1)?0x0000807f:0x3f404040;}for(unsigned bone=2;bone<4;++bone){for(unsigned r=0;r<3;++r)std::memcpy(bank[31+bone*3+r],bank[31+r],16);bank[31+bone*3][3]+=.1f*bone;}}
    const D3DVERTEXELEMENT9 decls[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},{0,32,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,36,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1},{0,40,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,44,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(decls,&decl));
    IDirect3DVertexBuffer9* sourceVB=nullptr;IDirect3DIndexBuffer9* sourceIB=nullptr;void* upload=nullptr;
    const bool deferredMutation=std::getenv("NORTHLIGHT_TEST_DEFERRED_MUTATION")!=nullptr;
    const bool identicalRewrite=std::getenv("NORTHLIGHT_TEST_IDENTICAL_REWRITE")!=nullptr;
    const bool deferredPose=std::getenv("NORTHLIGHT_TEST_DEFERRED_POSE")!=nullptr;
    ok(d->CreateVertexBuffer(sizeof floor,deferredMutation?D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY:0,0,deferredMutation?D3DPOOL_DEFAULT:D3DPOOL_MANAGED,&sourceVB,nullptr));ok(sourceVB->Lock(0,sizeof floor,&upload,0));std::memcpy(upload,floor,sizeof floor);ok(sourceVB->Unlock());
    const WORD sourceIndices[]={0,1,2,2,1,3};ok(d->CreateIndexBuffer(sizeof sourceIndices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&sourceIB,nullptr));ok(sourceIB->Lock(0,sizeof sourceIndices,&upload,0));std::memcpy(upload,sourceIndices,sizeof sourceIndices);ok(sourceIB->Unlock());
    const unsigned meshGrid=std::getenv("NORTHLIGHT_TEST_MESH_GRID")?std::atoi(std::getenv("NORTHLIGHT_TEST_MESH_GRID")):0;
    unsigned sourceVertices=4,sourcePrimitives=2;
    if(meshGrid){assert(meshGrid<=128&&!deferredMutation);std::vector<V> vertices;
        for(unsigned y=0;y<=meshGrid;++y)for(unsigned x=0;x<=meshGrid;++x){V v=floor[0];const float u=float(x)/meshGrid,w=float(y)/meshGrid;
            v.x+=(floor[1].x-floor[0].x)*u;v.y+=(floor[2].y-floor[0].y)*w;v.u+=(floor[1].u-floor[0].u)*u;v.v+=(floor[2].v-floor[0].v)*w;vertices.push_back(v);}
        std::vector<WORD> indices;for(unsigned y=0;y<meshGrid;++y)for(unsigned x=0;x<meshGrid;++x){const WORD a=WORD(y*(meshGrid+1)+x),b=a+1,c=WORD(a+meshGrid+1),e=c+1;for(WORD i:{a,b,c,c,b,e})indices.push_back(i);}
        drop(sourceVB);drop(sourceIB);const UINT vbBytes=UINT(vertices.size()*sizeof(V)),ibBytes=UINT(indices.size()*2);
        ok(d->CreateVertexBuffer(vbBytes,0,0,D3DPOOL_MANAGED,&sourceVB,nullptr));ok(sourceVB->Lock(0,vbBytes,&upload,0));std::memcpy(upload,vertices.data(),vbBytes);ok(sourceVB->Unlock());
        ok(d->CreateIndexBuffer(ibBytes,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&sourceIB,nullptr));ok(sourceIB->Lock(0,ibBytes,&upload,0));std::memcpy(upload,indices.data(),ibBytes);ok(sourceIB->Unlock());
        sourceVertices=UINT(vertices.size());sourcePrimitives=UINT(indices.size()/3);
    }
    std::vector<unsigned char> rewriteVertices,rewriteIndices;
    if(identicalRewrite){assert(!deferredMutation);D3DVERTEXBUFFER_DESC v{};D3DINDEXBUFFER_DESC i{};ok(sourceVB->GetDesc(&v));ok(sourceIB->GetDesc(&i));rewriteVertices.resize(v.Size);rewriteIndices.resize(i.Size);
        ok(sourceVB->Lock(0,v.Size,&upload,D3DLOCK_READONLY));std::memcpy(rewriteVertices.data(),upload,v.Size);ok(sourceVB->Unlock());
        ok(sourceIB->Lock(0,i.Size,&upload,D3DLOCK_READONLY));std::memcpy(rewriteIndices.data(),upload,i.Size);ok(sourceIB->Unlock());}
    IDirect3DVertexShader9* coverageVS=nullptr;if(argc<=5&&reinterpret_cast<DWORD*>(vsBytes.data())[0]==0xfffe0300u){std::vector<uint32_t> words;assert(NorthlightTerrainCoverage::vertex(reinterpret_cast<uint32_t*>(vsBytes.data()),vsBytes.size()/4,words));ok(d->CreateVertexShader(reinterpret_cast<DWORD*>(words.data()),&coverageVS));}
    D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetRenderTarget(0,drawSurface));ok(d->SetRenderTarget(1,drawDepth));ok(d->SetDepthStencilSurface(zbuffer));d->SetViewport(&vp);
    ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})d->SetRenderState(key,TRUE);
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(key,FALSE);
    d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
    d->SetTexture(0,diffuse);d->SetTexture(1,argc>5&&std::strcmp(argv[5],"composite")==0?diffuse:white);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    d->SetVertexDeclaration(decl);d->SetVertexShader(forwardVS);d->SetPixelShader(beauty);d->SetVertexShaderConstantF(0,bank[0],43);d->SetPixelShaderConstantF(78,bank[78],1);
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V)));ok(d->EndScene());d->SetRenderTarget(1,nullptr);
    // Packaged DXVK uses production RESZ on real MSAA depth. The native material
    // oracle supplies rasterized depth (RESZ support depends on the physical GPU).
    auto referenceDepth=[&](auto draw){
        if(resolveMSDepth){SavedState saved(d);assert(saved.ok);ok(d->SetTexture(0,depth));ok(d->SetRenderState(D3DRS_POINTSIZE,0x3f800000));ok(d->SetRenderState(D3DRS_POINTSIZE,0x7fa05000));return;}
        float constants[256*4];ok(d->GetVertexShaderConstantF(0,constants,256));
        {SavedState saved(d);assert(saved.ok);ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));ok(d->SetRenderTarget(1,depthSurface));ok(d->SetViewport(&vp));
        ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        ok(d->SetVertexShader(forwardVS));ok(d->SetPixelShader(beauty));ok(d->BeginScene());ok(draw());ok(d->EndScene());}
        ok(d->SetVertexShaderConstantF(0,constants,256));
    };
    if(samples){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));ok(d->StretchRect(drawSurface,nullptr,baseSurface,nullptr,D3DTEXF_NONE));ok(d->SetRenderTarget(0,drawSurface));ok(d->SetDepthStencilSurface(zbuffer));referenceDepth([&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));});}
    MaterialDepthMeter captureDevice(d);NorthlightAutoBump bump(&captureDevice);bump.setDeferredCapture(std::getenv("NORTHLIGHT_TEST_DEFERRED_CAPTURE")!=nullptr);bump.setTextureWriteGuard(std::getenv("NORTHLIGHT_TEST_TEXTURE_GUARD")!=nullptr);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpStrength=100;bump.configure(settings);bump.setProjection(xs,ys,-1);
    MirrorGate textureGate;
    NorthlightMirrorResources::ForwardIDirect3DTexture9 gameTexture(diffuse,textureGate,[](void*,const char*){},nullptr);
    NorthlightDrawSnapshot::Ref<IDirect3DSurface9> sourceSurface;ok(diffuse->GetSurfaceLevel(0,sourceSurface.out()));
    NorthlightMirrorResources::ForwardIDirect3DSurface9 gameSurface(sourceSurface.p,textureGate,[](void*,const char*){},nullptr);
    if(std::getenv("NORTHLIGHT_TEST_TEXTURE_GUARD")){
        NorthlightMirrorWrite::sink={[](void* p,void* texture){static_cast<NorthlightAutoBump*>(p)->beforeTextureWrite(texture);},nullptr,&bump};
        bump.setTextureWriteGuard(true,[](void* p){NorthlightMirrorWrite::pending.store(static_cast<NorthlightAutoBump*>(p)->pendingTextures()?1:0);},&bump);
    }
    static void* mutationSource=nullptr;static void* rewriteIndex=nullptr;static uint64_t mutationRevision=1;
    if(deferredMutation)mutationSource=sourceVB;
    if(identicalRewrite){mutationSource=sourceVB;rewriteIndex=sourceIB;}
    NorthlightMaterialProfile materialProfile(&captureDevice);NorthlightMaterialPixelAudit pixelAudit(&captureDevice);const bool materialProfiling=std::getenv("NORTHLIGHT_TEST_MATERIAL_PROFILE")!=nullptr;
    if(materialProfiling){bump.setProfiler(&materialProfile);materialProfile.beginFrame(127,true);}
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    bump.setConstantEpochSource(&extension,[](void* context,NorthlightConstantEpoch::Stamp& stamp){return static_cast<ExtensionDevice*>(context)->constantStamp(stamp);});
#endif
    bump.setMemoryPressure(pressure);assert(bump.active());
    bump.setGeometryTracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void* p,bool){return p==mutationSource||p==rewriteIndex?mutationRevision:1;},nullptr);
    const bool lazyVertex=std::getenv("NORTHLIGHT_TEST_LAZY_VERTEX")!=nullptr;
    if(lazyVertex){auto disabled=settings;disabled.autoBump=0;bump.configure(disabled);assert(!bump.active());}
    const auto registrationCreates=captureDevice.vertexCreates;
    bump.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)vsBytes.data(),vsBytes.size()/4),(DWORD*)vsBytes.data(),vsBytes.size()/4,model?3:0);bump.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)psBytes.data(),psBytes.size()/4));
    if(std::getenv("NORTHLIGHT_TEST_VARIANT_AUDIT"))std::printf("MATERIAL_VARIANT_AUDIT stage=registration creates=%u\n",captureDevice.vertexCreates-registrationCreates);
    if(lazyVertex){bump.configure(settings);assert(bump.active());std::puts("MATERIAL_LAZY_VERTEX_REGISTERED_OFF_ENABLED_BEFORE_FIRST_DRAW");}
    if(model&&std::getenv("NORTHLIGHT_TEST_MATERIAL_PROFILE")){
        SavedState saved(d);assert(saved.ok);ok(d->SetVertexDeclaration(decl));ok(d->SetStreamSource(0,sourceVB,0,sizeof(V)));ok(d->SetIndices(sourceIB));
        NorthlightAutoDisplacement bounds(d);bounds.tracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);bounds.registerMaterial(vs,reinterpret_cast<const DWORD*>(vsBytes.data()),vsBytes.size()/4,4);
#ifdef NORTHLIGHT_MATERIAL_MIRROR
        bounds.setConstantEpochSource(&extension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});
#endif
        NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};float nearest=0;
        for(float distance:{60.f,0.f,60.f}){
            float moved[43][4];std::memcpy(moved,bank,sizeof moved);for(unsigned bone=0;bone<4;++bone)moved[33+3*bone][3]-=distance;
            ok(d->SetVertexShaderConstantF(0,moved[0],43));bool proven=false;for(unsigned attempt=0;attempt<20&&!proven;++attempt){bounds.endFrame();proven=bounds.nearestMaterialDepth(vs,args,nearest);}assert(proven&&(distance?nearest>48:nearest<8));
        }
        NorthlightAutoBump farCapture(&captureDevice);auto farSettings=settings;farSettings.autoBumpModels=1;farCapture.configure(farSettings);
        farCapture.setGeometryTracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);
        farCapture.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(vsBytes.data()),vsBytes.size()/4),reinterpret_cast<const DWORD*>(vsBytes.data()),vsBytes.size()/4,3);
        farCapture.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(psBytes.data()),psBytes.size()/4));
        unsigned replays=0;bool culled=false;
        ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
        for(unsigned attempt=0;attempt<20&&!culled;++attempt){farCapture.endFrame();farCapture.beginDraw();const unsigned before=replays;
            farCapture.capture(vs,[&]{++replays;return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);},false,&args);culled=replays==before&&MaterialCaptureStateTestAccess::rejected(farCapture)>0;}
        assert(culled&&!farCapture.texture()&&!farCapture.occlusionTexture());
        std::puts("MATERIAL_SKIN_ADMISSION_PASS far_capture_omitted no_material_output");
        ok(d->SetVertexShaderConstantF(0,bank[0],43));std::puts("MATERIAL_SKIN_RANGE_PASS buffered_current_palette far_near_far source_reuse");
    }
    if(std::getenv("NORTHLIGHT_TEST_RANGE_STREAMS")){
        assert(!model&&argc<=5);auto split=std::vector<D3DVERTEXELEMENT9>(std::begin(decls),std::end(decls));for(auto& e:split)if(e.Usage==D3DDECLUSAGE_COLOR){e.Stream=1;e.Offset=0;}
        std::stable_sort(split.begin(),split.end()-1,[](const auto& a,const auto& b){return a.Stream!=b.Stream?a.Stream<b.Stream:a.Offset<b.Offset;});
        IDirect3DVertexDeclaration9* splitDecl=nullptr;ok(d->CreateVertexDeclaration(split.data(),&splitDecl));IDirect3DVertexBuffer9* colors=nullptr;
        ok(d->CreateVertexBuffer(4*sizeof(DWORD),D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT,&colors,nullptr));
        static std::unordered_map<void*,uint64_t> revisions;revisions[sourceVB]=revisions[sourceIB]=revisions[colors]=1;
        NorthlightAutoDisplacement::Args geometry{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};V initial[4];std::memcpy(initial,floor,sizeof floor);
        d->SetVertexDeclaration(splitDecl);d->SetStreamSource(0,sourceVB,0,sizeof(V));d->SetStreamSource(1,colors,0,sizeof(DWORD));d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);d->SetIndices(sourceIB);
        float farRow[4];std::memcpy(farRow,bank[3],16);farRow[2]-=200;ok(d->SetVertexShaderConstantF(3,farRow,1));
        for(bool positionOnly:{false,true}){
            ok(sourceVB->Lock(0,sizeof initial,&upload,0));std::memcpy(upload,initial,sizeof initial);ok(sourceVB->Unlock());++revisions[sourceVB];
            NorthlightAutoDisplacement range(d);range.tracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void* p,bool){const auto i=revisions.find(p);return i==revisions.end()?uint64_t(0):i->second;},nullptr);
            if(!positionOnly)DisplacementStateTestAccess::fullRangeLayout(range);
            range.registerMaterial(vs,reinterpret_cast<const DWORD*>(vsBytes.data()),vsBytes.size()/4,1);unsigned hits=0;size_t readBytes=0;
            for(unsigned frame=0;frame<32;++frame){const DWORD color=0xff000000|frame;ok(colors->Lock(0,0,&upload,D3DLOCK_DISCARD));for(unsigned i=0;i<4;++i)static_cast<DWORD*>(upload)[i]=color;ok(colors->Unlock());++revisions[colors];
                float nearest=0;assert(range.nearestMaterialDepth(vs,geometry,nearest)&&nearest>48);hits+=DisplacementStateTestAccess::rangeHits(range);readBytes+=DisplacementStateTestAccess::rangeBytes(range);
                DisplacementStateTestAccess::fullColor(range,d,splitDecl,geometry,color);range.endFrame();}
            assert(hits==(positionOnly?31u:0u));
            V moved[4];std::memcpy(moved,initial,sizeof moved);for(auto& v:moved)v.y-=200.f/c;
            ok(sourceVB->Lock(0,sizeof moved,&upload,0));std::memcpy(upload,moved,sizeof moved);ok(sourceVB->Unlock());++revisions[sourceVB];
            float nearest=0;assert(range.nearestMaterialDepth(vs,geometry,nearest)&&nearest<48&&DisplacementStateTestAccess::rangeHits(range)==0);range.endFrame();
            moved[0].x=NAN;ok(sourceVB->Lock(0,sizeof moved,&upload,0));std::memcpy(upload,moved,sizeof moved);ok(sourceVB->Unlock());++revisions[sourceVB];assert(!range.nearestMaterialDepth(vs,geometry,nearest));
            std::printf("RANGE_STREAM_TRACKING positionOnly=%u frames=32 trackedHits=%u readBytes=%zu positionMutation=observed nonfinite=unknown\n",unsigned(positionOnly),hits,readBytes);
        }
        d->SetStreamSource(1,nullptr,0,0);d->SetVertexDeclaration(decl);drop(splitDecl);drop(colors);std::puts("RANGE_STREAM_TRACKING_PASS mutable_color stable_position changed_position nonfinite full_capture");return 0;
    }
    if(std::getenv("NORTHLIGHT_TEST_MODEL_RANGE")){
        assert(model&&std::strcmp(argv[5],"model4")==0);d->SetStreamSource(0,sourceVB,0,sizeof(V));d->SetIndices(sourceIB);d->SetVertexShader(vs);d->SetPixelShader(ps);
        NorthlightAutoDisplacement::Args geometry{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};const unsigned before=captureDevice.allocations,farVariants=captureDevice.vertexCreates;
        auto move=[&](float offset){for(unsigned bone=0;bone<4;++bone)bank[33+bone*3][3]+=offset;d->SetVertexShaderConstantF(0,bank[0],43);};
        move(-150);bump.beginDraw();unsigned captures=0,natives=0;ok(d->BeginScene());bump.capture(vs,[&]{++captures;return D3D_OK;},false,&geometry);ok(bump.nativeDraw([&]{++natives;return D3D_OK;}));ok(d->EndScene());
        assert(!captures&&natives==1&&!bump.texture()&&!bump.geometryPrepared()&&captureDevice.allocations==before);bump.endFrame();
        if(std::getenv("NORTHLIGHT_TEST_VARIANT_AUDIT"))std::printf("MATERIAL_VARIANT_AUDIT stage=far creates=%u\n",captureDevice.vertexCreates-farVariants);
        move(150);bump.beginDraw();ok(d->BeginScene());bump.capture(vs,[&]{++captures;return D3D_OK;},false,&geometry);ok(d->EndScene());assert(bump.texture()&&bump.geometryPrepared());
        std::puts("MODEL_RANGE_PASS far_capture_skipped native_preserved pose_returned_near");return 0;
    }
    if(std::getenv("NORTHLIGHT_TEST_FAR_ADMISSION")){
        assert(!model&&!grass&&argc==5);settings.autoBumpTerrain=3;bump.configure(settings);bank[3][2]=-150;
        ok(d->SetVertexShaderConstantF(0,bank[0],43));ok(d->SetStreamSource(0,sourceVB,0,sizeof(V)));ok(d->SetIndices(sourceIB));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
        NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};
        const auto render=[&]{bump.endFrame();ok(d->BeginScene());for(unsigned draw=0;draw<1000;++draw){bump.beginDraw();bump.capture(vs,[]{return D3D_OK;},false,&args);}ok(d->EndScene());};
        for(unsigned i=0;i<4;++i)render();const auto pixels=captureDevice.pixelQueries,depth=captureDevice.reads,allocations=captureDevice.allocations,draws=captureDevice.gpuDraws;
        LARGE_INTEGER a,b,hz;QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);for(unsigned frame=0;frame<32;++frame)render();QueryPerformanceCounter(&b);
        assert(captureDevice.allocations==allocations&&captureDevice.gpuDraws==draws&&!bump.texture()&&!bump.geometryPrepared());
        std::printf("MATERIAL_FAR_ADMISSION frames=32 drawsPerFrame=1000 cpuMs=%.6f pixelQueries=%u depthQueries=%u generated=0\n",double(b.QuadPart-a.QuadPart)*1000/double(hz.QuadPart)/32,captureDevice.pixelQueries-pixels,captureDevice.reads-depth);return 0;
    }
    if(std::getenv("NORTHLIGHT_TEST_MATERIAL_RANGE")){
        assert(!model&&!grass&&argc==5);std::vector<IDirect3DTexture9*> distant;
        for(unsigned i=0;i<40;++i){IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT lock{};ok(t->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<512;++y)std::memset(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,128,512*4);ok(t->UnlockRect(0));distant.push_back(t);}
        bank[3][2]=-150;d->SetVertexShaderConstantF(0,bank[0],43);d->SetStreamSource(0,sourceVB,0,sizeof(V));d->SetIndices(sourceIB);d->SetVertexShader(vs);d->SetPixelShader(ps);
        NorthlightAutoDisplacement::Args geometry{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);
        for(unsigned gated=0;gated<2;++gated){settings.autoBump=0;bump.configure(settings);settings.autoBump=1;settings.autoBumpTerrain=2;bump.configure(settings);bump.setMemoryPressure(true);
            const unsigned before=captureDevice.allocations,draws=captureDevice.gpuDraws;unsigned callbacks=0;LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
            for(unsigned frame=0;frame<600;++frame){d->SetTexture(0,distant[frame%distant.size()]);bump.beginDraw();ok(d->BeginScene());auto native=[&]{++callbacks;return D3D_OK;};bump.capture(vs,native,false,gated?&geometry:nullptr);ok(bump.nativeDraw(native));ok(d->EndScene());bump.endFrame();}
            QueryPerformanceCounter(&b);const auto allocations=captureDevice.allocations-before,generated=captureDevice.gpuDraws-draws;
            std::printf("MATERIAL_RANGE_WORKLOAD gated=%u frames=600 textureAllocations=%u generatedMaps=%u callbacks=%u wallMs=%.3f\n",gated,allocations,generated,callbacks,double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart);
            if(gated)assert(!allocations&&!generated&&callbacks==600);else assert(allocations>0&&generated>100&&callbacks==1200);
            IDirect3DQuery9* fence=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));ok(fence->Issue(D3DISSUE_END));while(fence->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)Sleep(1);drop(fence);
        }
        for(auto*& t:distant)drop(t);std::puts("MATERIAL_RANGE_WORKLOAD_PASS");return 0;
    }
    const auto normalsCreates=captureDevice.vertexCreates;
    d->SetVertexShader(vs);d->SetPixelShader(ps);ok(d->BeginScene());bump.capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));},true);ok(d->EndScene());
    if(std::getenv("NORTHLIGHT_TEST_VARIANT_AUDIT"))std::printf("MATERIAL_VARIANT_AUDIT stage=initial-normal creates=%u\n",captureDevice.vertexCreates-normalsCreates);
    if(!bump.texture()){assert(pressure&&bump.active());bump.endFrame();ok(d->BeginScene());bump.capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));},true);ok(d->EndScene());std::puts("MATERIAL_ALLOCATION_RETRY_PASS modes_retained=1");}
    assert(bump.texture());
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,output);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(material);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    float f[79][4]={};f[0][0]=1.f/W;f[0][1]=1.f/H;f[0][2]=.1f;f[0][3]=100;f[1][0]=xs;f[1][1]=ys;f[1][2]=-1;f[2][0]=1;for(unsigned i=0;i<4;++i)f[3+i][i]=1;f[20][2]=1;f[30][2]=1;f[33][0]=W;f[33][1]=H;f[33][2]=W/2;f[33][3]=H/2;f[73][1]=48;std::memcpy(f[74],light,12);f[76][0]=1;f[76][1]=.95f;f[76][2]=.85f;
    d->SetTexture(0,base);d->SetTexture(1,depth);d->SetTexture(4,white);d->SetTexture(5,base);d->SetTexture(8,white);d->SetTexture(9,bump.texture());d->SetTexture(10,white);d->SetTexture(12,baseline);d->SetTexture(14,normal);
    for(unsigned sampler:{0u,1u,4u,5u,8u,9u,10u,12u,14u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    for(unsigned enabled=0;enabled<2;++enabled){f[73][0]=float(enabled)*.75f;d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());save(d,output,folder+(enabled?"normal-on.fgr":"normal-off.fgr"));}
    // Compare the same native diffuse draw used by the game, with and without POM.
    bank[12][1]=bank[12][2]=1;std::memcpy(bank[25],macro,12);
    for(unsigned mode=1;mode<=(model?9u:8u);++mode){
        if(mode>=5&&std::getenv("NORTHLIGHT_TEST_WMO_BLEND"))continue; // Translucent displacement is intentionally unsupported.
        if(std::getenv("NORTHLIGHT_TEST_MATERIAL_DEPTH")&&mode>=5)continue;
        if(pomOnly&&mode!=2)continue;
#ifdef NORTHLIGHT_RELIEF_PREVIEW
        if(mode!=1&&mode!=5)continue;
#endif
        bump.endFrame();settings.autoBumpTerrain=mode>=5?3:mode==1?1:2;settings.autoBumpWalls=settings.autoBumpTerrain;settings.autoBumpModels=settings.autoBumpTerrain;if(grass)settings.autoBumpGrass=settings.autoBumpTerrain;settings.autoBumpStrength=75;settings.autoParallaxShadows=mode==3||mode==4;settings.autoCavity=mode==4;settings.autoDisplacementStrength=mode==8?0:100;settings.autoModelDisplacementStrength=mode==8?0:100;settings.autoModelDisplacementDistance=16;if(argc>5&&std::strcmp(argv[5],"minimum")==0){settings.autoDisplacementDetail=1;settings.autoHeightBalance=100;}bump.configure(settings);
        const bool batchTiming=mode==1&&std::getenv("NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING");
        if(std::getenv("NORTHLIGHT_TEST_MATERIAL_DEMO")){settings.autoParallaxDepth=200;settings.autoParallaxShadowStrength=100;settings.autoCavityStrength=100;settings.autoParallaxSteps=32;settings.autoParallaxShadowSteps=16;bump.configure(settings);}
        if(batchTiming){settings.autoCavity=std::getenv("NORTHLIGHT_TEST_MATERIAL_BATCH_NO_OCCLUSION")?0:1;bump.configure(settings);}
        if(pomOnly){settings.autoBumpStrength=0;settings.autoParallaxShadows=settings.autoCavity=0;if(std::getenv("NORTHLIGHT_TEST_POM_ONLY_DISABLED"))settings.autoParallaxDepth=0;bump.configure(settings);}
        if(argc>6){settings.autoDisplacementStrength=mode==8?0:unsigned(std::atoi(argv[6]));bump.configure(settings);}
        if(argc>7){settings.autoDisplacementDetail=unsigned(std::atoi(argv[7]));bump.configure(settings);}
        const float shadowLight[4]={light[0],light[1],light[2],1};bump.setLight(shadowLight);
        d->SetRenderTarget(0,drawSurface);d->SetDepthStencilSurface(zbuffer);d->SetViewport(&vp);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetVertexShaderConstantF(0,bank[0],43);
        if(model&&mode==9){float moved[4];std::memcpy(moved,bank[31],16);moved[3]+=.25f;d->SetVertexShaderConstantF(31,moved,1);}
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetTexture(0,diffuse);d->SetTexture(1,argc>5&&std::strcmp(argv[5],"composite")==0?diffuse:white);
        if(std::getenv("NORTHLIGHT_TEST_WMO_CUTOUT")||std::getenv("NORTHLIGHT_TEST_MATERIAL_ALPHA_TEST")){ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,127));}
        if(std::getenv("NORTHLIGHT_TEST_WMO_BLEND")){ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));}
        ok(d->SetTexture(D3DVERTEXTEXTURESAMPLER0,white));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0,key,D3DTEXF_POINT));
        ok(d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        for(unsigned sampler:{0u,1u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);}
        // Real DetailDoodad state from the game log: depth-writing alpha cutout
        // with SRCALPHA/INVSRCALPHA, rather than the opaque-only old fixture.
        if(grass){d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,127);}
        float nativeConstants[3][4]={};d->SetPixelShaderConstantF(0,nativeConstants[0],3);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));ok(d->BeginScene());
        const bool indexed=meshGrid||std::getenv("NORTHLIGHT_TEST_DEFERRED_RESIDENT")||deferredMutation||identicalRewrite||mode==6||mode==7||mode==9||(mode>=2&&mode<=4&&std::getenv("NORTHLIGHT_TEST_SHARED_BUFFERED"));
        NorthlightAutoDisplacement::Args geometry{{D3DPT_TRIANGLESTRIP,0,0,0,0,2,false},nullptr,D3DFMT_UNKNOWN,floor,sizeof(V)};
        if(indexed){geometry={{D3DPT_TRIANGLELIST,0,0,sourceVertices,0,sourcePrimitives,true}};d->SetStreamSource(0,sourceVB,0,sizeof(V));d->SetIndices(sourceIB);}
        const bool bufferedStrip=std::getenv("NORTHLIGHT_TEST_DEFERRED_STRIP")!=nullptr;
        if(bufferedStrip){geometry={{D3DPT_TRIANGLESTRIP,0,0,0,0,2,false}};ok(d->SetStreamSource(0,sourceVB,0,sizeof(V)));}
        auto native=[&]{return bufferedStrip?d->DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2):indexed?d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,sourceVertices,0,sourcePrimitives):d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));};
        const float preserved[16]={.13f,.24f,.35f,.46f,.57f,.68f,.79f,.81f,.92f,.12f,.23f,.34f,.45f,.56f,.67f,.78f};ok(d->SetVertexShaderConstantF(252,preserved,4));
        float allConstants[256*4];ok(d->GetVertexShaderConstantF(0,allConstants,256));
#ifdef NORTHLIGHT_MATERIAL_MIRROR
        if(model&&mode==5){
            MaterialConstantMeter reads(d);NorthlightAutoDisplacement probe(&reads);probe.configure(settings);
            probe.setConstantEpochSource(&extension,[](void* context,NorthlightConstantEpoch::Stamp& stamp){return static_cast<ExtensionDevice*>(context)->constantStamp(stamp);});
            probe.registerMaterial(vs,reinterpret_cast<const DWORD*>(vsBytes.data()),vsBytes.size()/4,4);
            if(probe.skinned(vs)){
            assert(probe.prepare(vs,4,7,geometry));reads.calls=reads.registers=0;
            assert(probe.prepare(vs,4,7,geometry));assert(reads.calls==0);
            for(unsigned repeat=0;repeat<3;++repeat){
                ok(probe.draw(native));ok(probe.draw(native,true));reads.calls=reads.registers=0;
                assert(probe.prepare(vs,4,7,geometry));assert(reads.calls==1&&reads.registers<=32);
            }
            assert(reads.builds==1);
            std::printf("MATERIAL_CONSTANT_REUSE_PASS unchangedCalls=0 afterReplayRegisters=%u builds=1\n",reads.registers);
            D3DVERTEXELEMENT9 packed[sizeof decls/sizeof *decls];std::copy(std::begin(decls),std::end(decls),packed);packed[0].Type=D3DDECLTYPE_SHORT4N;
            IDirect3DVertexDeclaration9* unsupported=nullptr;ok(d->CreateVertexDeclaration(packed,&unsupported));ok(d->SetVertexDeclaration(unsupported));assert(!probe.prepare(vs,4,7,geometry));ok(d->SetVertexDeclaration(decl));drop(unsupported);assert(probe.prepare(vs,4,7,geometry));
            }
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));
        }
#endif
        const auto modeCreates=captureDevice.vertexCreates;LARGE_INTEGER variantStart{},variantEnd{},variantHz{};
        if(std::getenv("NORTHLIGHT_TEST_VARIANT_AUDIT")){QueryPerformanceFrequency(&variantHz);QueryPerformanceCounter(&variantStart);}
        const auto depthReads=captureDevice.reads,allocations=captureDevice.allocations;const auto clears=MaterialCaptureStateTestAccess::clears(bump);bump.beginDraw();if(mode==3&&std::getenv("NORTHLIGHT_TEST_SHARED_WRAP"))MaterialCaptureStateTestAccess::wrap(bump);bump.capture(vs,native,!indexed,&geometry);
        if(mode==2){
#ifdef NORTHLIGHT_MATERIAL_STATE_BASELINE
            assert(captureDevice.reads-depthReads==1+unsigned(samples!=0));
#else
            assert(captureDevice.reads-depthReads==(pomOnly?0u:1u)+MaterialCaptureStateTestAccess::clears(bump)-clears);
#endif
            std::printf("MATERIAL_DEPTH_READS samples=%u reads=%u\n",unsigned(samples),captureDevice.reads-depthReads);
        }
        {IDirect3DSurface9 *rt=nullptr,*ds=nullptr;ok(d->GetRenderTarget(0,&rt));ok(d->GetDepthStencilSurface(&ds));assert(rt==drawSurface&&ds==zbuffer);drop(rt);drop(ds);
         if(pomOnly){assert(!bump.texture()&&!bump.occlusionTexture()&&captureDevice.allocations==allocations);assert(bump.active()==(std::getenv("NORTHLIGHT_TEST_POM_ONLY_DISABLED")==nullptr));}else assert(bump.active()&&bump.texture());}
        if((mode>=5&&mode<8||mode==9)){assert(bump.geometryPrepared());if(!unsafeSkin)assert(bump.geometryPrimitives()>2);std::printf("DISPLACEMENT_PREPARED mode=%u triangles=%u heightGPU=%u\n",mode,bump.geometryPrimitives(),unsigned(bump.geometryHeightGPU()));}
        if(mode==8)assert(!bump.geometryPrepared());
        if(mode==7&&coverageVS)d->SetVertexShader(coverageVS);
        if(std::getenv("NORTHLIGHT_TEST_SHARED_CLEAR_HALF")&&mode>=2&&mode<=4)MaterialCaptureStateTestAccess::clearHalf(bump,d);
        if(std::getenv("NORTHLIGHT_TEST_SHARED_ADDRESS_MISMATCH")&&mode>=2&&mode<=4){ok(d->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(1,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
        IDirect3DPixelShader9* hitMask=nullptr;
        if(mode==2&&std::getenv("NORTHLIGHT_TEST_SHARED_HIT_MASK")){
            using namespace NorthlightWaterShaderPatch;std::vector<uint32_t> debug;unsigned result=0;
            assert(NorthlightParallaxPatch::pixel(reinterpret_cast<uint32_t*>(psBytes.data()),psBytes.size()/4,argc>5&&std::strcmp(argv[5],"composite")==0,debug,nullptr,true,&result));debug.pop_back();
            // Native D3D9 rejects writing an output component a second time.
            // Keep the production prefix and native texkill, replacing only color writes.
            for(size_t at=1;at<debug.size();){unsigned op=debug[at]&65535,n=op==65534?(debug[at]>>16)&32767:(debug[at]>>24)&15;
                if(n&&op!=65534&&regType(debug[at+1])==8)debug.erase(debug.begin()+at,debug.begin()+at+n+1);else at+=n+1;}
            debug.insert(debug.end(),{0x03000003,dst(0,result,1),src(0,result,0xff),src(0,result,0xaa),0x03000003,dst(0,result,2),src(0,result,0xaa),src(0,result,0xaa),0x02000001,dst(8,0),src(0,result,0xd2),65535});
            ok(d->CreatePixelShader(reinterpret_cast<DWORD*>(debug.data()),&hitMask));
        }
        ok(bump.nativeDraw([&]{if(hitMask){assert(MaterialCaptureStateTestAccess::usingShared(bump));ok(d->SetPixelShader(hitMask));}return bump.geometryDraw(native,true);}));
        if(std::getenv("NORTHLIGHT_TEST_VARIANT_AUDIT")){QueryPerformanceCounter(&variantEnd);std::printf("MATERIAL_VARIANT_AUDIT stage=mode mode=%u creates=%u total=%u submitMs=%.6f\n",mode,captureDevice.vertexCreates-modeCreates,captureDevice.vertexCreates,double(variantEnd.QuadPart-variantStart.QuadPart)*1000/double(variantHz.QuadPart));}
        assert(bump.finishCaptures());
        if(std::getenv("NORTHLIGHT_TEST_DEFERRED_CAPTURE")){std::printf("MATERIAL_DEFERRED_MODE mode=%u queued=%u flushed=%u\n",mode,MaterialCaptureStateTestAccess::queued(bump),MaterialCaptureStateTestAccess::flushed(bump));assert(MaterialCaptureStateTestAccess::captured(bump,model?4:grass?3:argc>5?2:1)==1);}
        if(batchTiming||(mode==2&&std::getenv("NORTHLIGHT_TEST_SHARED_TIMING"))){
            IDirect3DQuery9 *frequency=nullptr,*start=nullptr,*finish=nullptr,*disjoint=nullptr;
            ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&finish));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            const bool batched=batchTiming&&std::getenv("NORTHLIGHT_TEST_MATERIAL_BATCHED"),packetLimit=std::getenv("NORTHLIGHT_TEST_DEFERRED_LIMIT")!=nullptr;const unsigned draws=batchTiming?(packetLimit?520:std::getenv("NORTHLIGHT_TEST_CAPTURE_DRAWS")?std::atoi(std::getenv("NORTHLIGHT_TEST_CAPTURE_DRAWS")):32):1;
            const bool deferredEnabled=std::getenv("NORTHLIGHT_TEST_DEFERRED_CAPTURE")!=nullptr;
            const bool barrier=std::getenv("NORTHLIGHT_TEST_DEFERRED_BARRIER")!=nullptr;
            const bool textureWrite=std::getenv("NORTHLIGHT_TEST_DEFERRED_TEXTURE_WRITE")!=nullptr;
            const bool multiStream=std::getenv("NORTHLIGHT_TEST_DEFERRED_MULTISTREAM")!=nullptr;
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> multiDecl;
            if(multiStream){D3DVERTEXELEMENT9 elements[sizeof decls/sizeof *decls];std::copy(std::begin(decls),std::end(decls),elements);elements[5].Stream=1;
                ok(d->CreateVertexDeclaration(elements,multiDecl.out()));ok(d->SetVertexDeclaration(multiDecl.p));ok(d->SetStreamSource(1,sourceVB,0,sizeof(V)));}
            unsigned writeKind=0;
            const auto writeAlpha=[&](bool restore){D3DLOCKED_RECT lock{};const auto kind=restore?0u:(writeKind++%4);
                if(kind==2)gameTexture.SetLOD(gameTexture.GetLOD());if(kind==3)gameTexture.GenerateMipSubLevels();
                if(kind==1)ok(gameSurface.LockRect(&lock,nullptr,0));else ok(gameTexture.LockRect(0,&lock,nullptr,0));
                for(unsigned y=0;y<first.height;++y){auto* row=static_cast<unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch;for(unsigned x=0;x<first.width;++x)row[x*4+3]=restore?first.pixels[(size_t(y)*first.width+x)*4+3]:0;}
                if(kind==1)ok(gameSurface.UnlockRect());else ok(gameTexture.UnlockRect(0));};
            if(batchTiming&&deferredEnabled&&std::getenv("NORTHLIGHT_TEST_DEFERRED_PRESSURE")){
                for(unsigned i=0;i<3;++i){bump.endFrame();bump.beginDraw();bump.capture(vs,native,!indexed,&geometry);ok(bump.nativeDraw(native));assert(bump.finishCaptures());}
                MaterialCaptureStateTestAccess::residency(bump,true);
                bump.endFrame();bump.beginDraw();bump.capture(vs,native,!indexed,&geometry);ok(bump.nativeDraw(native));assert(MaterialCaptureStateTestAccess::queued(bump)==1);
                bump.setMemoryPressure(true);assert(MaterialCaptureStateTestAccess::released(bump)&&!bump.texture()&&bump.finishCaptures());
                ok(d->EndScene());std::puts("MATERIAL_DEFERRED_PRESSURE_PASS queue_and_source_cache_released native_retained");return 0;
            }
            if(batchTiming&&deferredEnabled&&std::getenv("NORTHLIGHT_TEST_DEFERRED_FAILURE")){
                bump.endFrame();bump.beginDraw();bump.capture(vs,native,!indexed,&geometry);ok(bump.nativeDraw(native));assert(MaterialCaptureStateTestAccess::queued(bump)==1);
                captureDevice.failDeferred=true;assert(!bump.finishCaptures()&&!bump.texture()&&!bump.occlusionTexture());
                assert(bump.finishCaptures());bump.endFrame();assert(!MaterialCaptureStateTestAccess::queued(bump));ok(d->EndScene());
                std::puts("MATERIAL_DEFERRED_FAILURE_PASS incomplete_capture_hidden queue_released native_retained");return 0;
            }
            if(batchTiming&&deferredEnabled&&std::getenv("NORTHLIGHT_TEST_DEFERRED_UNUSED")){
                bump.endFrame();bump.beginDraw();const auto generated=captureDevice.gpuDraws,cleared=captureDevice.clears;bump.capture(vs,native,!indexed,&geometry);
                assert(MaterialCaptureStateTestAccess::queued(bump)==1&&captureDevice.gpuDraws==generated&&captureDevice.clears==cleared);
                bump.endFrame();assert(!bump.texture()&&!MaterialCaptureStateTestAccess::queued(bump)&&captureDevice.gpuDraws==generated&&captureDevice.clears==cleared);
                std::puts("MATERIAL_DEFERRED_UNUSED_PASS zero_draws zero_clears leases_released");
            }
            unsigned long long rewriteResident=0,rewriteUP=0,rewriteReads=0,rewriteHits=0;unsigned rewriteFrame=0;
            const auto render=[&]{bump.endFrame();ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));
                if(identicalRewrite){auto bytes=rewriteVertices;
                    if(std::getenv("NORTHLIGHT_TEST_REWRITE_CHANGED")){float uv=0;const auto offset=bytes.size()-sizeof(V)+offsetof(V,u);std::memcpy(&uv,bytes.data()+offset,sizeof uv);uv+=float(++rewriteFrame)/128;std::memcpy(bytes.data()+offset,&uv,sizeof uv);}
                    ok(sourceVB->Lock(0,UINT(bytes.size()),&upload,0));std::memcpy(upload,bytes.data(),bytes.size());ok(sourceVB->Unlock());
                    ok(sourceIB->Lock(0,UINT(rewriteIndices.size()),&upload,0));std::memcpy(upload,rewriteIndices.data(),rewriteIndices.size());ok(sourceIB->Unlock());++mutationRevision;}
                if(batched){for(unsigned i=0;i<draws;++i){bump.beginDraw();ok(bump.nativeDraw(native));}for(unsigned i=0;i<draws;++i){bump.beginDraw();bump.capture(vs,native,!indexed,&geometry);}}
                else for(unsigned i=0;i<draws;++i){
                    if(textureWrite){writeAlpha(true);ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}
                    if(deferredMutation){V moved[4];std::memcpy(moved,floor,sizeof moved);for(auto& v:moved)v.x+=float(i%7)*.025f;
                        ok(sourceVB->Lock(0,sizeof moved,&upload,D3DLOCK_DISCARD));std::memcpy(upload,moved,sizeof moved);ok(sourceVB->Unlock());++mutationRevision;
                        ok(d->SetRenderState(D3DRS_FILLMODE,i%2?D3DFILL_WIREFRAME:D3DFILL_SOLID));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,i%3==0));RECT rect={LONG(i%7),LONG(i%5),LONG(W-i%9),LONG(H-i%11)};ok(d->SetScissorRect(&rect));
                        for(unsigned bone=0;bone<4;++bone){float pose[4];std::memcpy(pose,bank[31+3*bone],16);pose[3]+=float(i%5)*.04f;ok(d->SetVertexShaderConstantF(31+3*bone,pose,1));}
                        float tint[]={.1f+float(i%5)*.05f,.2f,.3f,.8f};ok(d->SetPixelShaderConstantF(0,tint,1));
                        ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,i%2?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,i%3?D3DTEXF_LINEAR:D3DTEXF_POINT));
                        int integer[64]={};BOOL boolean[16]={};integer[i%64]=int(i)-15;boolean[i%16]=i%2;ok(d->SetVertexShaderConstantI(0,integer,16));ok(d->SetPixelShaderConstantI(0,integer,16));ok(d->SetVertexShaderConstantB(0,boolean,16));ok(d->SetPixelShaderConstantB(0,boolean,16));}
                    if(barrier)bump.setDeferredCapture(deferredEnabled&&i%8!=7);
                    if(deferredPose)for(unsigned bone=0;bone<4;++bone){float pose[4];std::memcpy(pose,bank[31+3*bone],16);pose[3]+=float(i%5)*.04f;ok(d->SetVertexShaderConstantF(31+3*bone,pose,1));}
                    bump.beginDraw();bump.capture(vs,native,!indexed,&geometry);
                    if(deferredMutation){V expected[4];std::memcpy(expected,floor,sizeof expected);for(auto& v:expected)v.x+=float(i%7)*.025f;MaterialCaptureStateTestAccess::mutation(bump,expected,sizeof expected);}
                    ok(bump.nativeDraw([&]{return bump.geometryDraw(native,true);}));
                    if(textureWrite){const auto pending=MaterialCaptureStateTestAccess::queued(bump);bump.beforeTextureWrite(baseline);assert(MaterialCaptureStateTestAccess::queued(bump)==pending);
                        if(deferredEnabled)assert(bump.pendingTextures());writeAlpha(false);assert(!bump.pendingTextures());}
                    if(deferredMutation){ok(sourceVB->Lock(0,sizeof floor,&upload,D3DLOCK_DISCARD));std::memset(upload,0,sizeof floor);ok(sourceVB->Unlock());++mutationRevision;float destroyed[4]={0,0,0,20};ok(d->SetVertexShaderConstantF(31,destroyed,1));}
                }
                float beforePixel[224*4],afterPixel[224*4],beforeVertex[256*4],afterVertex[256*4];
                int beforeInteger[2][64],afterInteger[2][64];BOOL beforeBoolean[2][16],afterBoolean[2][16];
                if(deferredMutation||deferredPose){ok(d->GetPixelShaderConstantF(0,beforePixel,224));ok(d->GetVertexShaderConstantF(0,beforeVertex,256));}
                if(deferredMutation){ok(d->GetVertexShaderConstantI(0,beforeInteger[0],16));ok(d->GetPixelShaderConstantI(0,beforeInteger[1],16));ok(d->GetVertexShaderConstantB(0,beforeBoolean[0],16));ok(d->GetPixelShaderConstantB(0,beforeBoolean[1],16));}
                assert(bump.finishCaptures());bump.setDeferredCapture(deferredEnabled);
                if(identicalRewrite){MaterialCaptureStateTestAccess::replayContent(bump,rewriteResident,rewriteUP,rewriteReads,rewriteHits);
#ifdef NORTHLIGHT_MATERIAL_MIRROR
                    assert(!extension.audit());
#endif
                }
                if(deferredMutation||deferredPose){ok(d->GetPixelShaderConstantF(0,afterPixel,224));ok(d->GetVertexShaderConstantF(0,afterVertex,256));assert(!std::memcmp(beforePixel,afterPixel,sizeof beforePixel)&&!std::memcmp(beforeVertex,afterVertex,sizeof beforeVertex));}
                if(deferredMutation){ok(d->GetVertexShaderConstantI(0,afterInteger[0],16));ok(d->GetPixelShaderConstantI(0,afterInteger[1],16));ok(d->GetVertexShaderConstantB(0,afterBoolean[0],16));ok(d->GetPixelShaderConstantB(0,afterBoolean[1],16));assert(!std::memcmp(beforeInteger,afterInteger,sizeof beforeInteger)&&!std::memcmp(beforeBoolean,afterBoolean,sizeof beforeBoolean));}
            };
            captureDevice.failResident=std::getenv("NORTHLIGHT_TEST_DEFERRED_CACHE_FAILURE")!=nullptr;
            for(unsigned i=0;i<4;++i)render();
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));LARGE_INTEGER cpuStart,cpuEnd,cpuHz;QueryPerformanceFrequency(&cpuHz);QueryPerformanceCounter(&cpuStart);
            const unsigned repeats=batchTiming?8:32;for(unsigned i=0;i<repeats;++i)render();QueryPerformanceCounter(&cpuEnd);
            ok(finish->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(d->EndScene());
            const auto wait=[](IDirect3DQuery9* q,void* result,DWORD size){const auto began=GetTickCount();HRESULT hr;while((hr=q->GetData(result,size,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-began<10000);Sleep(1);}ok(hr);};
            UINT64 hz=0,a=0,b=0;BOOL invalid=TRUE;wait(finish,&b,sizeof b);wait(start,&a,sizeof a);wait(frequency,&hz,sizeof hz);wait(disjoint,&invalid,sizeof invalid);assert(!invalid&&hz&&b>=a);
            std::printf("%s width=%u height=%u msaa=%u gpuMs=%.6f submitMs=%.6f repeats=%u draws=%u batched=%u\n",batchTiming?"MATERIAL_BATCH_TIMING":"SHARED_POM_TIMING",W,H,unsigned(samples),double(b-a)*1000/double(hz)/repeats,double(cpuEnd.QuadPart-cpuStart.QuadPart)*1000/double(cpuHz.QuadPart)/repeats,repeats,draws,unsigned(batched));
            if(identicalRewrite)std::printf("MATERIAL_IDENTICAL_REWRITE frames=%u draws=%u resident=%llu up=%llu readBytes=%llu verifiedHits=%llu changed=%u\n",4+repeats,draws,rewriteResident,rewriteUP,rewriteReads,rewriteHits,unsigned(std::getenv("NORTHLIGHT_TEST_REWRITE_CHANGED")!=nullptr));
            if(batchTiming&&deferredEnabled){std::printf("MATERIAL_DEFERRED_BENCH queued=%u flushed=%u mutation=%u barrier=%u\n",MaterialCaptureStateTestAccess::queued(bump),MaterialCaptureStateTestAccess::flushed(bump),unsigned(deferredMutation),unsigned(barrier));assert(MaterialCaptureStateTestAccess::queued(bump)==(multiStream||!samples?0:draws-(barrier?draws/8:0))&&MaterialCaptureStateTestAccess::flushed(bump)==MaterialCaptureStateTestAccess::queued(bump));
                if(samples&&!multiStream&&!textureWrite)MaterialCaptureStateTestAccess::stateReuse(bump);
                if(!identicalRewrite&&samples&&(meshGrid||bufferedStrip||std::getenv("NORTHLIGHT_TEST_DEFERRED_RESIDENT")))MaterialCaptureStateTestAccess::residency(bump,!captureDevice.failResident);
                if(multiStream){assert(!MaterialCaptureStateTestAccess::sourceReadBytes(bump));std::puts("MATERIAL_MULTISTREAM_PREFLIGHT_PASS zero_source_reads immediate_retained");}}
            if(batchTiming&&std::getenv("NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE")){
                if(samples){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));ok(d->StretchRect(drawSurface,nullptr,baseSurface,nullptr,D3DTEXF_NONE));}
                save(d,baseSurface,folder+"batch-color.fgr");IDirect3DSurface9* target=nullptr;ok(bump.texture()->GetSurfaceLevel(0,&target));save(d,target,folder+"batch-normal.fgr");drop(target);
                if(bump.occlusionTexture()){ok(bump.occlusionTexture()->GetSurfaceLevel(0,&target));save(d,target,folder+"batch-occlusion.fgr");drop(target);}
            }
            drop(frequency);drop(start);drop(finish);drop(disjoint);return 0;
        }
        ok(d->EndScene());drop(hitMask);
        {IDirect3DBaseTexture9* restored=nullptr;ok(d->GetTexture(D3DVERTEXTEXTURESAMPLER0,&restored));assert(restored==white);drop(restored);
         for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV}){DWORD value=0;ok(d->GetSamplerState(D3DVERTEXTEXTURESAMPLER0,key,&value));assert(value==DWORD(key==D3DSAMP_MINFILTER||key==D3DSAMP_MAGFILTER?D3DTEXF_POINT:D3DTADDRESS_CLAMP));}}
        if(samples)referenceDepth([&]{return bump.geometryDraw(native);});
        if(samples){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));ok(d->StretchRect(drawSurface,nullptr,baseSurface,nullptr,D3DTEXF_NONE));}
        if(mode==2&&std::getenv("NORTHLIGHT_TEST_SHARED_HIT_MASK"))save(d,baseSurface,folder+"shared-hit-mask.fgr");
#ifdef NORTHLIGHT_MATERIAL_MIRROR
        assert(!extension.audit());
#endif
        float restored[16];d->GetVertexShaderConstantF(252,restored,4);for(unsigned i=0;i<16;++i)if(preserved[i]!=restored[i])std::printf("VS_RESTORE mode=%u c%u[%u] expected=%f actual=%f\n",mode,252+i/4,i%4,preserved[i],restored[i]);assert(!std::memcmp(preserved,restored,sizeof restored));
        float allRestored[256*4];ok(d->GetVertexShaderConstantF(0,allRestored,256));assert(!std::memcmp(allConstants,allRestored,sizeof allConstants));
        IDirect3DVertexBuffer9* actual=nullptr;UINT offset=0,stride=0;d->GetStreamSource(0,&actual,&offset,&stride);assert(indexed?actual==sourceVB:actual==nullptr);drop(actual);
        IDirect3DVertexShader9* actualVS=nullptr;d->GetVertexShader(&actualVS);assert(actualVS==(mode==7&&coverageVS?coverageVS:vs));drop(actualVS);
        d->SetRenderTarget(0,output);d->SetDepthStencilSurface(nullptr);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(material);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
        d->SetTexture(0,base);d->SetTexture(1,depth);d->SetTexture(4,white);d->SetTexture(5,base);d->SetTexture(6,bump.occlusionTexture());d->SetTexture(8,white);d->SetTexture(9,bump.texture());d->SetTexture(10,white);d->SetTexture(12,baseline);d->SetTexture(14,normal);
        for(unsigned sampler:{0u,1u,4u,5u,6u,8u,9u,10u,12u,14u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
        f[73][0]=pomOnly?0:.75f;f[78][0]=bump.occlusionTexture()?1.f:0.f;f[78][1]=bump.occlusionDistance();f[18][0]=.2f;f[18][1]=.22f;f[18][2]=.26f;f[18][3]=1;
        if(mode==1){f[73][0]=0;d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());save(d,output,folder+"native-normal-off.fgr");f[73][0]=.75f;}
        d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());const char* names[]={"normal-on.fgr","pom-on.fgr","pom-shadow.fgr","pom-shadow-cavity.fgr","displacement-on.fgr","displacement-indexed.fgr","displacement-coverage.fgr","displacement-zero.fgr","displacement-animated.fgr"};save(d,output,folder+names[mode-1]);
        if(mode==1||mode==5){IDirect3DSurface9* map=nullptr;ok(bump.texture()->GetSurfaceLevel(0,&map));save(d,map,folder+(mode==1?"geometry-off-depth.fgr":"geometry-on-depth.fgr"));drop(map);}
        if(mode==4&&std::getenv("NORTHLIGHT_TEST_MATERIAL_DEMO")){IDirect3DSurface9* map=nullptr;if(bump.occlusionTexture()){ok(bump.occlusionTexture()->GetSurfaceLevel(0,&map));save(d,map,folder+"occlusion-factors.fgr");drop(map);}}
        if(materialProfiling&&mode<=4){ok(d->BeginScene());pixelAudit.sample(&materialProfile,nullptr,[&](UINT x,UINT y){
            Vertex small[]={{-.5f,-.5f,0,1,0,0},{float(x)-.5f,-.5f,0,1,1,0},{-.5f,float(y)-.5f,0,1,0,1},{float(x)-.5f,float(y)-.5f,0,1,1,1}};
            D3DVIEWPORT9 vp={0,0,x,y,0,1};ok(d->SetViewport(&vp));return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,small,sizeof(Vertex));});ok(d->EndScene());
            save(d,output,folder+"audit-sync.fgr");pixelAudit.poll();assert(pixelAudit.lastFrame==127&&pixelAudit.last[0]>0&&pixelAudit.last[1]>0);
            assert(pixelAudit.last[4]>=pixelAudit.last[0]&&pixelAudit.last[5]<=pixelAudit.last[4]&&pixelAudit.last[0]+pixelAudit.last[5]<=pixelAudit.last[4]);
            assert(pixelAudit.last[6]>=pixelAudit.last[0]&&pixelAudit.last[7]<=pixelAudit.last[6]&&pixelAudit.last[8]<=pixelAudit.last[6]);
            if(mode==1||mode==2)assert(!pixelAudit.last[2]&&!pixelAudit.last[3]);if(mode==3)assert(!pixelAudit.last[2]&&pixelAudit.last[3]>0);if(mode==4)assert(pixelAudit.last[2]>0&&pixelAudit.last[3]>0);
#ifdef NORTHLIGHT_MATERIAL_MIRROR
            assert(!extension.audit());
#endif
            std::printf("MATERIAL_AUDIT_PASS mode=%u valid=%u normal=%u cavity=%u shadow=%u\n",mode,pixelAudit.last[0],pixelAudit.last[1],pixelAudit.last[2],pixelAudit.last[3]);}
    }
    std::printf("MATERIAL_MSAA_PASS samples=%u pressure=%u actual_draws=1 depth_and_state_restored=1\n",unsigned(samples),unsigned(pressure));
    if(materialProfiling){materialProfile.endFrame();save(d,output,folder+"profile-sync.fgr");materialProfile.poll();}
    std::printf("POM_SHARED_TEST produced=%llu native=%llu clears=%llu\n",(unsigned long long)MaterialCaptureStateTestAccess::produced(bump),(unsigned long long)MaterialCaptureStateTestAccess::consumed(bump),(unsigned long long)MaterialCaptureStateTestAccess::clears(bump));
    std::puts("OBLIQUE_NATIVE_POM_PASS camera_height=.7 pitch_degrees=12 native_diffuse_normal_cache_capture_and_composite");std::puts("OBLIQUE_OCCLUSION_PASS");return 0;
}
