// Hidden standalone D3D9 fixture; never loads the game or installed proxy DLL.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "world_compiled_shaders.h"
#include "local_light_compiled_shaders.h"
#include "static_shadow_scene.h"

static void ok(HRESULT hr){assert(SUCCEEDED(hr));(void)hr;}
struct Legacy {float x,y,z,nx=0,ny=0,nz=0,u=0,v=0;};
using Compact=StaticShadow::Vertex;
static_assert(sizeof(Legacy)==32&&sizeof(Compact)==20,"actual vertex layouts");
static std::vector<unsigned char> pixels(IDirect3DDevice9* d,IDirect3DSurface9* target,unsigned width,unsigned height,unsigned samples){
    IDirect3DSurface9 *resolved=nullptr,*staging=nullptr;IDirect3DSurface9* source=target;
    if(samples){ok(d->CreateRenderTarget(width,height,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));source=resolved;}
    ok(d->CreateOffscreenPlainSurface(width,height,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(source,staging));
    D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)std::memcpy(out.data()+size_t(y)*width*4,static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,width*4);
    ok(staging->UnlockRect());staging->Release();if(resolved)resolved->Release();return out;
}
int main(int argc,char** argv){
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLiveTerrainLayout";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const D3DVERTEXELEMENT9 oldElements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    const D3DVERTEXELEMENT9 newElements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declarations[2]{};ok(d->CreateVertexDeclaration(oldElements,&declarations[0]));ok(d->CreateVertexDeclaration(newElements,&declarations[1]));
    IDirect3DVertexShader9* vertex[3]{};ok(d->CreateVertexShader(kShadowVSShader,&vertex[0]));ok(d->CreateVertexShader(kShadowCacheVSShader,&vertex[1]));ok(d->CreateVertexShader(kLocalShadowVSShader,&vertex[2]));
    IDirect3DPixelShader9* pixel[2]{};ok(d->CreatePixelShader(kShadowPSShader,&pixel[0]));ok(d->CreatePixelShader(kLocalShadowPSShader,&pixel[1]));
    for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));ok(d->SetTexture(0,nullptr));
    const float opaque[]={1,1,1,-1},sphere[]={10000,.00390625f,0,0};ok(d->SetPixelShaderConstantF(0,opaque,1));ok(d->SetPixelShaderConstantF(1,sphere,1));
    unsigned comparisons=0;size_t covered=0;uint64_t digest=1469598103934665603ull;
    for(unsigned samples:{0u,2u,4u,8u}){
        DWORD levels=0;ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_R32F,TRUE,D3DMULTISAMPLE_TYPE(samples),&levels));ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,D3DMULTISAMPLE_TYPE(samples),&levels));
        const unsigned width=256,height=192;IDirect3DSurface9 *target=nullptr,*depth=nullptr;
        ok(d->CreateRenderTarget(width,height,D3DFMT_R32F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,TRUE,&depth,nullptr));
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 viewport={0,0,width,height,0,1};ok(d->SetViewport(&viewport));
        for(unsigned pose=0;pose<8;++pose){
            const float originX=pose&1?5200.f:0.f,originY=pose&1?-33.f:0.f,originZ=pose&1?495.f:0.f;
            std::vector<Legacy> legacy(7);std::vector<Compact> compact(7);std::vector<uint32_t> indices;
            for(unsigned y=0;y<=32;++y)for(unsigned x=0;x<=32;++x){float px=float(x)/16-1,py=float(y)/16-1,pz=.4f+.15f*std::sin(float(x)*.4f)*std::cos(float(y)*.3f);legacy.push_back({originX+px,originY+py,originZ+pz});compact.push_back({originX+px,originY+py,originZ+pz,0,0});}
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){uint32_t a=y*33+x,b=a+1,c=a+33,e=c+1;indices.insert(indices.end(),{a,b,c,b,e,c});}
            IDirect3DVertexBuffer9* vb[2]{};IDirect3DIndexBuffer9* ib=nullptr;ok(d->CreateIndexBuffer(UINT(indices.size()*4),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));void* data=nullptr;ok(ib->Lock(0,0,&data,0));std::memcpy(data,indices.data(),indices.size()*4);ok(ib->Unlock());ok(d->SetIndices(ib));
            for(unsigned layout=0;layout<2;++layout){const size_t bytes=layout?compact.size()*sizeof(Compact):legacy.size()*sizeof(Legacy);ok(d->CreateVertexBuffer(UINT(bytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb[layout],nullptr));ok(vb[layout]->Lock(0,0,&data,D3DLOCK_DISCARD));if(layout)std::memcpy(data,compact.data(),bytes);else std::memcpy(data,legacy.data(),bytes);ok(vb[layout]->Unlock());}
            const float angle=float(pose)*.37f,scale=pose>=6?1.25f:.7f;float matrix[16]={};matrix[0]=std::cos(angle)*scale;matrix[1]=std::sin(angle)*scale;matrix[4]=-std::sin(angle)*scale;matrix[5]=std::cos(angle)*scale;matrix[10]=1;matrix[15]=1;
            matrix[12]=-originX*matrix[0]-originY*matrix[4];matrix[13]=-originX*matrix[1]-originY*matrix[5];matrix[14]=-originZ;
            if(pose==4){matrix[11]=.3f;matrix[15]=1-originZ*.3f;}ok(d->SetVertexShaderConstantF(0,matrix,4));
            ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,pose==5));RECT rect={37,23,219,171};ok(d->SetScissorRect(&rect));
            for(unsigned shader=0;shader<3;++shader){ok(d->SetVertexShader(vertex[shader]));ok(d->SetPixelShader(pixel[shader==2]));std::vector<unsigned char> output[2];
                for(unsigned layout=0;layout<2;++layout){const unsigned stride=layout?sizeof(Compact):sizeof(Legacy);ok(d->SetVertexDeclaration(declarations[layout]));ok(d->SetStreamSource(0,vb[layout],3*stride,stride));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->BeginScene());ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,4,0,33*33,0,UINT(indices.size()/3)));ok(d->EndScene());output[layout]=pixels(d,target,width,height,samples);}
                assert(output[0]==output[1]);size_t changed=0;for(size_t i=0;i<output[0].size();i+=4){float value;std::memcpy(&value,output[0].data()+i,4);assert(std::isfinite(value));changed+=value<.9999f;}assert(changed>1000);covered+=changed;
                for(auto byte:output[0]){digest^=byte;digest*=1099511628211ull;}++comparisons;
            }
            for(auto* buffer:vb)buffer->Release();ib->Release();
        }
        target->Release();depth->Release();
    }
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);
    for(auto* p:vertex)p->Release();for(auto* p:pixel)p->Release();for(auto* p:declarations)p->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("LIVE_TERRAIN_LAYOUT_GPU_PASS comparisons=%u samples=0,2,4,8 shaders=directional,cached,point exact=1 covered=%zu digest=%016llx\n",comparisons,covered,(unsigned long long)digest);
}
