// Offscreen real-device oracle: original client VS/PS, production capture and LocalDirect.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");std::fflush(stdout);va_end(args);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
#include "forwarders.h"
struct MaterialCallMeter:ForwardIDirect3DDevice9 {
    unsigned samplerGets=0,samplerSets=0;
    explicit MaterialCallMeter(IDirect3DDevice9* device):ForwardIDirect3DDevice9(device){}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD* v) override{++samplerGets;return real->GetSamplerState(n,k,v);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD v) override{++samplerSets;return real->SetSamplerState(n,k,v);}
};
static constexpr unsigned W=256,H=256;
static Image solid(float x,float y,float z,float a){Image i{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)};float v[]={x,y,z,a};for(unsigned at=0;at<W*H;++at)std::memcpy(i.pixels.data()+at*16,v,16);return i;}
static IDirect3DTexture9* mipTexture(IDirect3DDevice9* d,const std::string& path){
    auto base=image(path);IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(base.width,base.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr));
    for(unsigned level=0;level<t->GetLevelCount();++level){auto im=image(path+"."+std::to_string(level));D3DLOCKED_RECT lock{};ok(t->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);ok(t->UnlockRect(level));}return t;
}
static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* source){
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(source,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(out.data()+y*W*4,(char*)lock.pBits+y*lock.Pitch,W*16);staging->UnlockRect();staging->Release();return out;
}
static std::array<unsigned,3> occludedPixels(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    assert(texture);IDirect3DSurface9 *source=nullptr,*staging=nullptr;ok(texture->GetSurfaceLevel(0,&source));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(source,staging));
    D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::array<unsigned,3> count{};
    unsigned covered=0;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const auto* p=(unsigned char*)lock.pBits+y*lock.Pitch+x*4;if(p[3]>5){++covered;for(unsigned i=0;i<3;++i)count[i]+=unsigned(p[2-i])+2<unsigned(p[3]);}}
    std::printf("OCCLUSION covered=%u\n",covered);
    staging->UnlockRect();drop(staging);drop(source);return count;
}
struct GenerationMeter:MaterialCallMeter {
    unsigned generations=0;
    explicit GenerationMeter(IDirect3DDevice9* device):MaterialCallMeter(device){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD usage,D3DFORMAT f,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared)override{
        if(usage&D3DUSAGE_RENDERTARGET)++generations;
        return real->CreateTexture(w,h,l,usage,f,pool,out,shared);
    }
};
int main(int argc,char** argv){
    assert(argc==4);const std::string folder=argv[2];const bool baseline=std::strcmp(argv[3],"baseline")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialPerformance";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));GenerationMeter meter(raw);IDirect3DDevice9* d=&meter;
    IDirect3DSurface9 *targets[2]={};for(auto*& t:targets)ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&t,nullptr));
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,targets[0]);d->SetRenderTarget(1,targets[1]);D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetViewport(&vp);
    for(auto k:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(k,FALSE);
    for(auto k:{D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1})d->SetRenderState(k,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    auto* source=mipTexture(d,folder+"source.fgr");NorthlightAutoNormalCache cache(d);auto* normal=cache.resolve(source);assert(normal);
    for(unsigned n=0;n<4;++n){d->SetTexture(n,n<2?source:normal);for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})d->SetSamplerState(n,k,D3DTEXF_LINEAR);d->SetSamplerState(n,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(n,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);}
    struct PV{float x,y,z,w,u,v,px,py,pz,pw;};
    PV quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX2|D3DFVF_TEXCOORDSIZE4(1));
    float c[26][4]={};c[0][0]=.5f;c[20][1]=48;c[20][2]=.2f;c[21][0]=c[21][1]=1;c[21][2]=-1;c[22][0]=.018f;c[22][1]=16;c[22][2]=16;c[23][0]=.7f;c[23][1]=8;c[23][2]=.35f;c[23][3]=1;c[24][0]=.98f;c[24][2]=.2f;c[24][3]=1;
    unsigned cases=0;
    for(const char* entry:{"BumpOpaque","BumpLayer","BumpWall","BumpGrass","BumpWallOpaque","BumpComposite"}){
        auto code=read(folder+entry+"Fixture.bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader((DWORD*)code.data(),&ps));d->SetPixelShader(ps);
        for(unsigned mode=0;mode<9;++mode){
            c[25][0]=mode==0?1:mode==1?15.99f:mode==2?16:mode==3?16.01f:mode==4?47.99f:mode==5?48:mode==6?48.01f:1;
            c[25][1]=mode==5?.1f:0;c[25][2]=mode==7?16:1;c[25][3]=mode==8?0:1;for(unsigned i=0;i<4;++i){float u=i&1?1.f:0.f,v=i&2?1.f:0.f;quad[i].u=u*c[25][2];quad[i].v=v*c[25][2];quad[i].px=u*2-1;quad[i].py=v*2-1;quad[i].pz=0;quad[i].pw=c[25][0]+(u-.5f)*c[25][1];}d->SetPixelShaderConstantF(0,c[0],26);
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(PV)));ok(d->EndScene());
            for(unsigned target=0;target<2;++target){auto out=pixels(d,targets[target]);for(auto v:out)assert(std::isfinite(v));std::ofstream file(folder+entry+"-"+std::to_string(mode)+"-"+std::to_string(target)+".f32",std::ios::binary);file.write((char*)out.data(),out.size()*4);}
            ++cases;
        }drop(ps);
    }
    d->SetRenderTarget(1,nullptr);for(unsigned n=0;n<4;++n)d->SetTexture(n,nullptr);cache.reset();drop(source);
    // Revisit a fixed working set: unchanged cache budget, real compressed sources.
    std::vector<IDirect3DTexture9*> sources;
    for(unsigned i=0;i<20;++i){IDirect3DTexture9* tex=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_DXT1,D3DPOOL_MANAGED,&tex,nullptr));D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<128;++y)std::memset((char*)lock.pBits+y*lock.Pitch,0,1024);tex->UnlockRect(0);sources.push_back(tex);}
    const auto before=meter.generations;
    for(auto* source:sources){assert(cache.resolve(source));cache.endFrame();}
    const auto first=meter.generations-before;
    for(auto* source:sources){assert(cache.resolve(source));cache.endFrame();}
    const auto second=meter.generations-before-first;assert(first==20);assert(second==(baseline?20:0));
    auto vsBytes=read(folder+"shaders_vertex_vs_3_0_terrain.bls-0.bin"),psBytes=read(folder+"shaders_pixel_ps_3_0_terrain1.bls-0.bin");
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreatePixelShader((DWORD*)psBytes.data(),&ps));d->SetVertexShader(vs);d->SetPixelShader(ps);
    NorthlightAutoParallax parallax(d,cache);unsigned callbacks=0,gateReads=0;
    for(unsigned gate=0;gate<2;++gate){
        d->SetRenderState(D3DRS_ZENABLE,gate?TRUE:FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetTexture(0,nullptr);
        const auto before=meter.samplerGets;ok(parallax.draw(vs,vs,1,c[21],c[22],[&]{++callbacks;return D3D_OK;}));gateReads+=meter.samplerGets-before;
    }
    assert(callbacks==2&&gateReads==(baseline?16u:0u));drop(vs);drop(ps);
    std::printf("MATERIAL_PERF_GPU_PASS cases=%u initialGenerations=%u revisitGenerations=%u rejectedSamplerReads=%u baseline=%d\n",cases+2,first,second,gateReads,int(baseline));
    cache.reset();for(auto*& source:sources)drop(source);for(auto*& t:targets)drop(t);d->Release();api->Release();DestroyWindow(window);
}
