// Real Stormwind corona spectra, production compositors, independent GPU device.
#define main unused_composite_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <map>
static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;
}
static float linear(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned N=4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AdditiveAtmosphere";assert(RegisterClassW(&wc));
    auto window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const DWORD referenceCode[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
    IDirect3DPixelShader9* referenceShader=nullptr;ok(d->CreatePixelShader(referenceCode,&referenceShader));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE})ok(d->SetRenderState(key,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    std::map<std::vector<unsigned char>,IDirect3DTexture9*> textures;
    auto bind=[&](unsigned slot,Image im){auto& t=textures[im.pixels];if(!t)t=texture(d,im);ok(d->SetTexture(slot,t));};
    for(unsigned slot=0;slot<16;++slot){for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,key,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(slot,key,D3DTADDRESS_CLAMP));}
    // session-59608-210097468-0, pixel (2040,840): encoded native scene,
    // separately captured linear sky base, local-fog sample and horizon colour.
    const float spectra[][3]={{8/255.f,21/255.f,36/255.f},{0,1e-8f,4e-7f},{.18f,0,.31f}};
    const float bases[][3]={{.005531311f,.007904053f,.015716553f},{.2f,.1f,.3f},{.2f,.5f,.6f}};
    const float lampFog[]={.040893555f,.022506714f,.008186340f},haze[]={0,12/255.f,30/255.f};
    const char* entries[]={"WorldComposite","WorldUnlitComposite","WorldCompositeHdr","WorldUnlitCompositeHdr","WorldMaterialCompositeHdr","WorldPrelitComposite","WorldPrelitCompositeHdr"};
    unsigned cases=0,failures=0;float worst=0;
    constexpr unsigned Rows=3*2*3*3*2*8;
    struct Check {float analytic[4];unsigned spectrum,control;bool emission,horizon;float alpha,transmission,rain;};
    for(unsigned samples:{0u,2u}){
    const D3DFORMAT format=samples?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F;
    IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(N*2,N*Rows,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
    if(samples)ok(d->CreateRenderTarget(N*2,N*Rows,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    ok(d->CreateOffscreenPlainSurface(N*2,N*Rows,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
    auto render=[&](IDirect3DPixelShader9* shader,unsigned row,bool reference){
        const float x=reference?N:0,y=float(row*N);
        const Vertex quad[]={{x-.5f,y-.5f,0,1,0,0},{x+N-.5f,y-.5f,0,1,1,0},{x-.5f,y+N-.5f,0,1,0,1},{x+N-.5f,y+N-.5f,0,1,1,1}};
        ok(d->SetPixelShader(shader));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
    };
    for(unsigned variant=0;variant<7;++variant){
        bool variantFailed=false;
        const bool hdr=variant==2||variant==3||variant==4||variant==6;
        auto code=read(std::string(argv[1])+"/"+entries[variant]+".bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));ok(d->SetPixelShader(ps));
        std::vector<Check> checks;ok(d->BeginScene());
        for(unsigned spectrum=0;spectrum<3;++spectrum)for(bool emission:{false,true})for(float alpha:{0.f,.25f,1.f})for(float transmission:{0.f,.7f,1.f})for(bool horizon:{false,true})for(unsigned control=0;control<8;++control){
            const auto* source=spectra[spectrum];const auto* base=bases[spectrum];
            const bool enabled=control!=3,legacy=control==7;const unsigned debug=control>=4&&control<7?control-3:0;
            const float rain=control==0?0.f:control==2?1.f:.65f;
            for(unsigned slot=0;slot<16;++slot)bind(slot,solid(0));
            bind(0,solid(source[0],source[1],source[2],.8f));bind(1,solid(1));bind(2,solid(hdr?base[0]:0,hdr?base[1]:0,hdr?base[2]:0));
            bind(4,solid(lampFog[0],lampFog[1],lampFog[2],transmission));bind(9,solid(lampFog[0],lampFog[1],lampFog[2],transmission));bind(10,solid(0,0,0,1));bind(13,solid(rain));bind(15,solid(1277));
            bind(7,legacy?solid(alpha):solid(emission?source[0]:0,emission?source[1]*.2f:0,0,alpha));
            float c[80][4]{};c[0][0]=c[0][1]=1.f/N;c[0][2]=.2f;c[0][3]=1277;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;
            for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[25][1]=c[25][2]=1;for(unsigned i=0;i<4;++i)c[33][i]=N;
            std::memcpy(c[34],haze,sizeof haze);c[34][3]=horizon?1.f:0.f;c[24][2]=float(debug);c[79][0]=enabled?1.f:0.f;c[79][1]=legacy?0.f:1.f;
            Check check{{0,0,0,.8f},spectrum,control,emission,horizon,alpha,transmission,rain};auto* expected=check.analytic;
            ok(d->SetPixelShaderConstantF(0,c[0],80));render(ps,unsigned(checks.size()),false);
            for(unsigned k=0;k<3;++k){
                const float native=hdr?base[k]:source[k],encodedEmission=emission?(k==0?source[k]:k==1?source[k]*.2f:0):0;
                const float s=hdr?linear(source[k]):source[k],remaining=hdr?linear(source[k]-encodedEmission):source[k]-encodedEmission;
                const float fraction=std::clamp((s-remaining)/std::max(s,.000001f),0.f,1.f);
                const float effectiveAlpha=enabled&&!debug?alpha:0,protectedShare=enabled&&!debug?(legacy?alpha:alpha+(1-alpha)*fraction):0;
                const float hazeAmount=horizon?.5f:0,scatter=(lampFog[k]+(hdr?linear(haze[k]):haze[k])*hazeAmount*transmission)*(1-rain);
                const float t=rain+(1-rain)*transmission*(1-hazeAmount);
                expected[k]=debug?(debug==3?lampFog[k]:native):t*native*(1-protectedShare)+native*protectedShare+scatter*(1-effectiveAlpha);
            }
            // Quantize the independent analytic oracle through a constant-only
            // shader on the identical native target and MSAA resolve. Retain
            // the FP32 acceptance bound for both target formats.
            ok(d->SetPixelShaderConstantF(0,expected,1));render(referenceShader,unsigned(checks.size()),true);checks.push_back(check);
        }
        assert(checks.size()==Rows);ok(d->EndScene());
        // Actual/reference pairs occupy adjacent 4x4 blocks. Read once per
        // shader/format, preserving every case without 24,000 GPU stalls.
        if(samples)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(samples?resolved:target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned row=0;row<checks.size();++row){const auto& check=checks[row];const auto* data=static_cast<const unsigned char*>(lock.pBits)+row*N*lock.Pitch;
            for(unsigned k=0;k<4;++k){const float actual=samples?half(reinterpret_cast<const std::uint16_t*>(data)[k]):reinterpret_cast<const float*>(data)[k];
                const unsigned offset=N*4+k;const float reference=samples?half(reinterpret_cast<const std::uint16_t*>(data)[offset]):reinterpret_cast<const float*>(data)[offset];
                const float error=std::fabs(actual-reference);worst=std::max(worst,error);
                if(!(error<.00003f)){if(!variantFailed)std::fprintf(stderr,"ADDITIVE_ATMOSPHERE_FAIL shader=%s msaa=%u spectrum=%u control=%u emission=%u alpha=%g T=%g horizon=%u rain=%g channel=%u actual=%g analytic=%g sameFormatReference=%g error=%g\n",entries[variant],samples,check.spectrum,check.control,unsigned(check.emission),check.alpha,check.transmission,unsigned(check.horizon),check.rain,k,actual,check.analytic[k],reference,error);variantFailed=true;++failures;}
            }++cases;
        }ok(staging->UnlockRect());
        ps->Release();
    }
    staging->Release();if(resolved)resolved->Release();target->Release();
    }
    std::printf("ADDITIVE_ATMOSPHERE_%s cases=%u failures=%u worst=%g measuredStormwindSpectra zero tiny coloredChannels maskOff debug123 HDR SDR msaa0 msaa2 alpha additive horizon fog rain01 noClient\n",failures?"FAIL":"PASS",cases,failures,worst);
    for(unsigned slot=0;slot<16;++slot)ok(d->SetTexture(slot,nullptr));for(auto& entry:textures)entry.second->Release();
    referenceShader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return failures?1:0;
}
