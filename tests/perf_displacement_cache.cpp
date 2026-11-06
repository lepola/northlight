// Real-device regression: the overlay must use the current successful base geometry.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "quality_settings.h"
static unsigned availableMiB=1024;
static BOOL cacheMemory(LPMEMORYSTATUSEX p){p->ullAvailVirtual=uint64_t(availableMiB)<<20;return TRUE;}
#define GlobalMemoryStatusEx cacheMemory
#include "auto_displacement_renderer.h"
#undef GlobalMemoryStatusEx
#include "forwarders.h"
struct BuildMeter:ForwardIDirect3DDevice9 {unsigned builds=0,narrow=0,wide=0;size_t indexBytes=0,fullWidthBytes=0;explicit BuildMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT n,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** v,HANDLE* shared)override{++builds;return real->CreateVertexBuffer(n,usage,fvf,pool,v,shared);}
HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT n,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** v,HANDLE* shared)override{
    const HRESULT hr=real->CreateIndexBuffer(n,usage,format,pool,v,shared);if(SUCCEEDED(hr)){D3DINDEXBUFFER_DESC desc{};ok((*v)->GetDesc(&desc));assert(desc.Format==format&&desc.Size==n);indexBytes+=n;fullWidthBytes+=n*(format==D3DFMT_INDEX16?2:1);if(format==D3DFMT_INDEX16)++narrow;else{assert(format==D3DFMT_INDEX32);++wide;}}return hr;}};
