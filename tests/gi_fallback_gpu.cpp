// Production WorldGI and shadow-baseline contracts on a hidden D3D9 device.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>

static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;
}
static Image metadata(float coverage,bool spatial=false){
    Image im{16,4,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16*4*16)};
    for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        const bool valid=x<2&&y<2&&z<2&&coverage>0&&(!spatial||x==0);
        const float p[]={float(x),float(y),float(z),valid?1-.45f*coverage:-1};
        std::memcpy(im.pixels.data()+((z*4+x)+y*16)*16,p,16);
    }
    return im;
}
int main(int argc,char** argv){
    assert(argc>=3);const bool benchmark=argc>=5&&std::string(argv[4])=="--benchmark";const unsigned W=benchmark?1280:4,H=benchmark?720:4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightGiFallback";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    const auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,4,4,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    const auto module=LoadLibraryA(argv[2]);assert(module);const auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=4;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("GPU %s\n",adapter.Description);
    IDirect3DPixelShader9 *gi=nullptr,*oldGi=nullptr,*lighting[2]{};
    auto shader=[&](const std::string& path,IDirect3DPixelShader9** output){auto bytes=read(path);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),output));};
    shader(std::string(argv[1])+"/WorldGI.bin",&gi);
    shader(std::string(argv[1])+"/WorldLighting.bin",&lighting[0]);shader(std::string(argv[1])+"/WorldLightingSoft.bin",&lighting[1]);
    if(argc>=4)shader(std::string(argv[3])+"/WorldGI.bin",&oldGi);
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
    IDirect3DSurface9 *target=nullptr,*baseline=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&baseline,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
    std::array<IDirect3DTexture9*,16> owned{};
    auto bind=[&](unsigned slot,Image im){ok(d->SetTexture(slot,nullptr));if(owned[slot])owned[slot]->Release();owned[slot]=texture(d,im);ok(d->SetTexture(slot,owned[slot]));};
    auto draw=[&](IDirect3DPixelShader9* ps,unsigned n=1){ok(d->SetPixelShader(ps));ok(d->BeginScene());for(unsigned i=0;i<n;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    auto sample=[&](IDirect3DSurface9* surface){ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::array<float,4> value{};for(unsigned k=0;k<4;++k){uint16_t v;std::memcpy(&v,static_cast<char*>(lock.pBits)+lock.Pitch+8+2*k,2);value[k]=half(v);}ok(staging->UnlockRect());return value;};
    float c[79][4]{};c[0][0]=1.f/(W*2);c[0][1]=1.f/(H*2);c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;
    c[6][0]=c[6][1]=4;c[6][2]=3.75f;c[6][3]=1;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[19][3]=8;c[20][0]=4;c[20][1]=.5f;c[20][2]=.85f;c[20][3]=1;c[24][3]=1;
    c[33][0]=float(W*2);c[33][1]=float(H*2);c[33][2]=float(W);c[33][3]=float(H);
    const float depth=1000.f/(1000-.2f)*(1-.2f/10);
    bind(1,solid(depth));bind(7,solid(100,10000,1,-1000));bind(11,solid(0));
    unsigned cases=0,unknownNeutral=0,residentDark=0,negativeControls=0;float worst=0;
    for(float coverage:{0.f,.25f,.5f,1.f})for(float visibility:{-1.f,0.f,.15f,.5f,1.f})for(float gain:{0.f,.5f,1.f})for(float irradiance:{0.f,.1f,.7f})for(float confidence:{0.f,1.f})for(float ready:{0.f,1.f}){
        const float residentCoverage=coverage*ready;
        c[20][1]=gain;c[20][3]=ready;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(10,metadata(coverage));bind(12,solid(0,0,0,visibility));bind(14,solid(0,0,1,confidence));
        for(unsigned s:{4u,5u,6u})bind(s,solid(irradiance/.28209479f));
        draw(gi);const auto actual=sample(target);
        for(unsigned k=0;k<3;++k){const float raw=(irradiance-c[18][k])*residentCoverage*gain;
            const float expected=std::min(raw,0.f)+std::max(raw,0.f)*confidence;worst=std::max(worst,std::fabs(actual[k]-expected));assert(std::isfinite(actual[k])&&std::fabs(actual[k]-expected)<.001f);}
        assert(actual[3]==0);
        if(residentCoverage==0){assert(actual==(std::array<float,4>{}));++unknownNeutral;}
        if(residentCoverage==1&&irradiance==0&&gain>0){assert(actual[0]<0&&actual[1]<0&&actual[2]<0);++residentDark;}
        if(oldGi){
            draw(oldGi);const auto old=sample(target);
            if(residentCoverage==1||std::fabs(visibility)==1||gain==0)assert(old==actual);
            if(residentCoverage==0&&visibility==0&&gain>0){assert(old[0]<actual[0]-.09f);++negativeControls;}
        }++cases;
    }
    // Spatial fade at a missing neighboring probe, independent of born-time fade.
    c[20][1]=.5f;bind(10,metadata(1,true));bind(12,solid(0,0,0,.15f));bind(14,solid(0,0,1,1));for(unsigned s:{4u,5u,6u})bind(s,solid(.1f/.28209479f));
    float previous=0;for(unsigned i=0;i<=32;++i){c[6][0]=float(i)*.25f;ok(d->SetPixelShaderConstantF(0,c[0],79));draw(gi);const float actual=sample(target)[0],coverage=1-float(i)/32;
        const float expected=(.1f-.2f)*coverage*.5f;assert(std::fabs(actual-expected)<.0002f);if(i)assert(actual>=previous&&actual-previous<.002f);previous=actual;++cases;}
    // Sky and water cannot acquire the missing-probe correction.
    bind(10,metadata(0));bind(1,solid(1));draw(gi);assert(sample(target)==(std::array<float,4>{}));bind(1,solid(depth));c[30][0]=1;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(11,solid(5,1));draw(gi);assert(sample(target)==(std::array<float,4>{}));c[30][0]=0;bind(11,solid(0));
    // Producer contract: distinguish unknown coverage from fully occluded moon.
    c[7][0]=c[8][1]=1.f/48;c[10][2]=.5f;c[11][0]=c[12][1]=1.f/192;c[14][2]=.5f;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;c[35][0]=1.f/1280;
    c[16][2]=c[28][2]=1;c[27][1]=1;c[73][2]=1;c[6][0]=4;
    bind(2,solid(0));bind(3,solid(0));bind(12,solid(1,1,1,1));bind(15,solid(0));
    ok(d->SetRenderTarget(1,baseline));
    for(auto ps:lighting)for(float moon:{0.f,1.f})for(float outside:{0.f,1.f})for(float active:{0.f,1.f}){
        c[16][3]=moon;c[6][0]=outside?1000:4;for(unsigned k=0;k<3;++k)c[17][k]=active*.6f;
        ok(d->SetPixelShaderConstantF(0,c[0],79));draw(ps);const auto value=sample(baseline);const float expected=outside?-1:active?(moon?0:.15f):1;assert(std::fabs(value[3]-expected)<.0003f);++cases;
    }
    ok(d->SetRenderTarget(1,nullptr));
    // Run the production lighting -> GI -> SDR/HDR composition chain. The
    // second ambient palette is from the supplied ArgentTournament QA capture.
    // A missing hemisphere estimate preserves native brightness; actual direct
    // shadows, resident-probe occlusion and contact AO must remain visible.
    IDirect3DTexture9* chainLight=nullptr;IDirect3DSurface9* chainSurface=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&chainLight,nullptr));
    ok(chainLight->GetSurfaceLevel(0,&chainSurface));
    IDirect3DPixelShader9* composites[2]{};
    shader(std::string(argv[1])+"/WorldComposite.bin",&composites[0]);
    shader(std::string(argv[1])+"/WorldCompositeHdr.bin",&composites[1]);
    const float palettes[][3]={{.2f,.3f,.4f},{.31373f,.35686f,.4f}};
    unsigned chainCases=0,chainNative=0,chainShadow=0,chainResident=0,chainAO=0;float chainError=0;
    for(const auto& ambient:palettes)for(float coverage:{0.f,.25f,1.f})for(bool shadowed:{false,true})for(float ao:{.6f,1.f})for(float value:{0.f,.01f,.18f,.7f,1.f}){
        ok(d->SetTexture(8,nullptr));ok(d->SetRenderTarget(0,chainSurface));ok(d->SetRenderTarget(1,baseline));
        std::memset(c,0,sizeof c);c[0][0]=1.f/(W*2);c[0][1]=1.f/(H*2);c[0][2]=.2f;c[0][3]=1000;
        c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
        c[6][0]=c[6][1]=4;c[6][2]=3.75f;c[6][3]=1;c[10][2]=c[14][2]=.5f;c[16][2]=c[28][2]=1;
        c[19][3]=8;c[20][0]=4;c[20][1]=.5f;c[20][2]=.85f;c[20][3]=1;c[24][3]=1;c[27][1]=1;c[35][0]=1.f/1280;c[73][2]=1;
        c[33][0]=float(W*2);c[33][1]=float(H*2);c[33][2]=float(W);c[33][3]=float(H);
        for(unsigned k=0;k<3;++k){c[17][k]=c[29][k]=.4f;c[18][k]=ambient[k];}
        bind(1,solid(depth));bind(2,solid(shadowed?0.f:1.f));bind(3,solid(shadowed?0.f:1.f));
        bind(9,solid(0));bind(11,solid(0));bind(12,solid(1,1,1,1));bind(14,solid(0,0,1,1));
        ok(d->SetPixelShaderConstantF(0,c[0],79));draw(lighting[0]);
        const float visibility=shadowed?.15f:1.f;
        ok(d->SetRenderTarget(1,nullptr));bind(10,metadata(coverage));bind(7,solid(100,10000,1,-1000));
        for(unsigned slot:{4u,5u,6u})bind(slot,solid(0));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
        ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));draw(gi);ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        const auto coefficient=sample(chainSurface);
        for(unsigned k=0;k<3;++k)assert(std::fabs(coefficient[k]-(.4f*(visibility-1)-ambient[k]*.5f*coverage))<.001f);
        ok(d->SetRenderTarget(0,target));ok(d->SetTexture(8,chainLight));
        bind(0,solid(value,value*.7f,value*.2f,.375f));bind(2,solid(0));bind(3,solid(0));bind(7,solid(0));
        bind(9,solid(0,0,0,1));bind(10,solid(0,0,0,ao));bind(12,solid(ambient[0]+.4f,ambient[1]+.4f,ambient[2]+.4f));bind(13,solid(0));bind(15,solid(10));
        c[78][2]=1;ok(d->SetPixelShaderConstantF(78,c[78],1));
        for(unsigned hdr=0;hdr<2;++hdr){
            draw(composites[hdr]);const auto actual=sample(target);
            for(unsigned k=0;k<3;++k){
                const float encoded=value*(k==0?1.f:k==1?.7f:.2f);
                const float native=hdr?(encoded<=.04045f?encoded/12.92f:std::pow((encoded+.055f)/1.055f,2.4f)):encoded;
                const float transported=native*ao,delta=.4f*(visibility-1)-ambient[k]*.5f*coverage;
                const float expected=std::max(transported+std::min(transported/(ambient[k]+.4f),1.f)*delta,transported*.55f);
                const float error=std::fabs(actual[k]-expected);chainError=std::max(chainError,error);assert(std::isfinite(actual[k])&&error<.0015f);
                if(coverage==0&&!shadowed&&ao==1){assert(std::fabs(actual[k]-native)<.0015f);++chainNative;}
                if(value>0&&shadowed&&actual[k]<transported-.00001f)++chainShadow;
                if(value>0&&coverage==1&&!shadowed&&actual[k]<transported-.00001f)++chainResident;
                if(value>0&&ao<1&&coverage==0&&!shadowed&&actual[k]<native-.00001f)++chainAO;
            }
            assert(actual[3]==.375f);++chainCases;
        }
    }
    assert(chainNative&&chainShadow&&chainResident&&chainAO);
    std::printf("GI_COMPOSITE_BASELINE_PASS cases=%u maxError=%.9g nativeControls=%u directShadowControls=%u residentOcclusionControls=%u contactAOControls=%u SDR=1 HDR=1\n",chainCases,chainError,chainNative,chainShadow,chainResident,chainAO);
    ok(d->SetTexture(8,nullptr));for(auto* ps:composites)ps->Release();chainSurface->Release();chainLight->Release();
    assert(unknownNeutral&&residentDark&&(!oldGi||negativeControls));
    std::printf("GI_BASELINE_PASS missingProbeNeutral=%u provenResidentDark=%u oldDarkeningControls=%u\n",unknownNeutral,residentDark,negativeControls);
    std::printf("GI_FALLBACK_PASS cases=%u maxError=%.9g fullCoverageExact=%u skyWaterNeutral=1 unknownCoverageNeutral=1\n",cases,worst,unsigned(oldGi!=nullptr));
    if(benchmark&&oldGi){
        // Alternating GPU-only blocks at the capture's half resolution.
        c[6][0]=c[6][1]=4;c[20][1]=.5f;c[30][0]=0;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(12,solid(0,0,0,.15f));bind(14,solid(0,0,1,1));
        IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        for(float coverage:{0.f,1.f}){bind(10,metadata(coverage));std::vector<double> timings[2];
            for(unsigned block=0;block<20;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;auto* ps=mode?gi:oldGi;draw(ps,8);wait(event);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));draw(ps,32);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(begin->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&f&&b>=a);if(block>=4)timings[mode].push_back(double(b-a)*1000/double(f)/32);}
            double med[2];for(unsigned i=0;i<2;++i){std::sort(timings[i].begin(),timings[i].end());med[i]=(timings[i][3]+timings[i][4])*.5;}
            std::printf("GI_FALLBACK_TIMING resident=%.0f width=%u height=%u oldMs=%.6f newMs=%.6f deltaMs=%.6f\n",coverage,W,H,med[0],med[1],med[1]-med[0]);
        }
        for(auto* q:{event,begin,end,frequency,disjoint})q->Release();
    }
    if(oldGi)oldGi->Release();
    for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);if(owned[s])owned[s]->Release();}
    for(auto* ps:lighting)ps->Release();gi->Release();staging->Release();baseline->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
