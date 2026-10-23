// Replay production halo -> diffuse veil on an isolated device, including history.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "celestial_disc_compiled_shaders.h"

int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned N=64;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightGlareWall";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *baseline=nullptr,*fixed=nullptr,*veil=nullptr;
    auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&baseline));
    ok(d->CreatePixelShader(kCelestialHaloVisibilityPSShader,&fixed));ok(d->CreatePixelShader(kCelestialVeilPSShader,&veil));
    IDirect3DTexture9* halo=nullptr;ok(d->CreateTexture(1,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&halo,nullptr));
    IDirect3DSurface9 *haloSurface=nullptr,*haloRead=nullptr,*scene=nullptr,*sceneRead=nullptr;ok(halo->GetSurfaceLevel(0,&haloSurface));
    ok(d->CreateOffscreenPlainSurface(1,1,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&haloRead,nullptr));
    ok(d->CreateRenderTarget(N,N,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&scene,nullptr));
    ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&sceneRead,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(unsigned s=0;s<6;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto scalar=[&](float value){Image im{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(im.pixels.data(),&value,4);return texture(d,im);};
    auto zero=scalar(0),one=scalar(1);Image wet{1,1,D3DFMT_G32R32F,8,std::vector<unsigned char>(8)};float waterValues[]={10,1};std::memcpy(wet.pixels.data(),waterValues,8);auto water=texture(d,wet);
    auto draw=[&](IDirect3DSurface9* target,unsigned size,IDirect3DPixelShader9* ps,float (*c)[4]){
        ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        const Vertex q[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
        ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->SetPixelShader(ps));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());
    };
    const char* names[]={"open-sky","opaque-wall","opaque-wall-history","partial-source","partial-source-history","distant-sky-band-wall","terrain","water","below-horizon"};
    unsigned cases=0,failures=0,baselineLeaks=0;double maxError=0;
    for(float maxZ:{.94f,1.f})for(unsigned scenario=0;scenario<9;++scenario){
        Image depth{N,N,D3DFMT_R32F,4,std::vector<unsigned char>(N*N*4)};
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){float raw=1;
            if(scenario==1||scenario==2||(scenario==3||scenario==4)&&x<N/2)raw=maxZ*.7f;
            if(scenario==5)raw=.9995f;
            std::memcpy(depth.pixels.data()+(y*N+x)*4,&raw,4);
        }auto dep=texture(d,depth);
        float c[49][4]{};const float pitch=scenario==8?-.4f:.4f,cs=std::cos(pitch),sn=std::sin(pitch);
        c[0][0]=c[0][1]=1.f/N;c[0][3]=1/maxZ;c[1][0]=c[1][1]=5;c[1][2]=1;c[1][3]=scenario==7?1:0;
        c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=.04f;c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][3]=1;c[10][1]=.2f;c[10][2]=1000;c[10][3]=1;c[11][0]=.9999f;c[11][1]=1;c[11][2]=1.f/16777215;c[11][3]=1;
        c[12][0]=1.1f;c[12][1]=20;c[12][3]=1;c[47][0]=.8f;c[47][1]=.5f;c[47][2]=.2f;c[47][3]=.9f;c[48][3]=.6f;
        for(unsigned i=0;i<32;++i){c[13+i][0]=(i+.5f)/32;c[13+i][1]=c[13+i][2]=c[13+i][3]=.5f;}
        c[45][0]=scenario==2||scenario==4?1:0;c[45][1]=.2f;c[45][2]=.1f;c[46][0]=scenario==6?1:0;c[46][1]=20;
        const float expected=scenario==0?1:scenario==3?.75f:scenario==4?.95f:0;
        for(unsigned version=0;version<2;++version){
            ok(d->SetTexture(0,dep));ok(d->SetTexture(2,water));ok(d->SetTexture(3,one));ok(d->SetTexture(4,scenario==6?one:zero));ok(d->SetTexture(5,nullptr));
            draw(haloSurface,1,version?fixed:baseline,c);ok(d->GetRenderTargetData(haloSurface,haloRead));D3DLOCKED_RECT lock{};ok(haloRead->LockRect(&lock,nullptr,D3DLOCK_READONLY));float visibility;std::memcpy(&visibility,lock.pBits,4);ok(haloRead->UnlockRect());
            const double error=std::fabs(visibility-expected);if(version){maxError=std::max(maxError,error);if(error>2e-5)++failures;}
            ok(d->SetTexture(3,halo));ok(d->SetTexture(5,halo));draw(scene,N,veil,c);ok(d->GetRenderTargetData(scene,sceneRead));ok(sceneRead->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned lit=0;
            for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){float value;std::memcpy(&value,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);if(!std::isfinite(value))++failures;lit+=value>1e-5;}
            ok(sceneRead->UnlockRect());if(scenario==1||scenario==2||scenario==5){if(version&&lit)++failures;if(!version)baselineLeaks+=lit;}
            if(version&&(scenario==3||scenario==4)&&!lit)++failures;
            std::printf("%s maxZ=%.2f version=%s visibility=%.5f expected=%.5f veilPixels=%u\n",names[scenario],maxZ,version?"fixed":"baseline",visibility,expected,lit);++cases;
        }dep->Release();
    }
    assert(baselineLeaks>0);std::printf("GLARE_WALL_RESULT cases=%u baselineLeakedPixels=%u maxError=%g failures=%u\n",cases,baselineLeaks,maxError,failures);
    for(unsigned s=0;s<6;++s)d->SetTexture(s,nullptr);d->SetPixelShader(nullptr);baseline->Release();fixed->Release();veil->Release();zero->Release();one->Release();water->Release();haloSurface->Release();halo->Release();haloRead->Release();scene->Release();sceneRead->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
