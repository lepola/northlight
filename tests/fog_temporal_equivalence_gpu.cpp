// Genuine baseline/candidate fog filters on a hidden standalone D3D9 device.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main

static Image field(unsigned w,unsigned h,unsigned bits) {
    return {w,h,bits==16?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F,bits/2,std::vector<unsigned char>(std::size_t(w)*h*bits/2)};
}
static void store(Image& im,unsigned x,unsigned y,const std::uint16_t* h) {
    auto* p=im.pixels.data()+(std::size_t(y)*im.width+x)*im.bytes;
    if(im.bytes==8)std::memcpy(p,h,8);
    else{float v[4];for(unsigned i=0;i<4;++i)v[i]=half(h[i]);std::memcpy(p,v,16);}
}

int main(int argc,char** argv) {
    assert(argc==8||argc==9);
    const bool spatial=argc==9&&std::strcmp(argv[8],"FogBlur")==0;
    const unsigned fullW=std::strtoul(argv[4],nullptr,10),fullH=std::strtoul(argv[5],nullptr,10),W=fullW/2,H=fullH/2;
    const unsigned inputBits=std::strtoul(argv[6],nullptr,10),outputBits=std::strtoul(argv[7],nullptr,10);
    const D3DFORMAT format=outputBits==16?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightFogTemporalEquivalence";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[3]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto code=read(argv[i+1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    IDirect3DTexture9* resolved=nullptr;IDirect3DSurface9 *resolveSurface=nullptr,*staging=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&resolved,nullptr));ok(resolved->GetSurfaceLevel(0,&resolveSurface));
    ok(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    for(unsigned slot:{1u,9u,11u,14u,15u}){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,slot==9||slot==14?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    D3DVIEWPORT9 vp={0,0,W,H,0,1};
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    auto draw=[&](unsigned count){ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    unsigned cases=0,failures=0;
    for(unsigned shape=0;shape<18;++shape){
        auto current=field(W,H,inputBits),history=field(W,H,inputBits);
        Image depth{fullW,fullH,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(fullW)*fullH*4)},previous{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(W)*H*4)},water{fullW,fullH,D3DFMT_G32R32F,8,std::vector<unsigned char>(std::size_t(fullW)*fullH*8)};
        const float clipNear=.2f,clipFar=1277.f,partition=.94f,z=8;
        for(unsigned y=0;y<fullH;++y)for(unsigned x=0;x<fullW;++x){
            float receiver=z;if(spatial&&shape%6==2)receiver=4+.03f*x+.01f*y;if(spatial&&shape%6>=3)receiver=x<fullW/2?3.f:19.f;
            float raw=clipFar/(clipFar-clipNear)*(1-clipNear/receiver)*partition;if(shape==10||shape==17||spatial&&shape%6==4&&y>fullH/2)raw=1;
            if(spatial&&shape==16&&x%19==0)raw=x%2?NAN:INFINITY;
            std::memcpy(depth.pixels.data()+(std::size_t(y)*fullW+x)*4,&raw,4);
            const float mask[]={shape%6==5?receiver+10:receiver*.8f,(x/7+y/11)%3?.8f:0.f};
            std::memcpy(water.pixels.data()+(std::size_t(y)*fullW+x)*8,mask,8);
        }
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            std::uint16_t a[]={0,0,0,0x3c00},b[]={0,0,0,0x3c00};
            if(shape==1){a[0]=0x4400;a[1]=0x4000;a[2]=0x4c00;a[3]=0x3900;}
            if(shape>=2){a[0]=std::uint16_t(0x2000+(x*13+y*17)%0x3000);a[1]=std::uint16_t(0x2400+(x*5+y*31)%0x2800);a[2]=std::uint16_t(0x2800+(x*23+y*7)%0x2400);a[3]=std::uint16_t(0x3400+(x*11+y*3)%0x800);}
            std::memcpy(b,a,8);
            if(shape==3||shape==4&&x>W/2){b[0]+=2;b[1]+=1;b[2]-=1;b[3]+=1;}
            if(shape==4&&x<W/2){a[0]=a[1]=a[2]=b[0]=b[1]=b[2]=0;a[3]=b[3]=0x3c00;}
            if(shape==12){a[0]=(x+y)%2?0x8000:0;a[1]=0x8000;a[2]=0;a[3]=0x3c00;b[0]=0;b[1]=0;b[2]=0x8000;b[3]=0x3c00;}
            if(shape==13){const std::uint16_t special[]={0,0x8000,0x7c00,0xfc00,0x7e00,0x3555,0x7bff,0x0001};for(unsigned i=0;i<4;++i)a[i]=b[i]=special[(x+y+i)%8];}
            if(shape==14){a[0]=b[0]=0x7bff;a[1]=b[1]=0xfbff;a[2]=b[2]=0x0001;a[3]=b[3]=0x3c00;}
            store(current,x,y,a);store(history,x,y,b);
            float oldZ=shape==6?999.f:shape==11&&x%7==0?19.f:z;std::memcpy(previous.pixels.data()+(std::size_t(y)*W+x)*4,&oldZ,4);
        }
        auto ct=texture(d,current),ht=texture(d,history),dt=texture(d,depth),pt=texture(d,previous),wt=spatial?texture(d,water):nullptr;
        ok(d->SetTexture(9,ct));ok(d->SetTexture(14,ht));ok(d->SetTexture(1,dt));ok(d->SetTexture(15,pt));
        if(spatial)ok(d->SetTexture(11,wt));
        float c[68][4]{};c[0][0]=1.f/fullW;c[0][1]=1.f/fullH;c[0][2]=clipNear;c[0][3]=clipFar;c[1][0]=1.2686f;c[1][1]=1.9626f;c[1][2]=1;c[2][0]=1/partition;
        for(unsigned i=0;i<4;++i)c[3+i][i]=c[53+i][i]=1;
        c[33][0]=float(fullW);c[33][1]=float(fullH);c[33][2]=float(W);c[33][3]=float(H);
        if(spatial){c[30][0]=shape%3!=0;c[34][shape%2]=2;}
        c[64][1]=shape==5?0:shape==15?1:shape==16?.12345f:.85f;
        if(shape==7)c[56][0]=1000;
        if(shape==8)c[56][2]=-100;
        if(shape==9||shape==10){const float yaw=.045f;c[53][0]=c[55][2]=std::cos(yaw);c[53][2]=-std::sin(yaw);c[55][0]=std::sin(yaw);}
        ok(d->SetPixelShaderConstantF(0,c[0],68));
        for(unsigned samples:{0u,2u,4u,8u}){
            if(fullW>256&&samples)continue;
            DWORD qualities=0;if(samples&&FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,format,TRUE,D3DMULTISAMPLE_TYPE(samples),&qualities)))continue;
            IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetViewport(&vp));
            std::vector<unsigned char> output[2];std::vector<double> timing[2];
            const bool bench=fullW>256&&shape<6;const unsigned blocks=bench?28:2,draws=bench?32:1;
            for(unsigned block=0;block<blocks;++block){
                const unsigned mode=(block/2)%2?1-block%2:block%2;
                ok(d->SetPixelShader(shaders[mode]));draw(2);wait(event);
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(draws);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);
                if(block>=8)timing[mode].push_back(double(b-a)/double(f)*1000/draws);
                if(block<2){ok(d->StretchRect(target,nullptr,resolveSurface,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolveSurface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[mode].resize(std::size_t(W)*H*outputBits/2);for(unsigned y=0;y<H;++y)std::memcpy(output[mode].data()+std::size_t(y)*W*outputBits/2,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*outputBits/2);ok(staging->UnlockRect());}
            }
            std::size_t changed=0,nonfinite[2]={};for(std::size_t i=0;i<output[0].size();++i)changed+=output[0][i]!=output[1][i];
            for(unsigned mode=0;mode<2;++mode)for(std::size_t i=0;i<std::size_t(W)*H*4;++i){float value;if(outputBits==16){std::uint16_t bits;std::memcpy(&bits,output[mode].data()+i*2,2);value=half(bits);}else std::memcpy(&value,output[mode].data()+i*4,4);nonfinite[mode]+=!std::isfinite(value);}
            double median[2]{};for(unsigned mode=0;mode<2;++mode)if(!timing[mode].empty()){std::sort(timing[mode].begin(),timing[mode].end());auto n=timing[mode].size();median[mode]=(timing[mode][n/2]+timing[mode][(n-1)/2])*.5;}
            std::printf("{\"width\":%u,\"height\":%u,\"input_bits\":%u,\"output_bits\":%u,\"shape\":%u,\"msaa\":%u,\"changed_bytes\":%llu,\"old_nonfinite\":%llu,\"new_nonfinite\":%llu,\"old_ms\":%.6f,\"new_ms\":%.6f}\n",fullW,fullH,inputBits,outputBits,shape,samples,static_cast<unsigned long long>(changed),static_cast<unsigned long long>(nonfinite[0]),static_cast<unsigned long long>(nonfinite[1]),median[0],median[1]);std::fflush(stdout);
            failures+=changed!=0;++cases;target->Release();
        }
        for(unsigned slot:{1u,9u,11u,14u,15u})ok(d->SetTexture(slot,nullptr));ct->Release();ht->Release();dt->Release();pt->Release();if(wt)wt->Release();
    }
    std::printf("FOG_TEMPORAL_EQUIVALENCE cases=%u failures=%u game_launched=false\n",cases,failures);
    d->SetPixelShader(nullptr);for(auto shader:shaders)shader->Release();for(auto query:{event,start,end,frequency,disjoint})query->Release();staging->Release();resolveSurface->Release();resolved->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    return failures?1:0;
}
