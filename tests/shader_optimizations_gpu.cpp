// Frozen/current production bytecode oracle. Hidden offscreen device; no client DLL.
#define main northlight_unused_composite_helpers
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <sstream>

struct OptimizationVertex { float x,y,z,w; DWORD color; float u,v,dx,dy,flow[4],state[4]; };
static IDirect3DTexture9* optimizationMips(IDirect3DDevice9* d,Image im){
    assert(im.format==D3DFMT_A32B32G32R32F);IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(im.width,im.height,0,0,D3DFORMAT(im.format),D3DPOOL_MANAGED,&t,nullptr));
    for(unsigned level=0;level<t->GetLevelCount();++level){D3DLOCKED_RECT lock{};ok(t->LockRect(level,&lock,nullptr,0));
        for(unsigned y=0;y<im.height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,im.pixels.data()+size_t(y)*im.width*16,im.width*16);ok(t->UnlockRect(level));
        Image next{std::max(1u,im.width/2),std::max(1u,im.height/2),im.format,16,{}};next.pixels.resize(size_t(next.width)*next.height*16);
        for(unsigned y=0;y<next.height;++y)for(unsigned x=0;x<next.width;++x){float value[4]{};for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){float p[4];std::memcpy(p,im.pixels.data()+(size_t(std::min(y*2+j,im.height-1))*im.width+std::min(x*2+i,im.width-1))*16,16);for(unsigned k=0;k<4;++k)value[k]+=p[k]*.25f;}std::memcpy(next.pixels.data()+(size_t(y)*next.width+x)*16,value,16);}im=std::move(next);
    }return t;
}
static IDirect3DCubeTexture9* optimizationCube(IDirect3DDevice9* d,const Image& im){
    assert(im.width==im.height);IDirect3DCubeTexture9* t=nullptr;
    ok(d->CreateCubeTexture(im.width,1,0,D3DFORMAT(im.format),D3DPOOL_MANAGED,&t,nullptr));
    for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};ok(t->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));
        for(unsigned y=0;y<im.height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,im.pixels.data()+size_t(y)*im.width*im.bytes,im.width*im.bytes);
        ok(t->UnlockRect(D3DCUBEMAP_FACES(face),0));}return t;
}
int main(int argc,char** argv){
    assert(argc==5); // case list, frozen binary directory, current directory, DXVK DLL
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightOptimizationOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[4]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));
    ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX4|D3DFVF_TEXCOORDSIZE4(2)|D3DFVF_TEXCOORDSIZE4(3)));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    std::ifstream list(argv[1]);assert(list);std::string entry,folder;unsigned jobs=0,failures=0,samples=0;
    while(list>>entry>>folder>>samples){
        const auto raw=read(folder+"/params.bin");assert(raw.size()==32+128*16+32);
        std::uint32_t h[8];std::memcpy(h,raw.data(),32);const unsigned W=h[0],H=h[1];assert(samples==0||samples==4);
        float c[128][4],flow[4],state[4];std::memcpy(c,raw.data()+32,sizeof c);std::memcpy(flow,raw.data()+32+sizeof c,16);std::memcpy(state,raw.data()+48+sizeof c,16);
        std::vector<IDirect3DBaseTexture9*> owned;
        for(unsigned s=0;s<16;++s){const auto im=image(folder+"/"+std::to_string(s)+".fgr");IDirect3DBaseTexture9* t=s==h[4]?static_cast<IDirect3DBaseTexture9*>(optimizationCube(d,im)):h[7]&(1u<<s)?optimizationMips(d,im):texture(d,im);owned.push_back(t);ok(d->SetTexture(s,t));
            for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,key,h[6]&(1u<<s)?D3DTEXF_LINEAR:D3DTEXF_POINT));
            ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,h[7]&(1u<<s)?D3DTEXF_LINEAR:D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,key,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
        IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto bytes=read(std::string(argv[i+2])+"/"+entry+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&shaders[i]));}
        IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
        ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
        if(samples)ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));else{resolved=target;resolved->AddRef();}
        ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
        ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,h[3]));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,h[5]));
        OptimizationVertex quad[4]{};for(unsigned i=0;i<4;++i){auto& v=quad[i];v.x=(i&1?float(W):0)-.5f;v.y=(i&2?float(H):0)-.5f;v.z=0;v.w=1;v.color=0xBF8F6F3F;v.u=i&1?1:0;v.v=i&2?1:0;v.dx=(v.u-.5f)*.24f;v.dy=(v.v-.5f)*.24f;std::memcpy(v.flow,flow,16);std::memcpy(v.state,state,16);}
        std::vector<unsigned char> output[2];double median[2]{};
        IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        std::vector<double> timings[2];
        const bool clearCandidate=entry=="ContactBloom"&&c[1][3]==0&&c[2][0]==0;
        for(unsigned block=0;block<12;++block){unsigned variant=(block/2)&1?1-block%2:block%2;ok(d->SetPixelShader(shaders[variant]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x406080a0,1,0));
            const bool neutralClear=variant&&clearCandidate;
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));ok(d->BeginScene());for(unsigned repeat=0;repeat<8;++repeat){
                if(neutralClear)ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
                else ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(OptimizationVertex)));
            }ok(d->EndScene());ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
            UINT64 a=0,b=0,f=0;BOOL bad=FALSE;ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&f,sizeof f,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&f);
            if(block>=4)timings[variant].push_back(double(b-a)*1000/double(f)/8);
            if(block<2){if(samples)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[variant].resize(size_t(W)*H*16);for(unsigned y=0;y<H;++y)std::memcpy(output[variant].data()+size_t(y)*W*16,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*16);ok(staging->UnlockRect());}}
        for(unsigned v=0;v<2;++v){std::sort(timings[v].begin(),timings[v].end());median[v]=(timings[v][1]+timings[v][2])*.5;}
        size_t changed=0,big=0,nonfinite=0;double maxAbs=0,sum=0;for(size_t i=0;i<size_t(W)*H*4;++i){float a,b;std::memcpy(&a,output[0].data()+i*4,4);std::memcpy(&b,output[1].data()+i*4,4);double error=std::fabs(double(a)-b);changed+=a!=b;nonfinite+=!std::isfinite(a)||!std::isfinite(b);maxAbs=std::max(maxAbs,error);sum+=error*error;big+=error>1e-5+std::fabs(a)*1e-5;}
        std::printf("{\"entry\":\"%s\",\"case\":\"%s\",\"candidate_operation\":\"%s\",\"samples\":%u,\"pixels\":%u,\"changed\":%llu,\"over_tolerance\":%llu,\"nonfinite\":%llu,\"max_abs\":%.9g,\"rmse\":%.9g,\"before_ms\":%.7f,\"after_ms\":%.7f}\n",entry.c_str(),folder.c_str(),clearCandidate?"neutral-clear":"shader-draw",samples,W*H,static_cast<unsigned long long>(changed),static_cast<unsigned long long>(big),static_cast<unsigned long long>(nonfinite),maxAbs,std::sqrt(sum/(size_t(W)*H*4)),median[0],median[1]);std::fflush(stdout);failures+=big||nonfinite;++jobs;
        for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,nullptr));owned[s]->Release();}for(auto* q:{event,start,end,frequency,disjoint})q->Release();for(auto* shader:shaders)shader->Release();staging->Release();resolved->Release();target->Release();
    }
    std::printf("SHADER_OPTIMIZATIONS_GPU_%s jobs=%u failures=%u\n",failures?"FAIL":"PASS",jobs,failures);
    d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
