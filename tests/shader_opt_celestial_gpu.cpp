// Frozen/current bytecode comparison on a hidden D3D9 device; no game or generated shader headers.
#define main northlight_unused_celestial_helpers
#include "composite_distance_gpu.cpp"
#undef main
#include <array>

using CelestialConstants = std::array<std::array<float,4>,49>;

static Image celestialFloatImage(unsigned w,unsigned h,unsigned channels) {
    return {w,h,channels==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,channels*4,
            std::vector<unsigned char>(std::size_t(w)*h*channels*4)};
}
static void celestialPixel(Image& im,unsigned x,unsigned y,const float* value) {
    std::memcpy(im.pixels.data()+(std::size_t(y)*im.width+x)*im.bytes,value,im.bytes);
}
static float celestialValue(const std::vector<unsigned char>& bytes,std::size_t component,unsigned bits) {
    if(bits==16){std::uint16_t v;std::memcpy(&v,bytes.data()+component*2,2);return half(v);}
    float v;std::memcpy(&v,bytes.data()+component*4,4);return v;
}
static CelestialConstants celestialConstants(unsigned w,unsigned h,unsigned body,unsigned glare,
                                             unsigned glowMode,unsigned linear,unsigned scene,bool large=false) {
    const float elevation=scene==5?-.08f:scene==6?.0008f:.35f;
    const float cs=std::cos(elevation),sn=std::sin(elevation);
    CelestialConstants c{};
    c[0]={1.f/w,1.f/h,0,1/.94f};c[1]={5,5,1,float(scene==4||scene==7)};
    c[2]={0,1,0,0};c[3]={-sn,0,cs,0};c[4]={cs,0,sn,0};c[5][3]=1;
    c[6]={cs,0,sn,large?(glare?.025f:.25f):.12f};c[7][1]=1;c[8]={-sn,0,cs,0};
    c[9]={.82f,.63f,.41f,scene==5?.25f:1.f};
    const bool late=scene%2!=0;
    c[10]={float(linear),.2f,1000,float(late)};
    c[11]={.9999219f,float(late),1.5f/16777215,body?0.f:1.f};
    c[12]={body?1.f:1.1f,body?6.f:20.f,body?.9f:2.4f,float(glare)};
    c[45]={float(late),.35f,.1f,0};
    c[46]={float(scene==3||scene==7),20,0,float(glowMode)};
    c[47]={.8f,.5f,.2f,.9f};c[48]={1,.96f,.9f,.6f};
    if(scene==7){c[47]={-.08f,.3f,1.5f,.9f};c[48]={-.125f,1.25f,2,.6f};}
    return c;
}

