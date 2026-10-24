// Hidden standalone D3D9 comparison; the production AOBlur bytecode is unmodified.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#ifdef NORTHLIGHT_AO_SPLIT_PROFILE
#include <cstdarg>
static unsigned splitReports=0;
static void logf(const char* format,...) {
    char line[2048];va_list args;va_start(args,format);std::vsnprintf(line,sizeof line,format,args);va_end(args);
    assert(std::strstr(line,"AOBlurSetup=")&&std::strstr(line,"AOBlur=")&&!std::strstr(line,"marks_truncated"));
    ++splitReports;
}
#include "gpu_profile.h"
#endif

static Image rgba(unsigned w,unsigned h) {
    return {w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(std::size_t(w)*h*16)};
}
static void pixel(Image& image,unsigned x,unsigned y,const float* value) {
    std::memcpy(image.pixels.data()+(std::size_t(y)*image.width+x)*image.bytes,value,image.bytes);
}

int main(int argc,char** argv) {
    assert(argc==7);
    const unsigned fullW=std::strtoul(argv[4],nullptr,10),fullH=std::strtoul(argv[5],nullptr,10);
    const unsigned W=std::max(1u,fullW/2),H=std::max(1u,fullH/2);
    const bool fp16=std::strtoul(argv[6],nullptr,10)==16;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightAOBlurEquivalence";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[3]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
#ifdef NORTHLIGHT_AO_SPLIT_PROFILE
    auto* splitProfile=new NorthlightGpuProfile(d,"AO split",3);unsigned splitSamples=0;
#endif
    IDirect3DPixelShader9* shaders[2]{};
    for(unsigned i=0;i<2;++i){auto code=read(argv[i+1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    IDirect3DTexture9* resolved=nullptr;IDirect3DSurface9 *resolveSurface=nullptr,*staging=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&resolved,nullptr));ok(resolved->GetSurfaceLevel(0,&resolveSurface));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    for(unsigned s=0;s<4;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,s==0?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    auto draw=[&](unsigned count){ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    unsigned cases=0;
    for(unsigned shape=0;shape<9;++shape){
        Image depth{fullW,fullH,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(fullW)*fullH*4)},ambient=rgba(W,H),water=rgba(fullW,fullH);
        if(fp16)ambient={W,H,D3DFMT_A16B16G16R16F,8,std::vector<unsigned char>(std::size_t(W)*H*8)};
        const float clipNear=.2f,clipFar=1277.f,partition=.94f;
        for(unsigned y=0;y<fullH;++y)for(unsigned x=0;x<fullW;++x){
            const float rx=(2*(x+.5f)/fullW-1)/1.2686f,ry=(1-2*(y+.5f)/fullH)/1.9626f;
            float z=4/(1+.25f*rx+.15f*ry);
            if(shape==1||shape==6)z=x<fullW/2?2.f:6.f;
            if(shape==2)z=3.2f+.12f*std::sin(x*.11f)*std::sin(y*.13f);
            float raw=clipFar/(clipFar-clipNear)*(1-clipNear/z)*partition;if(shape==3||shape==6&&x>fullW*3/4)raw=1;
            pixel(depth,x,y,&raw);
            const float mask[]={2.5f,float(shape==4&&x>fullW/3&&x<fullW*2/3),0,0};pixel(water,x,y,mask);
        }
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            float ao=.15f+.8f*float((x*23+y*31)%127)/126;
            if(shape==0||shape==3)ao=1;
            if(shape==5)ao=1.f-float((x*7+y*11)%5)/4096;
            if(shape==7)ao=(x<W/2)?1:.6f;
            if(shape==8)ao=x%5==0&&y%7==0?.994f+.0005f*((x/5+y/7)%7):1;
            if(fp16){
                unsigned a=0x3400+(x*23+y*31)%2048; // Exact stored AO values from .25 to <1.
                if(shape==0||shape==3)a=0x3c00;
                if(shape==5)a=0x3c00-(x*7+y*11)%5; // Straddle the existing .9995 early-out.
                if(shape==7)a=x<W/2?0x3c00:0x38cd;
                if(shape==8)a=x%5==0&&y%7==0?0x3bf4+(x/5+y/7)%7:0x3c00;
                const unsigned base=shape==4?0x4000:0x2800;
                const std::uint16_t value[]={std::uint16_t(base+(x%11)*8),std::uint16_t(base+0x800+(y%7)*16),std::uint16_t(base+0x400+((x+y)%13)*8),std::uint16_t(a)};
                std::memcpy(ambient.pixels.data()+(std::size_t(y)*W+x)*8,value,8);
            }else{
                const float base=shape==4?2.f:0.f;
                const float value[]={base+.02f*float(x%11),base+.03f*float(y%7),base+.01f*float((x+y)%13),ao};pixel(ambient,x,y,value);
            }
        }
        auto dt=texture(d,depth),at=texture(d,ambient),wt=texture(d,water);
        ok(d->SetTexture(0,at));ok(d->SetTexture(1,dt));ok(d->SetTexture(2,at));ok(d->SetTexture(3,wt));
        float c[5][4]={{1.f/fullW,1.f/fullH,clipNear,clipFar},{1.2686f,1.9626f,.6f,0},{.08f,2,0,0},{0,1/partition,partition,0},{float(shape==4),0,0,0}};ok(d->SetPixelShaderConstantF(0,c[0],5));
        for(unsigned samples:{0u,2u,4u,8u}){
            if(fullW>256&&samples)continue;
            DWORD qualities=0;if(samples&&FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A16B16G16R16F,TRUE,D3DMULTISAMPLE_TYPE(samples),&qualities)))continue;
            IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetViewport(&vp));
            std::vector<unsigned char> output[2];std::vector<double> timing[2];
            const unsigned blocks=fullW>256?16:2,draws=fullW>256?16:1;
            for(unsigned block=0;block<blocks;++block){
                const unsigned mode=(block/2)%2?1-block%2:block%2;
                ok(d->SetPixelShader(shaders[mode]));draw(2);wait(event);
#ifdef NORTHLIGHT_AO_SPLIT_PROFILE
                if(block==1){assert(splitProfile->beginFrame(++splitSamples,true));ok(d->SetTexture(0,at));ok(d->SetTexture(1,dt));ok(d->SetPixelShader(shaders[mode]));splitProfile->mark("AOBlurSetup");}
#endif
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(draws);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);
                if(block>=4)timing[mode].push_back(double(b-a)/double(f)*1000/draws);
                if(block<2){ok(d->StretchRect(target,nullptr,resolveSurface,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolveSurface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[mode].resize(std::size_t(W)*H*8);for(unsigned y=0;y<H;++y)std::memcpy(output[mode].data()+std::size_t(y)*W*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);ok(staging->UnlockRect());}
#ifdef NORTHLIGHT_AO_SPLIT_PROFILE
                if(block==1){splitProfile->mark("AOBlur");splitProfile->endFrame();}splitProfile->poll();
#endif
            }
            std::size_t changed=0;for(std::size_t i=0;i<output[0].size();++i)changed+=output[0][i]!=output[1][i];
            std::size_t nonfinite=0;float minAO=1,maxAO=0;
            for(std::size_t i=0;i<output[1].size()/2;++i){std::uint16_t bits;std::memcpy(&bits,output[1].data()+i*2,2);float value=half(bits);nonfinite+=!std::isfinite(value);if(i%4==3){minAO=std::min(minAO,value);maxAO=std::max(maxAO,value);}}
            assert(!nonfinite&&minAO>=0&&maxAO<=1);
            assert((shape==0||shape==3)?minAO==1:minAO<1);
            if(fp16)for(std::size_t i=0;i<std::size_t(W)*H;++i)assert(!std::memcmp(output[1].data()+i*8,ambient.pixels.data()+i*8,6)); // Bloom RGB passes through, including HDR.
            double median[2]{};for(unsigned mode=0;mode<2;++mode)if(!timing[mode].empty()){std::sort(timing[mode].begin(),timing[mode].end());auto n=timing[mode].size();median[mode]=(timing[mode][n/2]+timing[mode][(n-1)/2])*.5;}
            std::printf("{\"width\":%u,\"height\":%u,\"input_bits\":%u,\"shape\":%u,\"msaa\":%u,\"changed_bytes\":%llu,\"nonfinite\":%llu,\"min_ao\":%.6f,\"max_ao\":%.6f,\"old_ms\":%.6f,\"new_ms\":%.6f}\n",fullW,fullH,fp16?16:32,shape,samples,static_cast<unsigned long long>(changed),static_cast<unsigned long long>(nonfinite),minAO,maxAO,median[0],median[1]);std::fflush(stdout);assert(!changed);++cases;target->Release();
        }
        for(unsigned slot=0;slot<4;++slot)ok(d->SetTexture(slot,nullptr));dt->Release();at->Release();wt->Release();
    }
    std::printf("AO_BLUR_EQUIVALENCE_PASS cases=%u game_launched=false\n",cases);
#ifdef NORTHLIGHT_AO_SPLIT_PROFILE
    wait(event);splitProfile->poll();assert(splitReports==splitSamples);delete splitProfile;
    std::printf("AO_SPLIT_PROFILE_PASS samples=%u reports=%u\n",splitSamples,splitReports);
#endif
    d->SetPixelShader(nullptr);for(auto shader:shaders)shader->Release();for(auto query:{event,start,end,frequency,disjoint})query->Release();staging->Release();resolveSurface->Release();resolved->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
