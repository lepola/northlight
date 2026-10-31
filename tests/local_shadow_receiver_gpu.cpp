// Real caster rasterization and production receiver comparison; no game assets.
#define main unused_composite_oracle
#include "composite_distance_gpu.cpp"
#undef main
#include "point_light_shadow.h"
#include <array>
using Vec=std::array<float,3>;
static Vec add(Vec a,Vec b,float scale=1){for(unsigned k=0;k<3;++k)a[k]+=scale*b[k];return a;}
static Vec rotate(Vec p,unsigned face){switch(face){case 0:return p;case 1:return {p[0],-p[1],-p[2]};case 2:return {p[2],p[0],p[1]};case 3:return {-p[2],p[0],-p[1]};case 4:return {p[1],p[2],p[0]};default:return {p[1],-p[2],-p[0]};}}
struct Caster{float p[3],uv[2];};
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];constexpr unsigned size=256;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightShadowReceiver";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *receiver=nullptr,*casterPS=nullptr;IDirect3DVertexShader9* casterVS=nullptr;
    auto bytes=read(folder+"Receiver.bin");ok(d->CreatePixelShader((const DWORD*)bytes.data(),&receiver));bytes=read(folder+"LocalShadowVS.bin");ok(d->CreateVertexShader((const DWORD*)bytes.data(),&casterVS));bytes=read(folder+"LocalShadowPS.bin");ok(d->CreatePixelShader((const DWORD*)bytes.data(),&casterPS));
    IDirect3DSurface9 *target=nullptr,*stage=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    unsigned cases=0,failures=0;uint64_t clearChecks=0,blockedChecks=0;float worstClear=1,worstBlocked=0;
    for(unsigned resolution:{128u,256u,512u}){
        IDirect3DCubeTexture9* cube=nullptr;IDirect3DSurface9* depth=nullptr;ok(d->CreateCubeTexture(resolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&cube,nullptr));ok(d->CreateDepthStencilSurface(resolution,resolution,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
        for(float height:{1.f,5.f,12.f})for(float slope:{0.f,.3f,1.f})for(unsigned orientation=0;orientation<6;++orientation)for(bool translated:{false,true})for(bool blocked:{false,true}){
            const float norm=std::sqrt(1+slope*slope),nearZ=.1f,farZ=64;
            const Vec u=rotate({1/norm,0,slope/norm},orientation),v=rotate({0,1,0},orientation),n=rotate({-slope/norm,0,1/norm},orientation);
            const Vec origin=translated?Vec{5313.125f,2217.375f,731.25f}:Vec{};const auto lamp=add(origin,n,height);
            auto square=[&](Vec center,float su,float sv){std::array<Caster,4> result{};for(unsigned i=0;i<4;++i){const auto p=add(add(center,u,(i&1)?su:-su),v,(i&2)?sv:-sv);std::memcpy(result[i].p,p.data(),12);result[i].uv[0]=float(i&1);result[i].uv[1]=float((i>>1)&1);}return result;};
            const auto plane=square(origin,150,150),blocker=square(add(origin,n,height*.5f),1.25f,1.75f);
            ok(d->SetTexture(1,nullptr));
            for(unsigned face=0;face<6;++face){
                IDirect3DSurface9* surface=nullptr;ok(cube->GetCubeMapSurface(D3DCUBEMAP_FACES(face),0,&surface));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,surface));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,resolution,resolution,0,1};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));
                ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
                ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1));ok(d->SetVertexShader(casterVS));ok(d->SetPixelShader(casterPS));float matrix[16];assert(NorthlightPointShadow::faceMatrix(lamp.data(),nearZ,farZ,face,matrix,resolution));ok(d->SetVertexShaderConstantF(0,matrix,4));const float material[]={0,0,0,-1},sphere[]={farZ*farZ,1.f/resolution,0,0};ok(d->SetPixelShaderConstantF(0,material,1));ok(d->SetPixelShaderConstantF(1,sphere,1));
                ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane.data(),sizeof(Caster)));if(blocked)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,blocker.data(),sizeof(Caster)));ok(d->EndScene());surface->Release();
            }
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetPixelShader(receiver));ok(d->SetTexture(1,cube));
            for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(1,key,D3DTEXF_POINT));ok(d->SetSamplerState(1,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(1,key,D3DTADDRESS_CLAMP));
            float c[84][4]{};c[2][3]=.025f;std::memcpy(c[7],lamp.data(),12);c[9][0]=nearZ;c[9][1]=farZ;c[9][2]=1.f/resolution;c[9][3]=1;std::memcpy(c[80],origin.data(),12);for(unsigned k=0;k<3;++k){c[81][k]=40*u[k];c[82][k]=40*v[k];c[83][k]=n[k];}ok(d->SetPixelShaderConstantF(0,c[0],84));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned clear=0,shadow=0;
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
                const float* f=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*16);const float pu=(float(x)+.5f)/size*40-20,pv=(float(y)+.5f)/size*40-20;
                const bool isClear=!blocked||std::fabs(pu)>3.5f||std::fabs(pv)>4.5f;const bool isBlocked=blocked&&std::fabs(pu)<1.25f&&std::fabs(pv)<1.75f;
                bool bad=!std::isfinite(f[0])||f[0]<0||f[0]>1;
                if(isClear){++clear;++clearChecks;worstClear=std::min(worstClear,f[0]);bad|=f[0]<.999f;}
                if(isBlocked){++shadow;++blockedChecks;worstBlocked=std::max(worstBlocked,f[0]);bad|=f[0]>.001f;}
                if(bad&&failures++<5)std::printf("FAIL resolution=%u height=%.1f slope=%.1f orientation=%u translated=%d blocked=%d xy=%u,%u visibility=%.6f expected=%s\n",resolution,height,slope,orientation,int(translated),int(blocked),x,y,f[0],isClear?"lit":isBlocked?"shadow":"finite");
            }
            ok(stage->UnlockRect());assert(clear>10000&&(!blocked||shadow>100));++cases;
        }
        ok(d->SetTexture(1,nullptr));cube->Release();depth->Release();
    }
    std::printf("LOCAL_SHADOW_RECEIVER cases=%u litPixels=%llu blockedPixels=%llu minLit=%.6f maxBlocked=%.6f failures=%u\n",cases,(unsigned long long)clearChecks,(unsigned long long)blockedChecks,worstClear,worstBlocked,failures);
    receiver->Release();casterPS->Release();casterVS->Release();stage->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return failures?1:0;
}
