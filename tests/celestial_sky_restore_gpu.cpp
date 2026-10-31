// Experimental SkyRestore state and production disc/core shader replay.
// Synthetic textures and flat layers do not validate identities or order in a live game session.
// Build with existing test include flags plus -I<repo>/tests and -I<this directory>.
#include "sky_restore.h"
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
    auto flat=[&](DWORD color,float z,bool blend,bool additive=false){
        const Flat q[]={{-.5f,-.5f,z,1,color},{N-.5f,-.5f,z,1,color},{-.5f,N-.5f,z,1,color},{N-.5f,N-.5f,z,1,color}};
        ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,additive?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Flat)));
    };
    auto capture=[&]{ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*N*4);for(unsigned y=0;y<N;++y)std::memcpy(out.data()+y*N*4,static_cast<char*>(l.pBits)+y*l.Pitch,N*16);ok(staging->UnlockRect());return out;};
    float weatherGain=1;
    auto planet=[&](unsigned body,bool early,bool surface=true,bool halo=true){
        const float cs=std::cos(.4f),sn=std::sin(.4f);float c[49][4]{};
        c[0][0]=c[0][1]=1.f/N;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
        c[6][0]=cs;c[6][2]=sn;c[6][3]=std::tan(.14f);c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
        c[9][0]=c[9][1]=c[9][2]=1;c[9][3]=weatherGain;c[10][1]=.2f;c[10][2]=1000;c[10][3]=early?0.f:1.f;
        c[11][0]=.99992f;c[11][1]=early?0.f:1.f;c[11][2]=1.5f/16777215;c[11][3]=body?0.f:1.f;c[12][0]=1;c[12][1]=20;
        for(unsigned k=0;k<3;++k)c[47][k]=c[48][k]=1;
        const Vertex q[]={{-.5f,-.5f,.99992f,1,0,0},{N-.5f,-.5f,.99992f,1,1,0},{-.5f,N-.5f,.99992f,1,0,1},{N-.5f,N-.5f,.99992f,1,1,1}};
        ok(d->SetTexture(1,body?moon:white));ok(d->SetPixelShader(early?after:before));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,early));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetPixelShaderConstantF(0,c[0],49));if(surface)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));
        if(halo){c[9][3]=1;c[12][0]=1.1f;c[12][1]=body?6.f:20.f;c[12][2]=(body?1.f:2.4f)*weatherGain;c[12][3]=1;c[46][3]=early?1.f:0.f;ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR));ok(d->SetPixelShaderConstantF(0,c[0],49));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));}
    };
    using namespace SkyRestore;
    Draw base{320,0x35545844,512,512,5,6,baseVS,basePS,0xddb1ff594d51e1aaull,0x1234,true};
    Draw cloud{768,0x35545844,512,512,5,6,cloudVS,cloudPS,0x44e88f50406cbb61ull,0x1234,true};
    unsigned failures=0,cases=0;double maxError=0;
    constexpr unsigned alpha1=64,alpha2=128,horizonAlpha=32;
    const float bg[]={26.f/255,26.f/255,26.f/255},star[]={8.f/255,12.f/255,16.f/255};
    const float c1[]={51.f/255,77.f/255,102.f/255},c2[]={102.f/255,77.f/255,51.f/255},hz[]={20.f/255,30.f/255,40.f/255};
    // Measure only the shader's unobscured body output. The layer oracle below is CPU arithmetic.
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());
    flat(D3DCOLOR_ARGB(255,26,26,26),1,false);flat(D3DCOLOR_ARGB(255,8,12,16),.99995f,true,true);planet(1,true);
    ok(d->EndScene());const auto unobscured=capture();
    for(unsigned scenario=0;scenario<6;++scenario){
        State state;state.begin(10);Draw observedBase=base,observedCloud=cloud;
        const bool baseSubmitted=scenario!=4;
        if(scenario==1)observedCloud.hash^=1;
        if(scenario==2)observedCloud.ps^=1;
        if(scenario==3)observedCloud.target++;
        state.after(observedBase,baseSubmitted);
        if(scenario==5)state.begin(11); // prior-frame base cannot arm a new frame
        bool triggered=false;unsigned triggerCount=0;
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());
        flat(D3DCOLOR_ARGB(255,26,26,26),1,false);
        flat(D3DCOLOR_ARGB(255,8,12,16),.99995f,true,true); // intervening additive stars
        triggered=state.before(observedCloud);if(triggered){++triggerCount;planet(1,true);}
        flat(D3DCOLOR_ARGB(alpha1,51,77,102),.99996f,true);
        if(state.before(observedCloud))++triggerCount; // only one insertion per frame
        flat(D3DCOLOR_ARGB(alpha2,102,77,51),.99997f,true);
        flat(D3DCOLOR_ARGB(horizonAlpha,20,30,40),.99998f,true); // later horizon/base
        if(!triggered)planet(1,false); // legacy late fallback
        RECT box={20,20,32,44};ok(d->SetScissorRect(&box));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
        flat(D3DCOLOR_ARGB(255,51,77,102),.5f,false);ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        ok(d->EndScene());const auto actual=capture();
        if(triggered!=(scenario==0)||triggerCount!=(scenario==0?1u:0u))++failures;
        // For fallback independently draw the intended legacy order as a reference.
        std::vector<float> fallback;
        if(!triggered){
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());
            flat(D3DCOLOR_ARGB(255,26,26,26),1,false);flat(D3DCOLOR_ARGB(255,8,12,16),.99995f,true,true);
            flat(D3DCOLOR_ARGB(alpha1,51,77,102),.99996f,true);flat(D3DCOLOR_ARGB(alpha2,102,77,51),.99997f,true);
            flat(D3DCOLOR_ARGB(horizonAlpha,20,30,40),.99998f,true);planet(1,false);
            ok(d->SetScissorRect(&box));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));flat(D3DCOLOR_ARGB(255,51,77,102),.5f,false);
            ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->EndScene());fallback=capture();
        }
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x)for(unsigned k=0;k<3;++k){
            const unsigned p=y*N+x;const bool foreground=x>=20&&x<32&&y>=20&&y<44;
            float expected=0;
            if(foreground)expected=c1[k];
            else if(triggered){expected=unobscured[p*4+k];expected=expected*(1-alpha1/255.f)+c1[k]*(alpha1/255.f);
                expected=expected*(1-alpha2/255.f)+c2[k]*(alpha2/255.f);expected=expected*(1-horizonAlpha/255.f)+hz[k]*(horizonAlpha/255.f);}
            else expected=fallback[p*4+k];
            double error=std::fabs(actual[p*4+k]-expected);maxError=std::max(maxError,error);
            if(!std::isfinite(actual[p*4+k])||error>3e-5){if(failures++<5)std::printf("FAIL scenario=%u pixel=%u channel=%u got=%g expected=%g\n",scenario,p,k,actual[p*4+k],expected);}
        }
        ++cases;
    }
    (void)bg;(void)star;
    std::printf("sky restore GPU cases=%u maxError=%.9g failures=%u\n",cases,maxError,failures);
    d->SetPixelShader(nullptr);for(unsigned s=0;s<6;++s)d->SetTexture(s,nullptr);moon->Release();white->Release();before->Release();after->Release();staging->Release();target->Release();zbuffer->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
