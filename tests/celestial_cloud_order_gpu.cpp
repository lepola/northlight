// Production disc/core shader, real D3D9 blending and sky-band depth.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "celestial_disc_compiled_shaders.h"
#include "celestial_disc.h"

int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned N=64;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCloudOrderTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *before=nullptr,*after=nullptr;auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&before));ok(d->CreatePixelShader(kCelestialDiscPSShader,&after));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr,*zbuffer=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->CreateDepthStencilSurface(N,N,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&zbuffer,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(zbuffer));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
    for(auto state:{D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ZWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    D3DVIEWPORT9 vp={0,0,N,N,0,1};ok(d->SetViewport(&vp));auto white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});
    NorthlightCelestialDisc::Texture lunar;assert(NorthlightCelestialDisc::loadTexture(argv[3],lunar));
    NorthlightCelestialDisc::softenMoonRim(lunar);
    for(unsigned p=0;p<lunar.rgba.size();p+=4)std::swap(lunar.rgba[p],lunar.rgba[p+2]);
    auto moon=texture(d,{lunar.width,lunar.height,D3DFMT_A8R8G8B8,4,lunar.rgba});
    for(unsigned s=0;s<6;++s){ok(d->SetTexture(s,white));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    struct Flat{float x,y,z,w;DWORD color;};
    auto flat=[&](DWORD color,float z,bool blend){
        const Flat q[]={{-.5f,-.5f,z,1,color},{N-.5f,-.5f,z,1,color},{-.5f,N-.5f,z,1,color},{N-.5f,N-.5f,z,1,color}};
        ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Flat)));
    };
    auto capture=[&]{ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*N*4);for(unsigned y=0;y<N;++y)std::memcpy(out.data()+y*N*4,static_cast<char*>(l.pBits)+y*l.Pitch,N*16);ok(staging->UnlockRect());return out;};
    float weatherGain=1,hdrPolicy=0;
    auto planet=[&](unsigned body,bool early,bool surface=true,bool halo=true){
        const float cs=std::cos(.4f),sn=std::sin(.4f);float c[49][4]{};
        c[0][0]=c[0][1]=1.f/N;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=std::tan(.14f);c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][0]=c[9][1]=c[9][2]=1;c[9][3]=weatherGain;c[10][1]=.2f;c[10][2]=1000;c[10][3]=early?0.f:1.f;
        c[10][0]=hdrPolicy;
        c[11][0]=.99992f;c[11][1]=early?0.f:1.f;c[11][2]=1.5f/16777215;c[11][3]=body?0.f:1.f;c[12][0]=1;c[12][1]=20;
        for(unsigned k=0;k<3;++k)c[47][k]=c[48][k]=1;
        const Vertex q[]={{-.5f,-.5f,.99992f,1,0,0},{N-.5f,-.5f,.99992f,1,1,0},{-.5f,N-.5f,.99992f,1,0,1},{N-.5f,N-.5f,.99992f,1,1,1}};
        ok(d->SetTexture(1,body?moon:white));ok(d->SetPixelShader(early?after:before));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,early));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetPixelShaderConstantF(0,c[0],49));if(surface)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));
        if(halo){c[9][3]=1;c[12][0]=1.1f;c[12][1]=body?6.f:20.f;c[12][2]=(body?1.f:2.4f)*weatherGain;c[12][3]=1;c[46][3]=early?1.f:0.f;ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));}
    };
    unsigned cases=0,failures=0,baselineWrong=0,geometryPixels=0;double maxError=0;
    for(unsigned body=0;body<2;++body)for(float gain:{1.f,.5f,.15f,0.f}){weatherGain=gain;
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());flat(D3DCOLOR_ARGB(255,26,26,26),1,false);planet(body,true);ok(d->EndScene());const auto unobscured=capture();
        for(unsigned a:{0u,64u,128u,255u})for(unsigned b:{0u,128u})for(unsigned order=0;order<2;++order){
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());flat(D3DCOLOR_ARGB(255,26,26,26),1,false);
            if(order)planet(body,true);
            else if(body)planet(body,true,true,false); // installed moon: early surface, late halo
            flat(D3DCOLOR_ARGB(a,51,77,102),.99996f,true);flat(D3DCOLOR_ARGB(b,102,77,51),.99997f,true);
            if(!order)planet(body,false,!body,true);
            ok(d->EndScene());auto actual=capture();
            for(unsigned p=0;p<N*N;++p)for(unsigned k=0;k<3;++k){
                const float c1[]={51.f/255,77.f/255,102.f/255},c2[]={102.f/255,77.f/255,51.f/255};
                const float expected=(unobscured[p*4+k]*(1-a/255.f)+c1[k]*(a/255.f))*(1-b/255.f)+c2[k]*(b/255.f);
                const double error=std::fabs(actual[p*4+k]-expected);
                if(order){maxError=std::max(maxError,error);if(!std::isfinite(actual[p*4+k])||error>3e-5){if(failures++<4)std::printf("FAIL cloud body=%u alpha=%u/%u p=%u c=%u got=%g expected=%g\n",body,a,b,p,k,actual[p*4+k],expected);}}
                else baselineWrong+=error>.01;
            }
            ++cases;
        }
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());flat(D3DCOLOR_ARGB(255,26,26,26),1,false);planet(body,true);RECT box={20,20,32,44};ok(d->SetScissorRect(&box));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));flat(D3DCOLOR_ARGB(255,51,77,102),.5f,false);ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->EndScene());auto foreground=capture();
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){const bool blocked=x>=20&&x<32&&y>=20&&y<44;const float color[]={51.f/255,77.f/255,102.f/255};for(unsigned k=0;k<3;++k){const float expected=blocked?color[k]:unobscured[(y*N+x)*4+k];if(std::fabs(foreground[(y*N+x)*4+k]-expected)>3e-5)++failures;}geometryPixels+=blocked;}
    }
    assert(baselineWrong);
    weatherGain=1;
    for(unsigned body=0;body<2;++body){hdrPolicy=body?3:2;
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());planet(body,true,true,false);ok(d->EndScene());const auto bright=capture();
        float peak=0;for(unsigned p=0;p<N*N;++p)peak=std::max(peak,bright[p*4]);assert(peak>1);++cases;
        ok(d->BeginScene());flat(D3DCOLOR_ARGB(128,51,77,102),.99996f,true);ok(d->EndScene());const auto covered=capture();
        for(unsigned p=0;p<N*N;++p)for(unsigned k=0;k<3;++k){const float tint[]={51.f/255,77.f/255,102.f/255};const float expected=bright[p*4+k]*(127.f/255)+tint[k]*(128.f/255);if(std::fabs(covered[p*4+k]-expected)>5e-5f)++failures;}++cases;
        std::printf("HDR celestial body=%u relativePeak=%g cloudOrder=linear\n",body,peak);
    }
    std::printf("cloud order GPU cases=%u checkedPixels=%u baselineWrongChannels=%u geometryPixels=%u maxError=%.9g failures=%u\n",cases,cases*N*N,baselineWrong,geometryPixels,maxError,failures);
    d->SetPixelShader(nullptr);for(unsigned s=0;s<6;++s)d->SetTexture(s,nullptr);moon->Release();white->Release();before->Release();after->Release();staging->Release();target->Release();zbuffer->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
