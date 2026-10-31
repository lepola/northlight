// Hidden device oracle: real production cube rasterization and local-light bytecode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_unused_composite_oracle
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "point_light_shadow.h"
#include "point_transmission_filter.h"
#include <array>

static void transmissionCoverage(IDirect3DDevice9* d,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps){
    constexpr unsigned size=128,texels=8;
    Image mask{texels,texels,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(texels*texels*4,255)};
    for(unsigned i=0;i<64;++i)mask.pixels[i*4+3]=(i*13%64)<20?255:0;
    auto* alpha=texture(d,mask);
    IDirect3DSurface9 *target=nullptr,*staging=nullptr,*previous=nullptr;
    ok(d->GetRenderTarget(0,&previous));ok(d->SetDepthStencilSurface(nullptr));
    ok(d->CreateRenderTarget(size,size,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetTexture(0,alpha));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,D3DTEXF_LINEAR));
    ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(0,state,D3DTADDRESS_WRAP));
    ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,-1.f/size,1.f/size,0,1};
    const float material[]={1,1,1,.5f},sphere[]={100,0,0,0};
    ok(d->SetVertexShaderConstantF(0,matrix,4));ok(d->SetPixelShaderConstantF(0,material,1));ok(d->SetPixelShaderConstantF(1,sphere,1));
    unsigned checks=0,negativeErrors=0;
    struct Caster{float x,y,z,u,v;};
    for(float phase:{.03125f,.078125f,.109375f})for(bool pointOnly:{false,true}){
        const Caster quad[]={{-1,1,.5f,phase,phase},{1,1,.5f,1+phase,phase},{-1,-1,.5f,phase,1+phase},{1,-1,.5f,1+phase,1+phase}};
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
        {NorthlightPointTransmissionFilter filter;ok(filter.begin(d,pointOnly));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Caster)));ok(d->EndScene());ok(filter.restore());}
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER}){DWORD value=0;ok(d->GetSamplerState(0,state,&value));assert(value==D3DTEXF_LINEAR);}
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        unsigned covered=0;
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            const unsigned u=unsigned(std::floor(((x+.5f)/size+phase)*texels))%texels;
            const unsigned v=unsigned(std::floor(((y+.5f)/size+phase)*texels))%texels;
            const bool expected=mask.pixels[(v*texels+u)*4+3]!=0;
            float depth;std::memcpy(&depth,static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*4,4);
            assert(std::isfinite(depth));const bool actual=depth<.75f;covered+=actual;
            if(pointOnly){assert(actual==expected);++checks;}else negativeErrors+=actual!=expected;
        }
        ok(staging->UnlockRect());if(pointOnly)assert(covered==size*size*20/64);
    }
    assert(negativeErrors); // Linear thresholding is a deliberately wrong coverage control.
    std::printf("transmission GPU checkedPixels=%u negativeLinearErrors=%u exactCoverage=20/64 samplerRestored=1\n",checks,negativeErrors);
    ok(d->SetTexture(0,nullptr));ok(d->SetRenderTarget(0,previous));previous->Release();staging->Release();target->Release();alpha->Release();
}

int main(int argc,char** argv){
    assert(argc==3);const std::string dir=argv[1];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightTransmissionOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexShader9* vs=nullptr;
    auto code=read((dir+"/LocalShadowPS.bin").c_str());ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));
    code=read((dir+"/LocalShadowVS.bin").c_str());ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vs));
    transmissionCoverage(d,vs,ps);
    ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));vs->Release();ps->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::puts("POINT_TRANSMISSION_GPU_PASS");
}
