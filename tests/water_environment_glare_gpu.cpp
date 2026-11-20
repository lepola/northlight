// Execute the production panoramic environment shader against changing screen halo inputs.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"

int main(int argc,char** argv){
    assert(argc==2);constexpr unsigned size=128;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWaterGlareTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,size,size,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=size;pp.BackBufferHeight=size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(kWaterSkyGradientShader,&shader));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    auto black=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,0}}),white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});
    for(unsigned s=0;s<5;++s){ok(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}
    struct Vertex{float x,y,z,w,u,v;};Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    float c[101][4]{};
    for(unsigned i=80;i<86;++i)c[i][0]=c[i][1]=c[i][2]=.1f;
    c[88][0]=std::sqrt(.96f);c[88][2]=.2f;c[88][3]=.05f;
    c[89][0]=1.1f;c[89][1]=20;c[89][2]=2.4f;
    c[90][0]=c[90][1]=c[90][2]=1;c[90][3]=1.8f;
    c[91][0]=c[91][1]=c[91][2]=1;c[92][0]=c[92][1]=c[92][2]=1;
    std::vector<float> reference(size*size*4);unsigned failures=0,lit=0,cases=0;
    // Simulate missing, fully hidden and visible main-camera halos, then return.
    for(unsigned mode=0;mode<4;++mode){
        for(unsigned s=0;s<4;++s)ok(d->SetTexture(s,mode==0?nullptr:mode==2?white:black));
        ok(d->SetPixelShaderConstantF(0,c[0],101));ok(d->SetPixelShader(shader));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)for(unsigned k=0;k<4;++k){
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+(x*4+k)*4,4);const unsigned i=(y*size+x)*4+k;
            if(!std::isfinite(got))++failures;
            if(!mode){reference[i]=got;if(k==0&&got>.2f)++lit;}
            else if(got!=reference[i])++failures;
        }
        ok(staging->UnlockRect());++cases;
    }
    assert(lit>10);std::printf("water environment glare GPU cases=%u pixels=%u lit=%u changedChannels=%u\n",cases,cases*size*size,lit,failures);
    d->SetPixelShader(nullptr);for(unsigned s=0;s<5;++s)d->SetTexture(s,nullptr);black->Release();white->Release();shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