struct CelestialFixture {
    IDirect3DDevice9* d;
    unsigned width,height,bits,bytes,sampleCount;
    IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr,*stencil=nullptr;
    IDirect3DPixelShader9* solid=nullptr;
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    std::vector<IDirect3DTexture9*> inputs;
    Vertex quad[4];
    CelestialFixture(IDirect3DDevice9* device,unsigned w,unsigned h,unsigned precision,unsigned samples)
        :d(device),width(w),height(h),bits(precision),bytes(precision/2),sampleCount(samples?samples:1),
         quad{{-.5f,-.5f,.99992f,1,0,0},{w-.5f,-.5f,.99992f,1,1,0},
              {-.5f,h-.5f,.99992f,1,0,1},{w-.5f,h-.5f,.99992f,1,1,1}} {
        const D3DFORMAT format=bits==16?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F;
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,TRUE,&stencil,nullptr));
        // Existing world_lighting_capture_gpu.cpp fixture: mov oC0,c0, with no clamp or texture conversion.
        const DWORD solidCode[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
        ok(d->CreatePixelShader(solidCode,&solid));
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));
        ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
        ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(stencil));
        D3DVIEWPORT9 vp={0,0,w,h,0,1};ok(d->SetViewport(&vp));
        ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,
                       D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
                       D3DRS_TWOSIDEDSTENCILMODE,D3DRS_CLIPPLANEENABLE})ok(d->SetRenderState(state,FALSE));
        ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
        for(unsigned s=0;s<6;++s){
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,s==1||s==4?D3DTEXF_LINEAR:D3DTEXF_POINT));
            for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,state,D3DTADDRESS_CLAMP));
            ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));
        }
    }
    void clearInputs(){for(unsigned s=0;s<6;++s)ok(d->SetTexture(s,nullptr));for(auto* p:inputs)p->Release();inputs.clear();}
    void bind(unsigned slot,const Image& im){auto* p=texture(d,im);inputs.push_back(p);ok(d->SetTexture(slot,p));}
    void scene(unsigned which){
        clearInputs();
        Image depth=celestialFloatImage(width,height,1),water=celestialFloatImage(width,height,4);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const bool box=x>=width/3&&x<width*2/3&&y>=height/4&&y<height*3/4;
            float z=which==1?1.f:box?.5f:1.f;
            if(which==2)z=box?.999f:1.f; // normalized sky, raw-depth repair still sees the silhouette
            if(which==3||which==4)z=1;
            if(which==6){const float bands[]={.94f-.00000006f,.94f,.94000006f,.9999218f,1.f};z=bands[x*5/width];}
            celestialPixel(depth,x,y,&z);
            const float mask[]={box?2.5f:0.f,box?1.f:0.f,0,0};celestialPixel(water,x,y,mask);
        }
        bind(0,depth);bind(2,water);
        Image authored=celestialFloatImage(64,64,4);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            const float u=(x+.5f)/32-1,v=(y+.5f)/32-1,r=std::sqrt(u*u+v*v);
            // Opaque detail, a soft authored rim and a half-lit phase; solar alpha remains analytic.
            const float a=std::clamp((1-r)*12,0.f,1.f)*(which%2&&x<32?.125f:1.f);
            const float values[]={.15f+float((x*7+y*11)%29)/24,.04045f+u*.02f,.3f+v*.2f,a};
            celestialPixel(authored,x,y,values);
        }
        bind(1,authored);
        Image terrain=celestialFloatImage(32,32,1);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const float v=x<16?1.f:x==16?.5f:0.f;celestialPixel(terrain,x,y,&v);}
        bind(4,terrain);
        const float histories[]={1,.25f,0,.5f,1,0,.001f,.75f};
        for(unsigned slot:{3u,5u}){Image value=celestialFloatImage(1,1,1);const float v=histories[(which+(slot==5?3:0))%8];celestialPixel(value,0,0,&v);bind(slot,value);}
    }
    void draw(unsigned count=1){ok(d->BeginScene());for(unsigned n=0;n<count;++n)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());}
    std::vector<unsigned char> pixels(){
        ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<unsigned char> out(std::size_t(width)*height*bytes);
        for(unsigned y=0;y<height;++y)std::memcpy(out.data()+std::size_t(y)*width*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*bytes);
        ok(staging->UnlockRect());return out;
    }
    struct Capture{std::vector<unsigned char> rgba,coverage;};
    Capture capture(IDirect3DPixelShader9* shader,const CelestialConstants& c,bool blend=false){
        ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetPixelShader(solid));
        const float background[]={-.5f,3.25f,.125f,c[46][0]>.5f?-0.f:-.25f};
        ok(d->SetPixelShaderConstantF(0,background,1));draw();
        ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));ok(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,0));
        ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
        ok(d->SetRenderState(D3DRS_STENCILREF,1));ok(d->SetRenderState(D3DRS_STENCILMASK,255));
        ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));
        ok(d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->SetRenderState(D3DRS_SRCBLEND,c[12][3]>.5f?D3DBLEND_INVDESTCOLOR:D3DBLEND_SRCALPHA));
        ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
        ok(d->SetPixelShader(shader));ok(d->SetPixelShaderConstantF(0,c[0].data(),49));draw();
        Capture result;result.rgba=pixels();
        // Coverage is measured independently of any shader output or background coincidence.
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,0));
        ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
        const float white[]={1,1,1,1};ok(d->SetPixelShader(solid));ok(d->SetPixelShaderConstantF(0,white,1));
        // Read each stencil sample separately: equal resolved coverage counts could hide a moved sample.
        for(unsigned sample=0;sample<sampleCount;++sample){
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,1u<<sample));draw();
            auto mask=pixels();result.coverage.insert(result.coverage.end(),mask.begin(),mask.end());
        }
        ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));return result;
    }
    void benchmark(IDirect3DPixelShader9* const (&shaders)[2],const CelestialConstants& c,unsigned entry,unsigned sceneId){
        std::vector<double> times[2];
        ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_SRCBLEND,entry||c[12][3]>.5f?D3DBLEND_INVDESTCOLOR:D3DBLEND_SRCALPHA));
        ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
        ok(d->SetPixelShaderConstantF(0,c[0].data(),49));
        constexpr unsigned draws=16;
        for(unsigned block=0;block<24;++block){
            const unsigned mode=(block/2)%2?1-block%2:block%2; // ABBA; discard two complete warm-up groups
            ok(d->SetPixelShader(shaders[mode]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00314217,1,0));draw(4);wait(event);
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(draws);
            ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
            UINT64 a=0,b=0,f=0;BOOL bad=FALSE;
            assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&
                   frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);
            if(block>=8)times[mode].push_back(double(b-a)/double(f)*1000/draws);
        }
        double medians[2]{};for(unsigned m=0;m<2;++m){assert(times[m].size()==8);std::sort(times[m].begin(),times[m].end());medians[m]=(times[m][3]+times[m][4])*.5;}
        std::printf("{\"type\":\"timing\",\"entry\":%u,\"scene\":%u,\"body\":%u,\"glare\":%u,\"glow_mode\":%u,\"linear\":%u,\"width\":%u,\"height\":%u,\"old_ms\":%.9f,\"new_ms\":%.9f,\"ratio\":%.6f,\"measured_blocks_per_shader\":8,\"draws_per_block\":16}\n",
                    entry,sceneId,c[11][3]>.5f?0u:1u,unsigned(c[12][3]),unsigned(c[46][3]),unsigned(c[10][0]),width,height,medians[0],medians[1],medians[1]/medians[0]);std::fflush(stdout);
    }
    ~CelestialFixture(){clearInputs();d->SetPixelShader(nullptr);d->SetDepthStencilSurface(nullptr);for(auto* q:{event,start,end,frequency,disjoint})q->Release();solid->Release();stencil->Release();staging->Release();resolved->Release();target->Release();}
};

