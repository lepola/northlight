#define main northlight_unused_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "sky_composition_renderer.h"

static float halfFloat(unsigned h){
    const unsigned exponent=(h>>10)&31,mantissa=h&1023;
    const float value=exponent?std::ldexp(1.f+mantissa/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
    return h&32768?-value:value;
}
int main(int argc,char** argv){
    assert(argc==2);constexpr unsigned N=32;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSkyLayerCapture";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const DWORD code[]={0xffff0200,0x0200001f,0x80000000,0x900f0000,0x03000005,0x800f0000,0x90e40000,0xa0e40000,0x02000001,0x800f0800,0x80e40000,0xffff};
    IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(code,&shader));
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    using NorthlightSkyComposition::Role;
    unsigned cases=0,failures=0,msaaCases=0;
    auto compare=[](unsigned fn,unsigned alpha,unsigned ref){switch(fn){case 1:return false;case 2:return alpha<ref;case 3:return alpha==ref;case 4:return alpha<=ref;case 5:return alpha>ref;case 6:return alpha!=ref;case 7:return alpha>=ref;default:return true;}};
    for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_4_SAMPLES}){
        DWORD quality=0;if(FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A16B16G16R16F,TRUE,samples,&quality)))continue;
        IDirect3DSurface9 *target=nullptr,*depth=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A8R8G8B8,samples,0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(N,N,D3DFMT_D24S8,samples,0,TRUE,&depth,nullptr));
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));
        NorthlightSkyComposition::Capture layers;
        struct Vertex{float x,y,z,w;DWORD color;};
        for(unsigned fn=1;fn<=8;++fn)for(unsigned ref:{0u,96u,128u,255u})for(bool background:{false,true})for(bool outsideUnorm:{false,true}){
            const unsigned alpha=96;layers.begin(cases);
            // FP16 capture must clamp native UNORM shader output before blending.
            const float multiplier[]={outsideUnorm?3.f:1.f,outsideUnorm?-1.f:1.f,outsideUnorm?10.f:1.f,1.f};
            ok(d->SetPixelShaderConstantF(0,multiplier,1));
            const D3DVIEWPORT9 viewport={0,0,N,N,.999f,1};ok(d->SetViewport(&viewport));
            for(auto state:{D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
            ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
            ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,fn));ok(d->SetRenderState(D3DRS_ALPHAREF,ref));
            ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetPixelShader(shader));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_ARGB(255,51,77,102),1,0));ok(d->BeginScene());assert(layers.prepare(d));
            const Vertex vertices[]={{-.5f,-.5f,.5f,1,D3DCOLOR_ARGB(alpha,128,64,32)},{N-.5f,-.5f,.5f,1,D3DCOLOR_ARGB(alpha,128,64,32)},{-.5f,N-.5f,.5f,1,D3DCOLOR_ARGB(alpha,128,64,32)},{N-.5f,N-.5f,.5f,1,D3DCOLOR_ARGB(alpha,128,64,32)}};
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));};
            if(!layers.draw(d,background?Role::Background:Role::Foreground,draw)){std::fprintf(stderr,"CAPTURE FAILURE msaa=%u fn=%u ref=%u background=%u\n",unsigned(samples),fn,ref,background);return 2;}ok(draw());ok(d->EndScene());
            // Both capture stages restore native state, including alpha test,
            // depth writes, shader and the narrow sky viewport.
            DWORD state=0;ok(d->GetRenderState(D3DRS_ALPHAFUNC,&state));assert(state==fn);ok(d->GetRenderState(D3DRS_ALPHATESTENABLE,&state));assert(state==TRUE);
            ok(d->GetRenderState(D3DRS_ZWRITEENABLE,&state));assert(state==TRUE);D3DVIEWPORT9 restored{};ok(d->GetViewport(&restored));assert(restored.MinZ==viewport.MinZ);
            IDirect3DPixelShader9* restoredShader=nullptr;ok(d->GetPixelShader(&restoredShader));assert(restoredShader==shader);restoredShader->Release();
            auto* mask=layers.texture(cases);assert(mask);IDirect3DSurface9* surface=nullptr;ok(mask->GetSurfaceLevel(0,&surface));ok(d->GetRenderTargetData(surface,staging));surface->Release();
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));const auto* pixel=reinterpret_cast<const unsigned short*>(static_cast<const char*>(lock.pBits)+(N/2)*lock.Pitch)+(N/2)*4;
            const bool passes=compare(fn,alpha,ref);const float expectedAlpha=passes&&!background?0.f:1.f;
            const float expectedRed=passes?(background?(outsideUnorm?1.f:128.f/255):0.f):51.f/255;
            const float expectedGreen=passes?(background&&!outsideUnorm?64.f/255:0.f):77.f/255;
            const float expectedBlue=passes?(background?(outsideUnorm?1.f:32.f/255):0.f):102.f/255;
            if(std::fabs(halfFloat(pixel[1])-expectedGreen)>.001f||std::fabs(halfFloat(pixel[2])-expectedBlue)>.001f||std::fabs(halfFloat(pixel[3])-expectedAlpha)>.001f||std::fabs(halfFloat(pixel[0])-expectedRed)>.001f){++failures;std::printf("FAIL capture msaa=%u function=%u ref=%u background=%u rgb=%g alpha=%g\n",unsigned(samples),fn,ref,background,halfFloat(pixel[0]),halfFloat(pixel[3]));}
            ok(staging->UnlockRect());++cases;if(samples)++msaaCases;
        }
        for(unsigned mode=0;mode<3;++mode){
            // Opaque fixed-function backgrounds ignore alpha, including alpha
            // written by a later stage. TFACTOR still owns the native colour.
            layers.begin(cases);const DWORD factor=0x00604020;
            ok(d->SetPixelShader(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            for(auto state:{D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ZENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
            ok(d->SetRenderState(D3DRS_TEXTUREFACTOR,factor));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
            ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TFACTOR));
            ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR));
            ok(d->SetTextureStageState(1,D3DTSS_COLOROP,mode?D3DTOP_SELECTARG1:D3DTOP_DISABLE));ok(d->SetTextureStageState(1,D3DTSS_COLORARG1,D3DTA_CURRENT|(mode==2?D3DTA_ALPHAREPLICATE:0)));
            ok(d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(1,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            ok(d->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
            const Vertex vertices[]={{-.5f,-.5f,.5f,1,0x60ffffff},{N-.5f,-.5f,.5f,1,0x60ffffff},{-.5f,N-.5f,.5f,1,0x60ffffff},{N-.5f,N-.5f,.5f,1,0x60ffffff}};
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));};
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());assert(layers.prepare(d));assert(layers.draw(d,Role::Background,draw));ok(draw());ok(d->EndScene());
            DWORD restored=0;ok(d->GetRenderState(D3DRS_TEXTUREFACTOR,&restored));assert(restored==factor);
            ok(d->GetTextureStageState(1,D3DTSS_ALPHAARG1,&restored));assert(restored==D3DTA_DIFFUSE);
            auto* mask=layers.texture(cases);assert(mask);IDirect3DSurface9* surface=nullptr;ok(mask->GetSurfaceLevel(0,&surface));ok(d->GetRenderTargetData(surface,staging));surface->Release();
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));const auto* pixel=reinterpret_cast<const unsigned short*>(static_cast<const char*>(lock.pBits)+(N/2)*lock.Pitch)+(N/2)*4;
            const float expected=mode==2?0.f:1.f;
            if(std::fabs(halfFloat(pixel[0])-expected*96.f/255)>.001f||std::fabs(halfFloat(pixel[1])-expected*64.f/255)>.001f||std::fabs(halfFloat(pixel[2])-expected*32.f/255)>.001f||std::fabs(halfFloat(pixel[3])-1.f)>.001f){
                ++failures;std::printf("FAIL fixed-function msaa=%u mode=%u rgb=%g,%g,%g alpha=%g\n",unsigned(samples),mode,halfFloat(pixel[0]),halfFloat(pixel[1]),halfFloat(pixel[2]),halfFloat(pixel[3]));}
            ok(staging->UnlockRect());++cases;if(samples)++msaaCases;
        }
        layers.reset();d->SetDepthStencilSurface(nullptr);target->Release();depth->Release();
    }
    std::printf("sky layer capture GPU cases=%u msaa=%u failures=%u\n",cases,msaaCases,failures);
    assert(cases>=64);staging->Release();shader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