static constexpr unsigned W=256,H=160;
static IDirect3DTexture9* sourceTexture(IDirect3DDevice9* d,const std::string& path){
    auto first=image(path);IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(first.width,first.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr));
    for(unsigned i=0;i<t->GetLevelCount();++i){auto im=image(path+"."+std::to_string(i));D3DLOCKED_RECT lock{};ok(t->LockRect(i,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);ok(t->UnlockRect(i));}return t;
}
static std::vector<float> depthPixels(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::string& path){
    IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> values(W*H);
    for(unsigned y=0;y<H;++y)std::memcpy(values.data()+y*W,(char*)lock.pBits+y*lock.Pitch,W*4);stage->UnlockRect();drop(stage);
    std::ofstream file(path,std::ios::binary);const unsigned header[]={0x31524746,W,H,D3DFMT_R32F,4};file.write((char*)header,sizeof header);file.write((char*)values.data(),values.size()*4);return values;
}
static float difference(const std::vector<float>& a,const std::vector<float>& b){assert(a.size()==b.size());float peak=0;for(unsigned i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));peak=std::max(peak,std::fabs(a[i]-b[i]));}return peak;}
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightDisplacementCache";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DDevice9* rawDevice=d;BuildMeter meter(d);d=&meter;
    IDirect3DSurface9 *color=nullptr,*depth=nullptr,*zbuffer=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&color,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&zbuffer,nullptr));
    ok(d->SetRenderTarget(0,color));ok(d->SetRenderTarget(1,depth));ok(d->SetDepthStencilSurface(zbuffer));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})d->SetRenderState(key,TRUE);for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE})d->SetRenderState(key,FALSE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
    auto vsBytes=read(folder+"terrain-vs.bin"),beautyBytes=read(folder+"PreviewBase.bin");std::vector<uint32_t> forwarded;assert(!NorthlightWaterShaderPatch::patch((uint32_t*)vsBytes.data(),vsBytes.size()/4,forwarded));
    IDirect3DVertexShader9 *vs=nullptr,*forwardVS=nullptr;IDirect3DPixelShader9* beauty=nullptr;ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreateVertexShader((DWORD*)forwarded.data(),&forwardVS));ok(d->CreatePixelShader((DWORD*)beautyBytes.data(),&beauty));ok(d->SetPixelShader(beauty));
    auto* textured=sourceTexture(d,folder+"source.fgr");auto* white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});
    for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})d->SetSamplerState(0,key,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    const float s=std::sin(.20943951f),c=std::cos(.20943951f),xs=1.7320508f*H/W,ys=1.7320508f;float bank[79][4]={};bank[0][0]=1;bank[1][1]=s;bank[1][2]=-c;bank[2][1]=c;bank[2][2]=s;bank[3][1]=-.7f*c+4*s;bank[3][2]=-.7f*s-4*c;bank[3][3]=1;bank[4][0]=xs;bank[5][1]=ys;bank[6][2]=100.f/(.1f-100);bank[6][3]=-1;bank[7][2]=.1f*100/(.1f-100);bank[18][0]=bank[18][1]=.25f;bank[13][0]=bank[13][1]=.5f;bank[78][0]=bank[78][1]=bank[78][2]=1;
    struct V{float x,y,z,nx,ny,nz,u,v;DWORD color,blend;};const V floor[]={{-4,-3.4f,0,0,0,1,-1,-.85f,0xffffffff,0xffffffff},{4,-3.4f,0,0,0,1,1,-.85f,0xffffffff,0xffffffff},{-4,4.6f,0,0,0,1,-1,1.15f,0xffffffff,0xffffffff},{4,4.6f,0,0,0,1,1,1.15f,0xffffffff,0xffffffff}};
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},{0,32,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,36,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,1},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));
    NorthlightAutoDisplacement geometry(d);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpTerrain=3;settings.autoDisplacementStrength=100;settings.autoDisplacementDetail=4;geometry.configure(settings);geometry.registerMaterial(vs,(DWORD*)vsBytes.data(),vsBytes.size()/4,1);geometry.registerVariant(forwardVS,(DWORD*)forwarded.data(),forwarded.size());

    std::vector<V> grid;std::vector<unsigned short> indices;
    for(unsigned y=0;y<=16;++y)for(unsigned x=0;x<=16;++x)grid.push_back({x*.5f-4,y*.5f-4,0,0,0,1,x*.125f,y*.125f,0xffffffff,0xffffffff});
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){unsigned short a=static_cast<unsigned short>(y*17+x);indices.insert(indices.end(),{a,static_cast<unsigned short>(a+1),static_cast<unsigned short>(a+17),static_cast<unsigned short>(a+17),static_cast<unsigned short>(a+1),static_cast<unsigned short>(a+18)});}
    auto bind=[&]{ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetTexture(0,textured));ok(d->SetVertexShaderConstantF(0,bank[0],34));};
    unsigned made[3]={};double elapsed[3]={};LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    for(unsigned round=0;round<3;++round){
        if(round==2){availableMiB=715;geometry.setMemoryPressure(true);for(unsigned i=0;i<60;++i)geometry.endFrame();}
        auto before=meter.builds;LARGE_INTEGER start,finish;QueryPerformanceCounter(&start);
        for(unsigned n=0;n<100;++n){grid[0].z=n*.0001f;bind();geometry.beginDraw();
            NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,UINT(grid.size()),0,512,true},indices.data(),D3DFMT_INDEX16,grid.data(),sizeof(V)};
            assert(geometry.prepare(vs,1,1,args));geometry.endFrame();}
        QueryPerformanceCounter(&finish);elapsed[round]=double(finish.QuadPart-start.QuadPart)*1000/frequency.QuadPart;made[round]=meter.builds-before;
    }
    assert(made[0]==100);
#ifdef PERF_BASELINE
    assert(made[1]==100);
#else
    assert(made[1]==0);
#endif
    assert(made[2]>0);
#ifndef PERF_BASELINE
    assert(meter.narrow==meter.builds&&meter.wide==0&&meter.indexBytes*2==meter.fullWidthBytes);
#endif
    std::printf("DISPLACEMENT_INDEX_PASS buffers16=%u buffers32=%u indexBytes=%zu fullWidthBytes=%zu\n",meter.narrow,meter.wide,meter.indexBytes,meter.fullWidthBytes);
    std::printf("DISPLACEMENT_CACHE_PASS first=%u revisit=%u pressureRebuild=%u firstMs=%.3f revisitMs=%.3f pressureMs=%.3f\n",made[0],made[1],made[2],elapsed[0],elapsed[1],elapsed[2]);
    geometry.reset();drop(decl);drop(textured);drop(white);drop(vs);drop(forwardVS);drop(beauty);drop(color);drop(depth);drop(zbuffer);rawDevice->Release();api->Release();DestroyWindow(window);return 0;
}