struct CelestialTotals{std::size_t cases=0,written=0,discarded=0,negativeRgb=0,negativeWrittenRgb=0,negativeAlpha=0,signedZeroAlpha=0;};
static void celestialCompare(CelestialFixture& fixture,IDirect3DPixelShader9* const (&shaders)[2],
                             const CelestialConstants& c,CelestialTotals& total,unsigned entry,unsigned scene,bool blend=false){
    auto before=fixture.capture(shaders[0],c,blend),after=fixture.capture(shaders[1],c,blend);
    std::size_t rgba=0,coverage=0,alpha=0;
    assert(before.rgba.size()==after.rgba.size()&&before.coverage.size()==after.coverage.size());
    for(std::size_t i=0;i<before.rgba.size();++i){rgba+=before.rgba[i]!=after.rgba[i];alpha+=i%fixture.bytes>=fixture.bytes*3/4&&before.rgba[i]!=after.rgba[i];}
    for(std::size_t i=0;i<before.coverage.size();++i)coverage+=before.coverage[i]!=after.coverage[i];
    for(std::size_t p=0;p<std::size_t(fixture.width)*fixture.height;++p){
        bool covered=false;
        for(unsigned sample=0;sample<fixture.sampleCount;++sample){
            const auto component=(std::size_t(sample)*fixture.width*fixture.height+p)*4;
            const float mask=celestialValue(before.coverage,component,fixture.bits);
            assert(mask==0||mask==1.f/fixture.sampleCount);
            total.written+=mask>0;total.discarded+=mask==0;covered|=mask>0;
        }
        for(unsigned k=0;k<4;++k){
            const float a=celestialValue(before.rgba,p*4+k,fixture.bits),b=celestialValue(after.rgba,p*4+k,fixture.bits);
            assert(std::isfinite(a)&&std::isfinite(b));
            if(k<3){total.negativeRgb+=a<0;total.negativeWrittenRgb+=a<0&&covered;}
            else{total.negativeAlpha+=a<0;total.signedZeroAlpha+=a==0&&std::signbit(a);}
        }
    }
    if(rgba||coverage){std::printf("FAIL celestial entry=%u scene=%u body=%g glare=%g mode=%g linear=%g blend=%u %ux%u bits=%u changed_bytes=%llu alpha_bytes=%llu coverage_bytes=%llu\n",
        entry,scene,c[11][3],c[12][3],c[46][3],c[10][0],unsigned(blend),fixture.width,fixture.height,fixture.bits,
        static_cast<unsigned long long>(rgba),static_cast<unsigned long long>(alpha),static_cast<unsigned long long>(coverage));std::fflush(stdout);}
    assert(!rgba&&!coverage&&!alpha);++total.cases;
}

