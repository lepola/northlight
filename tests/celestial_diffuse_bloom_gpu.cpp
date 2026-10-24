// Production bytecode replay, hidden D3D9 device. No game process is used.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "celestial_disc_compiled_shaders.h"
#include "celestial_glow.h"

int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned N=128;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightDiffuseBloomTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *oldVeil=nullptr,*hotVeil=nullptr,*veil=nullptr,*sky=nullptr;
    auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&oldVeil));code=read(argv[3]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&hotVeil));
    ok(d->CreatePixelShader(kCelestialVeilPSShader,&veil));ok(d->CreatePixelShader(kCelestialDiscPSShader,&sky));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));D3DVIEWPORT9 vp={0,0,N,N,0,1};ok(d->SetViewport(&vp));
    auto white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});ok(d->SetTexture(1,white));
    for(unsigned s=0;s<6;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto scalar=[&](float value){Image image{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(image.pixels.data(),&value,4);return texture(d,image);};
    const Vertex q[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    auto capture=[&](IDirect3DPixelShader9* ps,float (*c)[4]){
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(ps));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*N*4);for(unsigned y=0;y<N;++y)std::memcpy(out.data()+y*N*4,static_cast<char*>(l.pBits)+y*l.Pitch,N*16);ok(staging->UnlockRect());return out;};
    unsigned cases=0,failures=0,restoredPixels=0,coreRejected=0;double maxError=0,oldSky=0,newSky=0,hotPeak=0,diffusePeak=0;
    const float radius=.04f,sine=radius/std::sqrt(1+radius*radius),elevation=1.5f*sine;
    const float oldWrap=NorthlightCelestialGlow::Wrap*NorthlightCelestialGlow::smoothstep(sine,3*sine,elevation);
    const float wrap=NorthlightCelestialGlow::ringWrap(elevation,sine);
    for(float blockerDepth:{.5f,.999f})for(float disc:{0.f,.25f,1.f})for(float ring:{0.f,.5f,1.f}){
        Image depth{N,N,D3DFMT_R32F,4,std::vector<unsigned char>(N*N*4)};
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){float value=x>=40&&x<88&&y>=24&&y<104?blockerDepth:1;std::memcpy(depth.pixels.data()+(y*N+x)*4,&value,4);}
        auto dep=texture(d,depth),source=scalar(disc),surround=scalar(ring);ok(d->SetTexture(0,dep));ok(d->SetTexture(3,source));ok(d->SetTexture(5,surround));
        const float cs=std::cos(.35f),sn=std::sin(.35f);float c[49][4]{};
        c[0][0]=c[0][1]=1.f/N;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;
        c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=radius;c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][0]=c[9][1]=c[9][2]=c[9][3]=1;c[10][1]=.2f;c[10][2]=1000;c[10][3]=1;
        c[11][0]=.9999219f;c[11][1]=1;c[11][2]=1.5f/16777215;c[11][3]=1;
        c[12][0]=1.1f;c[12][1]=20;c[12][3]=1; // no compact core in either late pass
        c[47][0]=.8f;c[47][1]=.5f;c[47][2]=.2f;c[47][3]=.9f;c[48][0]=c[48][1]=c[48][2]=1;c[48][3]=wrap;
        auto repaired=capture(veil,c);c[48][3]=oldWrap;auto before=capture(oldVeil,c);c[48][3]=wrap;
        auto clearSky=capture(sky,c);c[48][3]=oldWrap;auto dimSky=capture(sky,c);c[48][3]=wrap;
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
            const bool geometry=x>=40&&x<88&&y>=24&&y<104;
            const float vx=(float(x)/N*2-1)/5,vy=(1-float(y)/N*2)/5,r=std::sqrt(vx*vx+vy*vy)/radius;
            const float optical=NorthlightCelestialGlow::sunGlare(r,disc,ring,wrap,.9f,0);
            for(unsigned k=0;k<3;++k){
                const float color[]={.8f,.5f,.2f};const float expected=geometry?1-std::exp(-optical*color[k]):0;
                const float got=repaired[(y*N+x)*4+k];const double error=std::fabs(got-expected);maxError=std::max(maxError,error);
                if(!std::isfinite(got)||error>2e-5){if(failures++<4)std::printf("FAIL bloom xy=%u,%u d=%g r=%g got=%g expected=%g\n",x,y,disc,ring,got,expected);}
            }
            if(geometry&&x>=44&&x<84&&y>=28&&y<100&&repaired[(y*N+x)*4]>1e-4&&before[(y*N+x)*4]==0)++restoredPixels;
            if(geometry){if(clearSky[(y*N+x)*4]!=0)++failures;else ++coreRejected;}
            if(!geometry&&disc==0&&ring==1){oldSky+=dimSky[(y*N+x)*4];newSky+=clearSky[(y*N+x)*4];}
        }
        if(disc==1&&ring==1){c[12][2]=2.4f;auto hot=capture(hotVeil,c);c[12][2]=0;
            for(unsigned y=60;y<68;++y)for(unsigned x=60;x<68;++x){hotPeak=std::max(hotPeak,double(hot[(y*N+x)*4]));diffusePeak=std::max(diffusePeak,double(repaired[(y*N+x)*4]));}
        }
        dep->Release();source->Release();surround->Release();++cases;
    }
    if(!(restoredPixels>10000&&newSky>oldSky*4&&hotPeak>diffusePeak+.3))++failures;
    std::printf("diffuse bloom GPU cases=%u pixels=%u restoredAcrossSilhouette=%u coreBlockedPixels=%u lowSunSkyGain=%.4f hotPeak=%.4f diffusePeak=%.4f maxError=%.9g failures=%u\n",cases,cases*N*N,restoredPixels,coreRejected,newSky/oldSky,hotPeak,diffusePeak,maxError,failures);
    for(unsigned s=0;s<6;++s)d->SetTexture(s,nullptr);d->SetPixelShader(nullptr);white->Release();oldVeil->Release();hotVeil->Release();veil->Release();sky->Release();target->Release();staging->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
