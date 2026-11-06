// Production receiver pipeline: character shadow, orange weapon light and blue spell light.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
static Image constant(float r,float g=0,float b=0,float a=0){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const bool legacy=argc==4&&std::strcmp(argv[3],"--legacy-counterexample")==0;assert(argc==3||legacy);const std::string compiled=argv[1];constexpr unsigned N=16;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightOverlapOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* ps[6]{};const char* entries[]={"WorldLighting","RemovalSmooth","TemporalLight","LocalDirect","LocalDirect16","WorldComposite"};
    for(unsigned i=0;i<6;++i){auto code=read(compiled+"/"+entries[i]+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps[i]));}
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
    ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    auto draw=[&](unsigned shader){ok(d->SetPixelShader(ps[shader]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    struct Target{IDirect3DTexture9* t=nullptr;IDirect3DSurface9* s=nullptr;};
    Target raw,base,smooth,history,output,distance;
    for(auto p:{&raw,&base,&smooth,&history,&output,&distance}){ok(d->CreateTexture(N,N,1,D3DUSAGE_RENDERTARGET,p==&distance?D3DFMT_R32F:D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&p->t,nullptr));ok(p->t->GetSurfaceLevel(0,&p->s));}
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    std::vector<IDirect3DTexture9*> owned;auto make=[&](Image im){auto t=texture(d,im);owned.push_back(t);return t;};
    auto zero=make(constant(0)),white=make(constant(1,1,1,1)),scene=make(constant(.5f,.5f,.5f,1)),normal=make(constant(0,0,-1,1)),ao=make(constant(0,0,0,1)),dist=make(constant(10)),fog=make(constant(0,0,0,1));
    const float nearZ=.2f,farZ=1000,nd=farZ/(farZ-nearZ)*(1-nearZ/10);auto depth=make(constant(nd));
    unsigned cases=0,receivers=0,movingReceivers=0,hiddenReceivers=0;float maxOrder=0,maxSum=0,maxTrail=0;
    for(bool shadow:{false,true})for(bool wide:{false,true})for(bool separated:{false,true})for(float temporal:{0.f,.85f}){
        float c[116][4]{};c[0][0]=c[0][1]=1.f/N;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;
        c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;c[35][0]=1.f/256;
        c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[10][2]=c[14][2]=.5f;
        c[16][2]=c[28][2]=-1;for(unsigned k=0;k<3;++k){c[17][k]=c[18][k]=c[29][k]=.5f;}
        c[20][2]=1;c[27][1]=1;c[30][1]=c[30][2]=1;c[30][3]=30;c[33][0]=c[33][1]=c[33][2]=c[33][3]=N;
        c[52][1]=1;c[53][0]=c[54][1]=c[55][2]=c[56][3]=1;c[57][0]=temporal;
        auto inputs=[&]{for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetTexture(0,scene));ok(d->SetTexture(1,depth));ok(d->SetTexture(2,white));ok(d->SetTexture(3,white));ok(d->SetTexture(10,ao));ok(d->SetTexture(11,zero));ok(d->SetTexture(14,normal));ok(d->SetTexture(15,dist));ok(d->SetPixelShaderConstantF(0,c[0],116));};
        inputs();ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderTarget(0,raw.s));ok(d->SetRenderTarget(1,base.s));ok(d->SetTexture(12,shadow?zero:white));draw(0);ok(d->SetRenderTarget(1,nullptr));
        ok(d->SetRenderTarget(0,smooth.s));ok(d->SetTexture(8,raw.t));ok(d->SetTexture(12,base.t));draw(1);
        // Reuse the current unfiltered directional buffer as valid stationary history.
        ok(d->SetRenderTarget(0,history.s));ok(d->SetRenderTarget(1,distance.s));ok(d->SetTexture(9,smooth.t));ok(d->SetTexture(14,raw.t));ok(d->SetTexture(15,dist));draw(2);ok(d->SetRenderTarget(1,nullptr));
        std::vector<std::array<float,3>> rawSnapshots;
        auto render=[&](unsigned mask,bool reverse,float motion=0.f){
            // The raw directional target is dead after TemporalLight; recycle
            // it only after inputs() has unbound every temporal input alias.
            inputs();ok(d->SetRenderTarget(0,raw.s));
            if(legacy){
                // Original production order: directional correction and local
                // radiance share raw RGB before removal/temporal filtering.
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderTarget(1,base.s));ok(d->SetTexture(12,shadow?zero:white));draw(0);ok(d->SetRenderTarget(1,nullptr));
            }else ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            ok(d->SetTexture(12,base.t));ok(d->SetTexture(2,zero));ok(d->SetTexture(3,nullptr));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
            const float colors[2][3]={{.04f,.012f,0},{0,.008f,.04f}};
            auto lamps=[&](unsigned which,unsigned slot,float (&pos)[16][4],float (&rgb)[16][4]){pos[slot][0]=(separated?(which?.5f:-.5f):0)+motion;pos[slot][2]=5;pos[slot][3]=30;for(unsigned k=0;k<3;++k)rgb[slot][k]=colors[which][k];rgb[slot][3]=1.f/30;};
            if(wide){float p[16][4]{},rgb[16][4]{};unsigned slot=0;for(unsigned j=0;j<2;++j){const unsigned which=reverse?1-j:j;if(mask&(1u<<which))lamps(which,slot++,p,rgb);}ok(d->SetPixelShaderConstantF(84,p[0],16));ok(d->SetPixelShaderConstantF(100,rgb[0],16));draw(4);}
            else for(unsigned j=0;j<2;++j){const unsigned which=reverse?1-j:j;if(!(mask&(1u<<which)))continue;float p[16][4]{},rgb[16][4]{};lamps(which,0,p,rgb);ok(d->SetPixelShaderConstantF(36,p[0],8));ok(d->SetPixelShaderConstantF(44,rgb[0],8));draw(3);}
            ok(d->GetRenderTargetData(raw.s,staging));D3DLOCKED_RECT rawLock{};ok(staging->LockRect(&rawLock,nullptr,D3DLOCK_READONLY));std::array<float,3> sample{};for(unsigned k=0;k<3;++k){std::uint16_t h;std::memcpy(&h,static_cast<char*>(rawLock.pBits)+8*rawLock.Pitch+(8*4+k)*2,2);sample[k]=half(h);}ok(staging->UnlockRect());rawSnapshots.push_back(sample);
            ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            if(legacy){
                ok(d->SetRenderTarget(0,smooth.s));ok(d->SetTexture(8,raw.t));ok(d->SetTexture(14,normal));ok(d->SetTexture(15,dist));draw(1);
                ok(d->SetRenderTarget(0,history.s));ok(d->SetRenderTarget(1,distance.s));ok(d->SetTexture(9,smooth.t));ok(d->SetTexture(14,raw.t));draw(2);ok(d->SetRenderTarget(1,nullptr));
            }
            ok(d->SetRenderTarget(0,output.s));ok(d->SetTexture(3,legacy?zero:raw.t));ok(d->SetTexture(8,history.t));ok(d->SetTexture(9,fog));ok(d->SetTexture(13,zero));ok(d->SetTexture(14,normal));ok(d->SetTexture(15,distance.t));draw(5);
            ok(d->GetRenderTargetData(output.s,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> pixels(N*N*4);for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N*4;++x){std::uint16_t h;std::memcpy(&h,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*2,2);pixels[y*N*4+x]=half(h);}ok(staging->UnlockRect());return pixels;
        };
        const auto dark=render(0,false),orange=render(1,false),blue=render(2,false),both=render(3,false),reverse=render(3,true);
        const unsigned center=(8*N+8)*4;
        std::printf("OVERLAP_CASE legacy=%u shadow=%u wide=%u separated=%u temporal=%g dark=%g,%g,%g orange=%g,%g,%g blue=%g,%g,%g rawRed=%g/%g rawBlue=%g/%g\n",unsigned(legacy),unsigned(shadow),unsigned(wide),unsigned(separated),temporal,dark[center],dark[center+1],dark[center+2],orange[center],orange[center+1],orange[center+2],blue[center],blue[center+1],blue[center+2],rawSnapshots[0][0],rawSnapshots[1][0],rawSnapshots[0][2],rawSnapshots[2][2]);std::fflush(stdout);
        assert(rawSnapshots[1][0]>rawSnapshots[0][0]+.002f&&rawSnapshots[2][2]>rawSnapshots[0][2]+.002f);
        // Current lights never enter directional history: a moving source and
        // an immediately retired flash must not leave either color behind.
        const auto moved=render(3,false,3.f),retiredBlue=render(1,false),retiredOrange=render(2,false),retiredBoth=render(0,false),returned=render(3,false);
        // Central receivers face both sources; compare final production composite RGB.
        for(unsigned y=6;y<10;++y)for(unsigned x=6;x<10;++x){const unsigned at=(y*N+x)*4;
            if(legacy&&shadow){assert(orange[at]==dark[at]&&blue[at+2]==dark[at+2]&&both[at]==dark[at]&&both[at+2]==dark[at+2]);++hiddenReceivers;}
            else{assert(orange[at]>dark[at]+.002f&&blue[at+2]>dark[at+2]+.002f);assert(both[at]>blue[at]+.002f&&both[at+2]>orange[at+2]+.002f);}
            bool changed=false;
            for(unsigned k=0;k<3;++k){assert(std::isfinite(both[at+k]));const float order=std::fabs(both[at+k]-reverse[at+k]),sum=std::fabs(both[at+k]-(orange[at+k]+blue[at+k]-dark[at+k]));maxOrder=std::max(maxOrder,order);maxSum=std::max(maxSum,sum);assert(order<=.001f&&sum<=.002f);assert(std::fabs(dark[at+k]-(shadow?.275f:.5f))<.002f);
                const float trail=std::max({std::fabs(retiredBlue[at+k]-orange[at+k]),std::fabs(retiredOrange[at+k]-blue[at+k]),std::fabs(retiredBoth[at+k]-dark[at+k]),std::fabs(returned[at+k]-both[at+k])});maxTrail=std::max(maxTrail,trail);assert(trail==0);changed|=std::fabs(moved[at+k]-both[at+k])>.0002f;
            }movingReceivers+=changed;++receivers;}
        ++cases;
    }
    assert(movingReceivers>0);assert(!legacy||hiddenReceivers>0);
    std::printf("local overlap production GPU legacy=%u cases=%u receivers=%u hiddenDespiteRawContribution=%u movingReceivers=%u maxOrder=%g maxSuperposition=%g maxRetiredTrail=%g actualItemGeometry=0 msaaSilhouettes=0\n",unsigned(legacy),cases,receivers,hiddenReceivers,movingReceivers,maxOrder,maxSum,maxTrail);
    for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);d->SetPixelShader(nullptr);for(auto p:{&raw,&base,&smooth,&history,&output,&distance}){p->s->Release();p->t->Release();}for(auto t:owned)t->Release();for(auto shader:ps)shader->Release();staging->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