int main(int argc,char** argv){
    // Four binaries are mandatory; no production generated header supplies either side.
    assert(argc==6||argc==9);const bool benchmark=argc==9;
    if(benchmark)assert(std::string(argv[6])=="--benchmark");
    const unsigned benchWidth=benchmark?std::strtoul(argv[7],nullptr,10):0,benchHeight=benchmark?std::strtoul(argv[8],nullptr,10):0;
    if(benchmark)assert(benchWidth>=256&&benchHeight>=256);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCelestialExactPair";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[5]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2][2]{};
    for(unsigned entry=0;entry<2;++entry)for(unsigned side=0;side<2;++side){auto bytes=read(argv[1+entry*2+side]);assert(bytes.size()>=8&&bytes.size()%4==0);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&shaders[entry][side]));}
    CelestialTotals totals;unsigned targets=0,unsupportedMsaa=0;
    for(unsigned bits:{16u,32u})for(unsigned odd:{0u,1u})for(unsigned samples:{0u,2u}){
        if(odd&&samples)continue;
        DWORD colorQuality=0,depthQuality=0;const auto format=bits==16?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F;
        if(samples&&(FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,format,TRUE,D3DMULTISAMPLE_TYPE(samples),&colorQuality))||
                     FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,D3DMULTISAMPLE_TYPE(samples),&depthQuality))||!colorQuality||!depthQuality)){++unsupportedMsaa;continue;}
        CelestialFixture fixture(d,odd?137:128,odd?79:96,bits,samples);++targets;
        for(unsigned scene=0;scene<8;++scene){fixture.scene(scene);
            for(unsigned entry=0;entry<2;++entry)for(unsigned body=0;body<2;++body)for(unsigned glare=0;glare<2;++glare)
            for(unsigned mode=0;mode<3;++mode)for(unsigned linear=0;linear<2;++linear){
                auto c=celestialConstants(fixture.width,fixture.height,body,glare,mode,linear,scene);
                celestialCompare(fixture,shaders[entry],c,totals,entry,scene);
                // Production blend against signed HDR is an additional check, separate from raw alpha/coverage.
                if(scene==7&&mode==0)celestialCompare(fixture,shaders[entry],c,totals,entry,scene,true);
            }
        }
    }
    assert(targets>=4&&totals.written&&totals.discarded&&totals.negativeRgb&&totals.negativeWrittenRgb&&totals.negativeAlpha&&totals.signedZeroAlpha);
    std::printf("{\"type\":\"equivalence\",\"cases\":%llu,\"targets\":%u,\"unsupported_msaa_targets\":%u,\"written_samples\":%llu,\"discarded_samples\":%llu,\"negative_rgb_components\":%llu,\"negative_written_rgb_components\":%llu,\"negative_alpha_components\":%llu,\"signed_zero_alpha_components\":%llu,\"changed_rgba_bytes\":0,\"changed_coverage_bytes\":0}\n",
        static_cast<unsigned long long>(totals.cases),targets,unsupportedMsaa,static_cast<unsigned long long>(totals.written),
        static_cast<unsigned long long>(totals.discarded),static_cast<unsigned long long>(totals.negativeRgb),static_cast<unsigned long long>(totals.negativeWrittenRgb),
        static_cast<unsigned long long>(totals.negativeAlpha),static_cast<unsigned long long>(totals.signedZeroAlpha));std::fflush(stdout);
    if(benchmark){
        CelestialFixture fixture(d,benchWidth,benchHeight,16,0);
        for(unsigned scene:{0u,1u}){fixture.scene(scene);
            for(unsigned entry=0;entry<2;++entry)for(unsigned body=0;body<2;++body)for(unsigned linear=0;linear<2;++linear){
                auto c=celestialConstants(benchWidth,benchHeight,body,1,0,linear,scene,true);
                celestialCompare(fixture,shaders[entry],c,totals,entry,scene);
                fixture.benchmark(shaders[entry],c,entry,scene);
            }
        }
        fixture.scene(1);for(unsigned linear=0;linear<2;++linear){auto c=celestialConstants(benchWidth,benchHeight,0,0,1,linear,1,true);celestialCompare(fixture,shaders[0],c,totals,0,1);fixture.benchmark(shaders[0],c,0,1);}
        for(unsigned linear=0;linear<2;++linear){auto c=celestialConstants(benchWidth,benchHeight,0,1,1,linear,1,true);celestialCompare(fixture,shaders[0],c,totals,0,1);fixture.benchmark(shaders[0],c,0,1);}
    }
    std::printf("CELESTIAL_EXACT_PAIR_PASS cases=%llu game_launched=false\n",static_cast<unsigned long long>(totals.cases));
    d->SetPixelShader(nullptr);for(auto& entry:shaders)for(auto* p:entry)p->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
