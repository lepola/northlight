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
#include "patch_terrain_shadow_coverage.h"
#include "forwarders.h"
struct MaterialDepthMeter:ForwardIDirect3DDevice9 {
    unsigned reads=0;explicit MaterialDepthMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** s)override{++reads;return real->GetDepthStencilSurface(s);}
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
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));
    const UINT bytes=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:4;std::ofstream f(path,std::ios::binary);UINT header[]={0x31524746,desc.Width,desc.Height,UINT(desc.Format),bytes};f.write((char*)header,20);D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(UINT y=0;y<desc.Height;++y)f.write((char*)lock.pBits+y*lock.Pitch,desc.Width*bytes);stage->UnlockRect();drop(stage);
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc>=3);std::string folder=argv[2];const bool model=argc>5&&std::strstr(argv[5],"model"),grass=argc>5&&std::strcmp(argv[5],"grass")==0;
    const auto samples=static_cast<D3DMULTISAMPLE_TYPE>(std::getenv("NORTHLIGHT_TEST_MSAA")?std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA")):0);
    const bool pressure=std::getenv("NORTHLIGHT_TEST_PRESSURE")!=nullptr;
    const bool resolveMSDepth=samples&&std::strstr(argv[1],"SysWOW64")==nullptr;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NormalOblique";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.MultiSampleType=samples;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    MaterialAllocationMeter meter(d,pressure);DeviceMirror mirror;mirror.gate.exclusive=true;ExtensionDevice extension(&meter,&mirror);d=&extension;
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
    auto first=image(folder+"source.fgr");IDirect3DTexture9* diffuse=nullptr;ok(d->CreateTexture(first.width,first.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&diffuse,nullptr));
    for(unsigned level=0;level<diffuse->GetLevelCount();++level){auto im=image(folder+"source.fgr."+std::to_string(level));D3DLOCKED_RECT lock{};ok(diffuse->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);diffuse->UnlockRect(level);}
    auto vsBytes=read(folder+"terrain-vs.bin"),psBytes=read(folder+"terrain-ps.bin");std::vector<std::uint32_t> patched;assert(!NorthlightWaterShaderPatch::patch((std::uint32_t*)vsBytes.data(),vsBytes.size()/4,patched));
    IDirect3DVertexShader9 *vs=nullptr,*forwardVS=nullptr;IDirect3DPixelShader9 *ps=nullptr,*beauty=nullptr,*material=nullptr;
    ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreateVertexShader((DWORD*)patched.data(),&forwardVS));ok(d->CreatePixelShader((DWORD*)psBytes.data(),&ps));
    auto beautyBytes=read(folder+"PreviewBase.bin");ok(d->CreatePixelShader((DWORD*)beautyBytes.data(),&beauty));ok(d->CreatePixelShader(kMaterialCompositeShader,&material));
    const float cameraHeight=argc>4?std::atof(argv[4]):.7f;const float angle=argc>3?std::atof(argv[3])*.01745329252f:.20943951f,s=std::sin(angle),c=std::cos(angle),xs=1.7320508f*H/W,ys=1.7320508f;
    float bank[79][4]={};bank[0][0]=1;bank[1][1]=s;bank[1][2]=-c;bank[2][1]=c;bank[2][2]=s;bank[3][1]=-cameraHeight*c+4*s;bank[3][2]=-cameraHeight*s-4*c;bank[3][3]=1;
    bank[4][0]=xs;bank[5][1]=ys;bank[6][2]=100.f/(.1f-100.f);bank[6][3]=-1;bank[7][2]=.1f*100/(.1f-100);
    bank[18][0]=bank[18][1]=.25f;bank[13][0]=bank[13][1]=.5f;
    float light[]={-.4f,.3f,.8660254f};
    if(std::getenv("NORTHLIGHT_TEST_MSAA")){light[0]=-.94f;light[1]=.2f;light[2]=.28f;}
    const float energy=light[1]*c+light[2]*s;
    bank[78][0]=.2f+energy;bank[78][1]=.22f+.95f*energy;bank[78][2]=.26f+.85f*energy;
    if(argc>5&&!grass){const float projectionRows[4][4]={{xs,0,0,0},{0,ys,0,0},{0,0,bank[6][2],bank[7][2]},{0,0,-1,0}};std::memcpy(bank[2],projectionRows,sizeof projectionRows);const float viewRows[3][4]={{1,0,0,0},{0,s,c,-cameraHeight*c+4*s},{0,-c,s,-cameraHeight*s-4*c}};std::memcpy(bank[31],viewRows,sizeof viewRows);}
    if(grass){bank[9][0]=0;bank[9][1]=1;}
    const float macro[]={bank[78][0],bank[78][1],bank[78][2]};auto* baseline=solid(d,macro[0],macro[1],macro[2],1);auto* normal=solid(d,0,c,s,1);IDirect3DTexture9* white=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT wl{};ok(white->LockRect(0,&wl,nullptr,0));*static_cast<DWORD*>(wl.pBits)=0xffffffff;ok(white->UnlockRect(0));
    struct V{float x,y,z,nx,ny,nz,u,v;DWORD color,blend,weights=255,bones=0;};V floor[]={{-4,-3.4f,0,0,0,1,-1,-.85f,0xffffffff,0xffffffff},{4,-3.4f,0,0,0,1,1,-.85f,0xffffffff,0xffffffff},{-4,4.6f,0,0,0,1,-1,1.15f,0xffffffff,0xffffffff},{4,4.6f,0,0,0,1,1,1.15f,0xffffffff,0xffffffff}};
    if(model){for(unsigned i=0;i<4;++i){floor[i].weights=std::strcmp(argv[5],"model4")==0?0x3f404040:255;floor[i].bones=0x100;}bank[6][0]=1;bank[6][1]=bank[6][2]=bank[6][3]=0;bank[7][1]=1;bank[7][0]=bank[7][2]=bank[7][3]=0;bank[10][0]=bank[10][1]=bank[10][2]=.7f;bank[11][0]=bank[11][1]=bank[11][2]=.3f;bank[12][0]=.4f;bank[12][1]=-.3f;bank[12][2]=-.8660254f;for(unsigned j=0;j<4;++j)bank[28][j]=1;bank[30][0]=0;bank[30][1]=bank[30][2]=1;for(unsigned r=0;r<3;++r)std::memcpy(bank[34+r],bank[31+r],16);bank[34][3]+=.15f;if(std::strcmp(argv[5],"model4")==0){for(auto& v:floor){v.weights=0x3f404040;v.bones=0x03020100;}for(unsigned bone=2;bone<4;++bone){for(unsigned r=0;r<3;++r)std::memcpy(bank[31+bone*3+r],bank[31+r],16);bank[31+bone*3][3]+=.05f*bone;}}}
    const bool unsafeSkin=argc>5&&std::strcmp(argv[5],"modelunsafe")==0;
    if(unsafeSkin){for(unsigned r=0;r<3;++r){bank[34+r][0]=.8660254f*bank[31+r][0]-.5f*bank[31+r][2];bank[34+r][2]=.5f*bank[31+r][0]+.8660254f*bank[31+r][2];}for(unsigned i=0;i<4;++i){floor[i].bones=0x03020100;floor[i].weights=(i&1)?0x0000807f:0x3f404040;}for(unsigned bone=2;bone<4;++bone){for(unsigned r=0;r<3;++r)std::memcpy(bank[31+bone*3+r],bank[31+r],16);bank[31+bone*3][3]+=.1f*bone;}}
    const D3DVERTEXELEMENT9 decls[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},{0,32,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,36,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1},{0,40,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,44,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(decls,&decl));
    IDirect3DVertexBuffer9* sourceVB=nullptr;IDirect3DIndexBuffer9* sourceIB=nullptr;void* upload=nullptr;
    ok(d->CreateVertexBuffer(sizeof floor,0,0,D3DPOOL_MANAGED,&sourceVB,nullptr));ok(sourceVB->Lock(0,sizeof floor,&upload,0));std::memcpy(upload,floor,sizeof floor);ok(sourceVB->Unlock());
    const WORD sourceIndices[]={0,1,2,2,1,3};ok(d->CreateIndexBuffer(sizeof sourceIndices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&sourceIB,nullptr));ok(sourceIB->Lock(0,sizeof sourceIndices,&upload,0));std::memcpy(upload,sourceIndices,sizeof sourceIndices);ok(sourceIB->Unlock());
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
    MaterialDepthMeter captureDevice(d);NorthlightAutoBump bump(&captureDevice);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpStrength=100;bump.configure(settings);bump.setProjection(xs,ys,-1);
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    bump.setConstantEpochSource(&extension,[](void* context,NorthlightConstantEpoch::Stamp& stamp){return static_cast<ExtensionDevice*>(context)->constantStamp(stamp);});
#endif
    bump.setMemoryPressure(pressure);assert(bump.active());
    bump.setGeometryTracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);
    bump.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)vsBytes.data(),vsBytes.size()/4),(DWORD*)vsBytes.data(),vsBytes.size()/4,model?3:0);bump.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)psBytes.data(),psBytes.size()/4));
    d->SetVertexShader(vs);d->SetPixelShader(ps);ok(d->BeginScene());bump.capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));},true);ok(d->EndScene());
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
#ifdef NORTHLIGHT_RELIEF_PREVIEW
        if(mode!=1&&mode!=5)continue;
