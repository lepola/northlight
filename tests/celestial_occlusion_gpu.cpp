// Hidden D3D9 device; compare the installed and repaired production shaders.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "celestial_disc.h"
#include "celestial_disc_compiled_shaders.h"

int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned size=64;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCelestialHorizonTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *old=nullptr,*fixed=nullptr,*halo=nullptr;auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&old));
    ok(d->CreatePixelShader(kCelestialDiscPSShader,&fixed));ok(d->CreatePixelShader(kCelestialHaloVisibilityPSShader,&halo));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    Image depth{size,size,D3DFMT_R32F,4,std::vector<unsigned char>(size*size*4)};
    const float bands[]={.5f,.9985f,1.f,.940001f};
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)std::memcpy(depth.pixels.data()+(y*size+x)*4,&bands[x/16],4);
    auto dep=texture(d,depth),white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});
    ok(d->SetTexture(0,dep));ok(d->SetTexture(1,white));
    for(unsigned s=0;s<6;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    unsigned cases=0,pixels=0,failures=0,oldHorizonLeaks=0,oldMountainLeaks=0;
    NorthlightCelestialDisc::SkyBand band;assert(band.observe(.9990234375f,1,.94f));
    for(float degrees:{-12.f,-6.f,0.f,6.f,20.f})for(float horizon:{0.f})for(unsigned mode=0;mode<2;++mode){
        const float pitch=degrees*.01745329252f,cs=std::cos(pitch),sn=std::sin(pitch);
        float c[49][4]{};c[0][0]=c[0][1]=1.f/size;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;
        c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=std::tan(.2f);c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][0]=c[9][1]=c[9][2]=c[9][3]=1;c[10][1]=.2f;c[10][2]=1000;c[10][3]=1;
        c[11][0]=mode?band.depth(.94f):.9952f;c[11][1]=1;c[11][2]=NorthlightCelestialDisc::RepairDepthTolerance;c[12][0]=1;c[46][2]=0;
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(mode?fixed:old));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            // discRay removes the D3D9 half-texel offset from interpolated UV.
            const float vx=(float(x)/size*2-1)/5,vy=(1-float(y)/size*2)/5;
            const float z=vy*cs+sn,length=std::sqrt(vx*vx+vy*vy+1);
            const bool above=z>=horizon*length,geometry=mode?bands[x/16]<1.f-1.f/16777216:bands[x/16]<c[11][0]-c[11][2];
            const bool inDisc=std::max(std::fabs(vx),std::fabs(vy))<=c[6][3];
            const float nativeFade=std::clamp(30*z,0.f,1.f);
            const bool expected=inDisc&&!geometry&&nativeFade>.001f;
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);
            if(!std::isfinite(got)||std::fabs(got-float(expected))>1e-5){if(failures++<5)std::printf("FAIL mode=%u elevation=%g horizon=%g xy=%u,%u got=%g expected=%d\n",mode,degrees,horizon,x,y,got,expected);}
            if(!mode&&got>.5f){oldHorizonLeaks+=!above;oldMountainLeaks+=x/16==1;}
            ++pixels;
        }
        ok(staging->UnlockRect());++cases;
        if(mode){ // Source visibility must also go dark when every halo tap is below the horizon.
            for(unsigned tap=0;tap<32;++tap){c[13+tap][0]=.625f;c[13+tap][1]=.5f;c[13+tap][2]=c[13+tap][3]=.5f;}
            ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(halo));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float got;std::memcpy(&got,lock.pBits,4);ok(staging->UnlockRect());
            const float fade=std::clamp(30*(sn+cs/320.f),0.f,1.f);
            const float expected=fade*(2-fade);
            if(std::fabs(got-expected)>1e-5){++failures;std::printf("FAIL halo elevation=%g horizon=%g got=%g expected=%g\n",degrees,horizon,got,expected);}
            ++cases;
        }
    }
    assert(!oldHorizonLeaks&&oldMountainLeaks); // current native per-pixel horizon fade is retained
    IDirect3DPixelShader9 *oldVeil=nullptr,*fixedVeil=nullptr;code=read(argv[3]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&oldVeil));ok(d->CreatePixelShader(kCelestialVeilPSShader,&fixedVeil));
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){float z=(x>=16&&x<48&&y>=8&&y<56)?.5f:1.f;std::memcpy(depth.pixels.data()+(y*size+x)*4,&z,4);}
    auto blocker=texture(d,depth);ok(d->SetTexture(0,blocker));ok(d->SetTexture(3,white));ok(d->SetTexture(5,white));
    unsigned baselineInterior=0,fixedInterior=0,edgeSpill=0;
    for(unsigned mode=0;mode<2;++mode){
        const float cs=std::cos(.35f),sn=std::sin(.35f);float c[49][4]{};
        c[0][0]=c[0][1]=1.f/size;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;
        c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=std::tan(.2f);c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][0]=c[9][1]=c[9][2]=c[9][3]=1;c[10][1]=.2f;c[10][2]=1000;c[10][3]=1;
        c[11][0]=band.depth(.94f);c[11][1]=1;c[11][2]=NorthlightCelestialDisc::RepairDepthTolerance;c[11][3]=1;
        c[12][0]=1.1f;c[12][1]=20;c[12][2]=mode?0.f:2.4f;c[12][3]=1;
        c[47][0]=.8f;c[47][1]=.5f;c[47][2]=.2f;c[47][3]=.9f;c[48][0]=c[48][1]=c[48][2]=1;c[48][3]=.6f;
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(mode?fixedVeil:oldVeil));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);
            const bool body=x>=16&&x<48&&y>=8&&y<56;
            const bool interior=x>=20&&x<44&&y>=12&&y<52;
            // Diffuse bloom may cross the silhouette; only the compact core
            // must be absent there (tested quantitatively in the bloom replay).
            const bool expected=body;
            if((got>.00001f)!=expected){if(failures++<5)std::printf("FAIL veil mode=%u xy=%u,%u got=%g expected=%d\n",mode,x,y,got,expected);}
            if(interior&&got>0){if(mode)++fixedInterior;else ++baselineInterior;}
            if(mode&&body&&!interior&&got>0)++edgeSpill;
            ++pixels;
        }
        ok(staging->UnlockRect());++cases;
        // Both solar and lunar direct discs/glare must still reject every opaque body pixel.
        for(unsigned body=0;body<2;++body){
            c[11][3]=body?0.f:1.f;c[12][2]=2.4f;
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(fixed));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=8;y<56;++y)for(unsigned x=16;x<48;++x){float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);if(got!=0)++failures;}
            ok(staging->UnlockRect());++cases;
        }
    }
    assert(baselineInterior&&fixedInterior==baselineInterior&&edgeSpill);
    std::printf("veil actor baselineLitInterior=%u fixedLitInterior=%u edgeBloomPixels=%u\n",baselineInterior,fixedInterior,edgeSpill);
    oldVeil->Release();fixedVeil->Release();blocker->Release();d->SetTexture(3,nullptr);d->SetTexture(5,nullptr);
    std::printf("celestial GPU cases=%u pixels=%u baselineBelowHorizon=%u baselineThroughMountains=%u failures=%u\n",cases,pixels,oldHorizonLeaks,oldMountainLeaks,failures);
    d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);old->Release();fixed->Release();halo->Release();dep->Release();white->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
