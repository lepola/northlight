// Optional isolated component benchmark, not an in-game FPS claim.
static void coverageBenchmark(IDirect3DDevice9* d,IDirect3DPixelShader9* native){
    if(!std::getenv("NORTHLIGHT_COVERAGE_BENCH"))return;
    constexpr unsigned W=1920,H=1080;
    IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    Plane old;Capture current;
    const D3DVIEWPORT9 vp={0,0,W,H,0,1};
    auto frame=[&](bool fixed){
        old.endFrame();current.endFrame();for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned r=1;r<4;++r)ok(d->SetRenderTarget(r,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetViewport(&vp));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE})ok(d->SetRenderState(k,FALSE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetPixelShader(native));
        const float colour[]={.2f,.06f,.01f,.25f};ok(d->SetPixelShaderConstantF(0,colour,1));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x202020,1,0));
        ok(d->BeginScene());
        for(unsigned i=0;i<64;++i){const bool glow=i%4==0;ok(d->SetRenderState(D3DRS_DESTBLEND,glow?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
            const float x=float((i%8)*208)-.5f,y=float((i/8)*112)-.5f;
            const Vertex q[]={{x,y,0,1,0,0},{x+256,y,0,1,1,0},{x,y+192,0,1,0,1},{x+256,y+192,0,1,1,1}};
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));};
            ok(fixed?current.draw(d,glow?Write::Additive:Write::Alpha,draw,true):old.draw(d,glow?Write::None:Write::Alpha,draw,true));
        }
        current.endBatch();old.endBatch();
        if(fixed){assert(current.texture(d,target)&&current.hasEmission());}else assert(old.texture(d,target));
        ok(d->EndScene());
    };
    std::vector<double> gpu[2],cpu[2];
    for(unsigned block=0;block<24;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;
        frame(mode);wait(event);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
        for(unsigned i=0;i<16;++i)frame(mode);
        QueryPerformanceCounter(&b);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
        UINT64 first=0,last=0,hz=0;BOOL bad=TRUE;assert(begin->GetData(&first,sizeof first,0)==S_OK&&end->GetData(&last,sizeof last,0)==S_OK&&freq->GetData(&hz,sizeof hz,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&hz&&last>=first);
        if(block>=8){gpu[mode].push_back(double(last-first)*1000/hz/16);cpu[mode].push_back(double(b.QuadPart-a.QuadPart)*1000/frequency.QuadPart/16);}
    }
    auto median=[](std::vector<double>& v){std::sort(v.begin(),v.end());assert(v.size()==8);return (v[3]+v[4])*.5;};
    const double oldGpu=median(gpu[0]),newGpu=median(gpu[1]),oldCpu=median(cpu[0]),newCpu=median(cpu[1]);
    std::printf("EFFECT_COVERAGE_BENCH width=%u height=%u draws=64 additive=16 alpha=48 oldGpuMs=%.6f newGpuMs=%.6f deltaGpuMs=%.6f oldSubmitMs=%.6f newSubmitMs=%.6f deltaSubmitMs=%.6f fullFrameFpsMeasured=0\n",W,H,oldGpu,newGpu,newGpu-oldGpu,oldCpu,newCpu,newCpu-oldCpu);
    current.reset();old.reset();for(auto* q:{event,begin,end,freq,disjoint})q->Release();target->Release();
}