#endif
        bump.endFrame();settings.autoBumpTerrain=mode>=5?3:mode==1?1:2;settings.autoBumpWalls=settings.autoBumpTerrain;settings.autoBumpModels=settings.autoBumpTerrain;if(grass)settings.autoBumpGrass=settings.autoBumpTerrain;settings.autoBumpStrength=75;settings.autoParallaxShadows=mode==3||mode==4;settings.autoCavity=mode==4;settings.autoDisplacementStrength=mode==8?0:100;settings.autoModelDisplacementStrength=mode==8?0:100;settings.autoModelDisplacementDistance=16;if(argc>5&&std::strcmp(argv[5],"minimum")==0){settings.autoDisplacementDetail=1;settings.autoHeightBalance=100;}bump.configure(settings);
        if(argc>6){settings.autoDisplacementStrength=mode==8?0:unsigned(std::atoi(argv[6]));bump.configure(settings);}
        if(argc>7){settings.autoDisplacementDetail=unsigned(std::atoi(argv[7]));bump.configure(settings);}
        const float shadowLight[4]={light[0],light[1],light[2],1};bump.setLight(shadowLight);
        d->SetRenderTarget(0,drawSurface);d->SetDepthStencilSurface(zbuffer);d->SetViewport(&vp);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetVertexShaderConstantF(0,bank[0],43);
        if(model&&mode==9){float moved[4];std::memcpy(moved,bank[31],16);moved[3]+=.25f;d->SetVertexShaderConstantF(31,moved,1);}
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetTexture(0,diffuse);d->SetTexture(1,argc>5&&std::strcmp(argv[5],"composite")==0?diffuse:white);
        for(unsigned sampler:{0u,1u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);}
        // Real DetailDoodad state from the game log: depth-writing alpha cutout
        // with SRCALPHA/INVSRCALPHA, rather than the opaque-only old fixture.
        if(grass){d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,127);}
        float nativeConstants[3][4]={};d->SetPixelShaderConstantF(0,nativeConstants[0],3);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));ok(d->BeginScene());
        const bool indexed=mode==6||mode==7||mode==9;
        NorthlightAutoDisplacement::Args geometry{{D3DPT_TRIANGLESTRIP,0,0,0,0,2,false},nullptr,D3DFMT_UNKNOWN,floor,sizeof(V)};
        if(indexed){geometry={{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};d->SetStreamSource(0,sourceVB,0,sizeof(V));d->SetIndices(sourceIB);}
        auto native=[&]{return indexed?d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2):d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));};
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
        const auto depthReads=captureDevice.reads;bump.capture(vs,native,!indexed,&geometry);
        if(mode==2){
#ifdef NORTHLIGHT_MATERIAL_STATE_BASELINE
            assert(captureDevice.reads-depthReads==1+unsigned(samples!=0));
#else
            assert(captureDevice.reads-depthReads==1);
#endif
            std::printf("MATERIAL_DEPTH_READS samples=%u reads=%u\n",unsigned(samples),captureDevice.reads-depthReads);
        }
        {IDirect3DSurface9 *rt=nullptr,*ds=nullptr;ok(d->GetRenderTarget(0,&rt));ok(d->GetDepthStencilSurface(&ds));assert(rt==drawSurface&&ds==zbuffer);drop(rt);drop(ds);assert(bump.active()&&bump.texture());}
        if((mode>=5&&mode<8||mode==9)){assert(bump.geometryPrepared());if(!unsafeSkin)assert(bump.geometryPrimitives()>2);std::printf("DISPLACEMENT_PREPARED mode=%u triangles=%u\n",mode,bump.geometryPrimitives());}
        if(mode==8)assert(!bump.geometryPrepared());
        if(mode==7&&coverageVS)d->SetVertexShader(coverageVS);
        ok(bump.nativeDraw([&]{return bump.geometryDraw(native,true);}));ok(d->EndScene());
        if(samples)referenceDepth([&]{return bump.geometryDraw(native);});
        if(samples){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));ok(d->StretchRect(drawSurface,nullptr,baseSurface,nullptr,D3DTEXF_NONE));}
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
        f[73][0]=.75f;f[78][0]=bump.occlusionTexture()?1.f:0.f;f[18][0]=.2f;f[18][1]=.22f;f[18][2]=.26f;f[18][3]=1;
        d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());const char* names[]={"normal-on.fgr","pom-on.fgr","pom-shadow.fgr","pom-shadow-cavity.fgr","displacement-on.fgr","displacement-indexed.fgr","displacement-coverage.fgr","displacement-zero.fgr","displacement-animated.fgr"};save(d,output,folder+names[mode-1]);
        if(mode==1||mode==5){IDirect3DSurface9* map=nullptr;ok(bump.texture()->GetSurfaceLevel(0,&map));save(d,map,folder+(mode==1?"geometry-off-depth.fgr":"geometry-on-depth.fgr"));drop(map);}
    }
    std::printf("MATERIAL_MSAA_PASS samples=%u pressure=%u actual_draws=1 depth_and_state_restored=1\n",unsigned(samples),unsigned(pressure));
    std::puts("OBLIQUE_NATIVE_POM_PASS camera_height=.7 pitch_degrees=12 native_diffuse_normal_cache_capture_and_composite");std::puts("OBLIQUE_OCCLUSION_PASS");return 0;
}
