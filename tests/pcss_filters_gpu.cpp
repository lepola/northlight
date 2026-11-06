// Authored depth maps plus the real local-light submission path. No client assets.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
static void pcOk(HRESULT hr){assert(SUCCEEDED(hr));}
static float pcHalf(uint16_t h){unsigned e=(h>>10)&31,m=h&1023;float v=e==31?(m?NAN:INFINITY):(e?std::ldexp(float(1024+m),int(e)-25):std::ldexp(float(m),-24));return h&32768?-v:v;}
static IDirect3DTexture9* pcTexture(IDirect3DDevice9* d,unsigned width,unsigned height,unsigned components,const std::vector<float>& data){
    IDirect3DTexture9* t=nullptr;pcOk(d->CreateTexture(width,height,1,0,components==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&t,nullptr));
    D3DLOCKED_RECT lock{};pcOk(t->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,data.data()+y*width*components,width*components*4);pcOk(t->UnlockRect(0));return t;
}
static std::vector<float> pcRead(IDirect3DDevice9* d,IDirect3DSurface9* surface){
    D3DSURFACE_DESC desc{};pcOk(surface->GetDesc(&desc));IDirect3DSurface9* stage=nullptr;pcOk(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));pcOk(d->GetRenderTargetData(surface,stage));
    D3DLOCKED_RECT lock{};pcOk(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> values(desc.Width*desc.Height);
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x){const char* row=static_cast<const char*>(lock.pBits)+y*lock.Pitch;
        values[y*desc.Width+x]=desc.Format==D3DFMT_A16B16G16R16F?pcHalf(reinterpret_cast<const uint16_t*>(row)[x*4]):reinterpret_cast<const float*>(row)[x*(desc.Format==D3DFMT_R32F?1:4)];}
    pcOk(stage->UnlockRect());stage->Release();return values;
}
struct WorldScreenEffectsTest {
    template<class Draw> static void timing(IDirect3DDevice9* d,const char* label,Draw draw){
        IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        pcOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));pcOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));
        pcOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));pcOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        for(unsigned block=0;block<10;++block)for(unsigned order=0;order<2;++order){
            const bool soft=(block+order)%2;
            pcOk(disjoint->Issue(D3DISSUE_BEGIN));pcOk(begin->Issue(D3DISSUE_END));pcOk(d->BeginScene());
            for(unsigned repeat=0;repeat<8;++repeat)draw(soft);
            pcOk(d->EndScene());pcOk(end->Issue(D3DISSUE_END));pcOk(frequency->Issue(D3DISSUE_END));pcOk(disjoint->Issue(D3DISSUE_END));
            auto read=[](IDirect3DQuery9* query,void* value,DWORD size){HRESULT hr;DWORD deadline=GetTickCount()+10000;
                while((hr=query->GetData(value,size,D3DGETDATA_FLUSH))==S_FALSE){assert(LONG(deadline-GetTickCount())>0);Sleep(0);}pcOk(hr);};
            BOOL bad=FALSE;UINT64 a=0,b=0,f=0;read(end,&b,sizeof b);read(begin,&a,sizeof a);read(frequency,&f,sizeof f);read(disjoint,&bad,sizeof bad);assert(!bad&&f&&b>=a);
            if(block>=2)std::printf("PCSS_TIMING pass=%s soft=%u block=%u ms=%.6f\n",label,unsigned(soft),block,double(b-a)*1000/double(f)/8);
        }
        for(auto* q:{begin,end,frequency,disjoint})q->Release();
    }
    static void benchmark(IDirect3DDevice9* d){
        constexpr unsigned W=1920,H=1080;WorldRenderer w(d);w.valid=true;assert(w.resources(W,H,D3DFMT_A8R8G8B8));
        assert(w.softShadowResources(W,H));w.pointIndex=0;auto& slot=w.pointSlot();
        slot.selected.attenuationEnd=64;slot.selected.diffuse[0]=slot.selected.diffuse[1]=slot.selected.diffuse[2]=1;
        assert(w.pointResources()&&w.pointSoftShaders());slot.ready=true;
        for(unsigned face=0;face<6;++face){pcOk(d->SetRenderTarget(0,slot.faces[face]));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));}
        pcOk(d->SetRenderTarget(0,slot.faces[D3DCUBEMAP_FACE_POSITIVE_Z]));D3DRECT halfFace{0,0,128,256};
        pcOk(d->Clear(1,&halfFace,D3DCLEAR_TARGET,D3DCOLOR_COLORVALUE(.97f,.97f,.97f,1),1,0));
        w.projection[0]=w.projection[1]=w.projection[2]=1;for(unsigned k=0;k<4;++k)w.context.inverseView[k*5]=1;
        std::vector<float> normals(W/2*(H/2)*4);for(unsigned i=0;i<W/2*(H/2);++i){normals[i*4+2]=-1;normals[i*4+3]=1;}
        drop(w.normalBuffer);w.normalBuffer=pcTexture(d,W/2,H/2,4,normals);
        auto* depths=pcTexture(d,W,H,1,std::vector<float>(W*H,(64.f/(64-.1f))*(1-.1f/5)));
        pcOk(d->SetRenderTarget(0,w.baselineSurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        pcOk(d->SetRenderTarget(0,w.lightSurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        pcOk(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));pcOk(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
        // Share an authored immutable cube among ready slots. This measures
        // screen filtering/correction only, never cubemap generation or refresh.
        for(unsigned i=1;i<64;++i){w.pointSlots[i].selected=slot.selected;w.pointSlots[i].ready=true;w.pointSlots[i].cube=slot.cube;slot.cube->AddRef();}
        for(unsigned count:{1u,18u,64u}){char label[64];std::snprintf(label,sizeof label,"local_%u_full_screen",count);
            timing(d,label,[&](bool soft){w.quality.softShadows=soft;for(w.pointIndex=0;w.pointIndex<count;++w.pointIndex)assert(w.renderPointLighting(depths,nullptr,W,H,.1f,64,0,1,1,0,1,0));});}
        assert(w.pointDraws==0);w.pointIndex=0;
        for(unsigned sampler=0;sampler<16;++sampler)pcOk(d->SetTexture(sampler,nullptr));
        std::vector<float> map(1024*1024);for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x)map[y*1024+x]=x<512?.4f:1.f;
        auto* shadow=pcTexture(d,1024,1024,1,map);
        float c[79][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=64;
        c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
        for(unsigned k=0;k<4;++k)c[3+k][k]=1;
        c[7][0]=c[8][1]=1.f/48;c[10][2]=.6f;c[10][3]=1;
        c[11][0]=c[12][1]=1.f/192;c[14][2]=.6f;c[14][3]=1;
        c[16][2]=c[28][2]=-1;c[17][0]=c[17][1]=c[17][2]=c[29][0]=c[29][1]=c[29][2]=.6f;
        c[18][0]=c[18][1]=c[18][2]=.2f;c[20][2]=1;c[27][0]=c[27][1]=1;
        c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;c[35][0]=1.f/200;
        pcOk(d->SetPixelShaderConstantF(0,c[0],79));pcOk(d->SetTexture(1,depths));pcOk(d->SetTexture(2,shadow));pcOk(d->SetTexture(3,shadow));
        pcOk(d->SetTexture(12,w.neutralWhite));pcOk(d->SetTexture(14,w.normalBuffer));
        pcOk(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));pcOk(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));pcOk(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
        timing(d,"directional_with_consumer",[&](bool soft){
            pcOk(d->SetTexture(15,nullptr));pcOk(d->SetRenderTarget(1,nullptr));
            if(soft){pcOk(d->SetRenderTarget(0,w.softVisibilitySurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
                pcOk(d->SetPixelShader(w.directionalSoftPS));pcOk(w.quad(W/2,H/2));}
            pcOk(d->SetRenderTarget(0,w.lightSurface));pcOk(d->SetRenderTarget(1,w.baselineSurface));
            pcOk(d->SetTexture(15,soft?w.softVisibility:nullptr));pcOk(d->SetPixelShader(soft?w.lightingSoftPS:w.lightingPS));pcOk(w.quad(W/2,H/2));
        });
        pcOk(d->SetRenderTarget(1,nullptr));for(unsigned sampler=0;sampler<16;++sampler)pcOk(d->SetTexture(sampler,nullptr));
        depths->Release();shadow->Release();
    }
    static void run(IDirect3DDevice9* d,const char* probeFile){
        WorldRenderer w(d);w.valid=true;assert(w.resources(128,128,D3DFMT_A8R8G8B8));
        pcOk(d->SetDepthStencilSurface(nullptr));pcOk(d->SetVertexShader(nullptr));pcOk(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})pcOk(d->SetRenderState(state,FALSE));
        pcOk(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));pcOk(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        for(unsigned sampler=0;sampler<16;++sampler){pcOk(d->SetTexture(sampler,nullptr));
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})pcOk(d->SetSamplerState(sampler,state,D3DTEXF_POINT));
            pcOk(d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE));pcOk(d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));pcOk(d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
        std::ifstream input(probeFile,std::ios::binary);std::vector<char> code{std::istreambuf_iterator<char>(input),{}};assert(!code.empty());
        IDirect3DPixelShader9* probe=nullptr;pcOk(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&probe));
        IDirect3DSurface9* target=nullptr;pcOk(d->CreateRenderTarget(256,32,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));pcOk(d->SetRenderTarget(0,target));
        unsigned cases=0;for(bool useNear:{false,true})for(float slope:{0.f,.015f})for(float gap:{.2f,20.f,40.f}){
            std::vector<float> map(1024*1024);for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x)map[y*1024+x]=x<512?.6f-gap/200+slope*((x+.5f)/1024-.5f):1.f;
            auto* texture=pcTexture(d,1024,1024,1,map);pcOk(d->SetTexture(2,texture));pcOk(d->SetTexture(3,texture));
            float c[81][4]{};c[2][3]=1.f/1024;c[35][0]=1.f/200;c[80][0]=slope;c[80][2]=useNear?1.f:0.f;
            std::vector<float> images[2];unsigned widths[2]{};
            for(unsigned soft=0;soft<2;++soft){c[80][3]=float(soft);pcOk(d->SetPixelShaderConstantF(0,c[0],81));pcOk(d->SetPixelShader(probe));pcOk(d->BeginScene());pcOk(w.quad(256,32));pcOk(d->EndScene());images[soft]=pcRead(d,target);
                for(unsigned x=0;x<256;++x){float v=images[soft][16*256+x];assert(std::isfinite(v)&&v>=0&&v<=1);widths[soft]+=v>.01f&&v<.99f;}
                assert(images[soft][16*256+15]<.001f&&images[soft][16*256+240]>.999f);
            }
            if(gap==.2f)assert(images[0]==images[1]);else assert(widths[1]>=widths[0]);
            if(gap==40)assert(widths[1]>widths[0]);
            std::printf("PCSS_EDGE near=%u slope=%.3f gap=%.1f pcf=%u pcss=%u\n",unsigned(useNear),slope,gap,widths[0],widths[1]);++cases;
            pcOk(d->SetTexture(2,nullptr));pcOk(d->SetTexture(3,nullptr));texture->Release();
        }
        probe->Release();pcOk(d->SetRenderTarget(0,w.lightSurface));target->Release();
        // Real per-source path: optional resolve must preserve the cube and RT,
        // select the right consumer, and survive on/off/on without stale masks.
        w.pointIndex=0;auto& slot=w.pointSlot();slot.selected.attenuationEnd=64;slot.selected.diffuse[0]=slot.selected.diffuse[1]=slot.selected.diffuse[2]=1;
        assert(w.pointResources());slot.ready=true;
        for(unsigned face=0;face<6;++face){pcOk(d->SetRenderTarget(0,slot.faces[face]));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));}
        // An actual opaque depth on half the +Z face; enough penumbra pixels.
        pcOk(d->SetRenderTarget(0,slot.faces[D3DCUBEMAP_FACE_POSITIVE_Z]));D3DRECT halfFace{0,0,128,256};
        pcOk(d->Clear(1,&halfFace,D3DCLEAR_TARGET,D3DCOLOR_COLORVALUE(.97f,.97f,.97f,1),1,0));
        const auto cubeBefore=pcRead(d,slot.faces[D3DCUBEMAP_FACE_POSITIVE_Z]);
        w.projection[0]=w.projection[1]=w.projection[2]=1;for(unsigned k=0;k<4;++k)w.context.inverseView[k*5]=1;
        std::vector<float> normals(64*64*4);for(unsigned i=0;i<64*64;++i){normals[i*4+2]=-1;normals[i*4+3]=1;}
        drop(w.normalBuffer);w.normalBuffer=pcTexture(d,64,64,4,normals);
        const float depth=(64.f/(64-.1f))*(1-.1f/5);auto* depths=pcTexture(d,128,128,1,std::vector<float>(128*128,depth));
        pcOk(d->SetRenderTarget(0,w.baselineSurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        pcOk(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));pcOk(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));pcOk(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        std::vector<float> previous[2];unsigned different=0;
        for(unsigned iteration=0;iteration<4;++iteration){unsigned soft=iteration%2;w.quality.softShadows=soft;pcOk(d->SetRenderTarget(0,w.lightSurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            pcOk(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,128,128,.1f,64,0,1,1,0,1,0));pcOk(d->EndScene());
            IDirect3DSurface9* current=nullptr;pcOk(d->GetRenderTarget(0,&current));assert(current==w.lightSurface);current->Release();assert(slot.ready&&w.pointDraws==0);
            auto pixels=pcRead(d,w.lightSurface);for(float v:pixels)assert(std::isfinite(v)&&v<=.0001f&&v>=-1.001f);
            if(iteration<2)previous[soft]=pixels;else assert(previous[soft]==pixels);
            if(soft){assert(w.pointSoftResolvePS&&w.softVisibility);for(unsigned i=0;i<pixels.size();++i)different+=std::fabs(pixels[i]-previous[0][i])>.0001f;}
        }
        assert(different>0&&cubeBefore==pcRead(d,slot.faces[D3DCUBEMAP_FACE_POSITIVE_Z]));
        // Live Off drops only the optional scratch and invalidates lighting
        // history. Actor/fog maps and completed cube publication stay valid.
        auto* actor=w.actorShadowPS;auto* tiles=w.actorTilesPS;auto* cube=slot.cube;
        NorthlightSettings::Config config;config.quality=w.quality;config.celestial=w.celestialProfiles;
        config.quality.softShadows=0;w.temporalValid=true;w.applyLiveSettings(config);
        assert(!w.softVisibility&&!w.temporalValid&&w.actorShadowPS==actor&&w.actorTilesPS==tiles&&slot.ready&&slot.cube==cube);
        config.quality.softShadows=1;w.applyLiveSettings(config);
        // Simulate either optional allocation backoff or unavailable shaders:
        // both must reproduce ordinary PCF without invalidating the cube.
        for(unsigned failure=0;failure<2;++failure){
            if(failure==0)w.softShadowRetryAt=GetTickCount()+1000;
            else{w.softShadowRetryAt=0;drop(w.pointSoftResolvePS);w.pointSoftTried=true;}
            pcOk(d->SetRenderTarget(0,w.lightSurface));pcOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            pcOk(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,128,128,.1f,64,0,1,1,0,1,0));pcOk(d->EndScene());
            assert(pcRead(d,w.lightSurface)==previous[0]&&slot.ready&&slot.cube==cube&&w.pointDraws==0);
        }
        std::printf("PCSS_PIPELINE_PASS edgeCases=%u localChanged=%u cubeUnchanged=1 toggleStable=1 targetRestored=1\n",cases,different);
        std::puts("PCSS_FALLBACK_PASS allocationBackoff=1 shaderUnavailable=1 liveOff=1 historyInvalidated=1 actorMapsRetained=1 cubeRetained=1");
        for(unsigned sampler=0;sampler<16;++sampler)d->SetTexture(sampler,nullptr);depths->Release();
    }
};
int main(int argc,char** argv){
    assert(argc==3||argc==4);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"PCSSFilters";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* device=nullptr;pcOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    D3DADAPTER_IDENTIFIER9 adapter{};pcOk(api->GetAdapterIdentifier(0,0,&adapter));std::printf("PCSS_ADAPTER name=%s vendor=%04lx device=%04lx\n",adapter.Description,(unsigned long)adapter.VendorId,(unsigned long)adapter.DeviceId);
    WorldScreenEffectsTest::run(device,argv[2]);if(argc==4)WorldScreenEffectsTest::benchmark(device);device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
