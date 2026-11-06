// Exact production MaterialComposite support oracle; hidden device, never the game.
#define main northlight_unused_material_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>

static const float supportDepths[]={4,7.99f,8,8.01f,36,47.98f,48,48.02f,96,119.9f,120,120.1f,180};
static const float supportColors[][4]={{-.5f,.25f,2,-0.f},{-0.f,-1,.5f,-.5f},{.75f,-0.f,-.25f,0},{.5f,.375f,.25f,1}};
static Image supportSolid(float r,float g,float b,float a){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float value[]={r,g,b,a};std::memcpy(im.pixels.data(),value,16);return im;
}
static std::array<Image,4> supportFields(unsigned w,unsigned h,int benchmark=-1){
    std::array<Image,4> images={Image{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(w)*h*4)},
        Image{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(std::size_t(w)*h*16)},
        Image{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(std::size_t(w)*h*16)},
        Image{w,h,D3DFMT_G32R32F,8,std::vector<unsigned char>(std::size_t(w)*h*8)}};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const unsigned domain=benchmark<0?(y/4)%6:0,capture=benchmark<0?y%4:0;
        const float z=benchmark<0?supportDepths[x%13]:benchmark==0?4:benchmark==1?180:x<w/2?4:180;
        const float depth=domain==1?1.f:1000.f/(1000.f-.2f)*(1-.2f/z);
        // +Z is the scene reference. Oct (.5,.5) is neutral detail in that frame.
        const float cover=capture==2?.01f:1.f,octX=capture==1?.5f:.68f,octY=capture==1?.5f:.38f;
        const float material[]={octX*cover,octY*cover,(capture==3?z+10:z)*cover,cover};
        const float occlusion[]={capture==1?1.f:.35f,capture==1?1.f:.65f,capture==1?1.f:.45f,1};
        const float water[]={domain==3?z+20:domain==5?.2f:z*.8f,domain==2||domain==3?1.f:domain==4?.003f:0.f};
        const void* values[]={&depth,material,occlusion,water};
        for(unsigned i=0;i<images.size();++i)std::memcpy(images[i].pixels.data()+(std::size_t(y)*w+x)*images[i].bytes,values[i],images[i].bytes);
    }
    return images;
}
static void supportConstants(float (&c)[79][4],unsigned w,unsigned h,unsigned mode,float occlusionRange){
    std::memset(c,0,sizeof c);c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.2f;c[0][3]=1000;
    c[1][0]=1.2f;c[1][1]=1.4f;c[1][2]=1;c[2][0]=1;
    for(unsigned i=0;i<4;++i)c[3+i][i]=1;
    c[18][0]=.25f;c[18][1]=.18f;c[18][2]=.2f;c[18][3]=.15f;
    c[19][0]=.04f;c[19][1]=.02f;c[19][2]=.03f;c[20][2]=.6f;
    c[25][1]=c[25][2]=1;c[30][0]=c[30][2]=1;
    c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(w/2);c[33][3]=float(h/2);
    c[73][0]=mode&1?1.f:0.f;c[73][1]=48;
    c[74][0]=.6f;c[74][2]=.8f;c[75][0]=-.3f;c[75][1]=.4f;c[75][2]=.8660254f;
    c[76][0]=.7f;c[76][1]=.5f;c[76][2]=.3f;c[77][0]=.1f;c[77][1]=.2f;c[77][2]=.4f;
    c[78][0]=mode&2?1.f:0.f;c[78][1]=occlusionRange;
}
static float supportValue(const std::vector<unsigned char>& pixels,std::size_t component,bool fp16){
    if(fp16){std::uint16_t bits;std::memcpy(&bits,pixels.data()+component*2,2);return half(bits);}
    float value;std::memcpy(&value,pixels.data()+component*4,4);return value;
}
int main(int argc,char** argv){
    assert(argc==4||(argc==5&&std::strcmp(argv[4],"--benchmark")==0));const bool benchmark=argc==5;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialSupportOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto code=read(argv[i+1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    for(unsigned s=0;s<16;++s){const bool linear=s==0||s==4||s==5||s==10||s==12;
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,key,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));
        ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,key,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    std::vector<IDirect3DTexture9*> owned;auto make=[&](const Image& im){auto* t=texture(d,im);owned.push_back(t);return t;};
    IDirect3DTexture9* scenes[4]{};for(unsigned i=0;i<4;++i)scenes[i]=make(supportSolid(supportColors[i][0],supportColors[i][1],supportColors[i][2],supportColors[i][3]));
    ok(d->SetTexture(4,make(supportSolid(0,0,0,.75f))));ok(d->SetTexture(5,make(supportSolid(.7f,.6f,.5f,1))));
    ok(d->SetTexture(8,make(supportSolid(0,0,0,1))));ok(d->SetTexture(10,make(supportSolid(0,0,0,.8f))));
    ok(d->SetTexture(12,make(supportSolid(.4f,.5f,.6f,1))));ok(d->SetTexture(14,make(supportSolid(0,0,1,1))));
    const unsigned patternSlots[]={1,9,6,11};
    auto bindFields=[&](const std::array<Image,4>& fields){std::array<IDirect3DTexture9*,4> t{};for(unsigned i=0;i<t.size();++i){t[i]=texture(d,fields[i]);ok(d->SetTexture(patternSlots[i],t[i]));}return t;};
    auto releaseFields=[&](std::array<IDirect3DTexture9*,4>& t){for(unsigned i=0;i<t.size();++i){ok(d->SetTexture(patternSlots[i],nullptr));t[i]->Release();}};
    unsigned cases=0,failures=0;std::size_t changedBytes=0,alphaChanges=0,earlyPixels=0,farPixels=0,normalEffects=0,occlusionEffects=0;
    for(const auto size:{std::array<unsigned,2>{128,80},std::array<unsigned,2>{137,79}}){
        const unsigned W=size[0],H=size[1];auto fields=bindFields(supportFields(W,H));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        D3DVIEWPORT9 vp={0,0,W,H,0,1};
        for(auto format:{D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F})for(unsigned samples:{0u,2u}){
            DWORD quality=0;if(samples&&FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,format,TRUE,D3DMULTISAMPLE_TYPE(samples),&quality))){std::printf("MATERIAL_SUPPORT_SKIP format=%u msaa=%u unsupported=1\n",unsigned(format),samples);continue;}
            const bool fp16=format==D3DFMT_A16B16G16R16F;const unsigned bytes=fp16?8:16;
            IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
            ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
            if(samples)ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));else{resolved=target;resolved->AddRef();}
            ok(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetViewport(&vp));
            const unsigned beforeFailures=failures;const std::size_t beforeChanged=changedBytes,beforeAlpha=alphaChanges;
            for(unsigned mode=0;mode<4;++mode)for(float range:{8.f,120.f,0.f})for(unsigned palette=0;palette<4;++palette){
                float c[79][4];supportConstants(c,W,H,mode,range);ok(d->SetPixelShaderConstantF(0,c[0],79));ok(d->SetTexture(0,scenes[palette]));
                std::vector<unsigned char> output[2];for(unsigned variant=0;variant<2;++variant){
                    ok(d->SetPixelShader(shaders[variant]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
                    if(samples)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));
                    D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[variant].resize(std::size_t(W)*H*bytes);
                    for(unsigned y=0;y<H;++y)std::memcpy(output[variant].data()+std::size_t(y)*W*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*bytes);ok(staging->UnlockRect());
                }
                std::size_t caseChanged=0;for(std::size_t i=0;i<output[0].size();++i)caseChanged+=output[0][i]!=output[1][i];changedBytes+=caseChanged;
                if(caseChanged&&failures++<4)std::printf("MATERIAL_SUPPORT_DIFFERENCE w=%u h=%u format=%u msaa=%u mode=%u range=%g palette=%u changedBytes=%llu\n",W,H,unsigned(format),samples,mode,range,palette,static_cast<unsigned long long>(caseChanged));
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const std::size_t pixel=std::size_t(y)*W+x;const unsigned domain=(y/4)%6;
                    alphaChanges+=std::memcmp(output[0].data()+pixel*bytes+bytes*3/4,output[1].data()+pixel*bytes+bytes*3/4,bytes/4)!=0;
                    const bool early=mode==0||domain==1||domain==2,farReceiver=!early&&x%13==12;
                    earlyPixels+=early;farPixels+=farReceiver;
                    for(unsigned k=0;k<4;++k){const float value=supportValue(output[1],pixel*4+k,fp16),source=supportColors[palette][k];
                        const float expected=farReceiver&&k<3?std::max(source,0.f):source;
                        if(!std::isfinite(value)||((early||farReceiver||k==3)&&value!=expected)){
                            if(failures++<4)std::printf("MATERIAL_SUPPORT_FAIL w=%u h=%u format=%u msaa=%u mode=%u range=%g palette=%u x=%u y=%u channel=%u got=%g expected=%g\n",W,H,unsigned(format),samples,mode,range,palette,x,y,k,value,expected);
                        }
                        if(domain==0&&y%4==0&&x%13==0&&palette==3&&k<3&&value!=source){normalEffects+=mode==1;occlusionEffects+=mode==2;}
                    }
                }
                ++cases;
            }
            std::printf("{\"width\":%u,\"height\":%u,\"output_bits\":%u,\"msaa\":%u,\"cases\":48,\"changed_bytes\":%llu,\"alpha_changes\":%llu,\"oracle_failures\":%u}\n",W,H,fp16?16:32,samples,static_cast<unsigned long long>(changedBytes-beforeChanged),static_cast<unsigned long long>(alphaChanges-beforeAlpha),failures-beforeFailures);std::fflush(stdout);
            staging->Release();resolved->Release();target->Release();
        }
        releaseFields(fields);
    }
    if(benchmark){
        constexpr unsigned W=3840,H=2160;IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        auto draw=[&](unsigned n){ok(d->BeginScene());for(unsigned i=0;i<n;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
        float c[79][4];supportConstants(c,W,H,3,120);ok(d->SetPixelShaderConstantF(0,c[0],79));ok(d->SetTexture(0,scenes[3]));
        for(unsigned support=0;support<3;++support){auto fields=bindFields(supportFields(128,1,int(support)));std::vector<double> timing[2];
            for(unsigned block=0;block<24;++block){const unsigned variant=(block/2)%2?1-block%2:block%2;ok(d->SetPixelShader(shaders[variant]));draw(2);wait(event);
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(16);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&hz,sizeof hz,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&hz&&b>=a);
                if(block>=8)timing[variant].push_back(double(b-a)/double(hz)*1000/16);
            }
            double median[2]{};for(unsigned variant=0;variant<2;++variant){assert(timing[variant].size()==8);std::sort(timing[variant].begin(),timing[variant].end());median[variant]=(timing[variant][3]+timing[variant][4])*.5;}
            std::printf("{\"benchmark\":\"%s\",\"width\":3840,\"height\":2160,\"source_fields\":\"128x1 synthetic\",\"old_ms\":%.6f,\"new_ms\":%.6f,\"paired_order\":\"ABBA\",\"game_fps\":false}\n",support==0?"near-only":support==1?"far-only":"mixed",median[0],median[1]);std::fflush(stdout);releaseFields(fields);
        }
        for(auto* q:{event,start,end,frequency,disjoint})q->Release();target->Release();
    }
    if(changedBytes||alphaChanges||!normalEffects||!occlusionEffects)++failures;
    std::printf("MATERIAL_SUPPORT_RESULT cases=%u changedBytes=%llu alphaChanges=%llu earlyPixels=%llu farClampPixels=%llu normalEffects=%llu occlusionEffects=%llu failures=%u game_launched=false\n",cases,static_cast<unsigned long long>(changedBytes),static_cast<unsigned long long>(alphaChanges),static_cast<unsigned long long>(earlyPixels),static_cast<unsigned long long>(farPixels),static_cast<unsigned long long>(normalEffects),static_cast<unsigned long long>(occlusionEffects),failures);std::fflush(stdout);
    ok(d->SetPixelShader(nullptr));for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto* t:owned)t->Release();for(auto* shader:shaders)shader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
