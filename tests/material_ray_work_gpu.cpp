// Procedural texture, production material rays, full-work reference and paired GPU timestamps.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <algorithm>
#include <chrono>

static constexpr unsigned W=960,H=540;
static void waitFor(IDirect3DQuery9* query){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    HRESULT hr;while((hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(std::chrono::steady_clock::now()<deadline);Sleep(0);}ok(hr);
}
static std::vector<float> readPixels(IDirect3DDevice9* d,IDirect3DSurface9* target){
    IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<float> values(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(values.data()+y*W*4,(char*)lock.pBits+y*lock.Pitch,W*16);
    stage->UnlockRect();stage->Release();return values;
}
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=std::string(argv[2])+"/";
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialRayWork";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    IDirect3DTexture9* height=nullptr;ok(d->CreateTexture(64,64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT lock{};ok(height->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){auto* p=(unsigned char*)lock.pBits+y*lock.Pitch+x*4;p[0]=static_cast<unsigned char>((x/4+y/4)%2?230:26);p[1]=p[2]=128;p[3]=255;}
    ok(height->UnlockRect(0));ok(d->SetTexture(0,height));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,key,D3DTEXF_LINEAR));
    ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP));
    IDirect3DPixelShader9* shaders[6]={};const char* names[]={"ReferenceRay","Ray","ReferenceShadow","Shadow","ReferenceZeroRay","ZeroRay"};
    for(unsigned i=0;i<6;++i){auto bytes=read(folder+names[i]+".bin");ok(d->CreatePixelShader((DWORD*)bytes.data(),&shaders[i]));}
    struct Vertex{float x,y,z,w,u,v,px,py,pz,pw;};
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX2|D3DFVF_TEXCOORDSIZE4(1)));
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    std::ofstream report(folder+"material-ray-work.json");report<<"[\n";
    unsigned cases=0;
    for(unsigned kind=0;kind<3;++kind)for(unsigned test=0;test<(kind==2?4u:kind?9u:42u);++test){
        float c[3][4]={{1,1,-1,0},{.018f,16,16,0},{0,8,0,0}};
        const float distance=kind?2.f:test<3?15.999f:test<6?16.f:test<9?16.001f:test<12?2.f:test<15?8.f:1.f;
        const unsigned steps[]={8,12,16,32};c[1][2]=float(steps[test%4]);c[2][0]=kind?float(test/3)*.45f:0;c[2][1]=float(4u<<(test%3));
        if(kind&&test>=6)c[2][0]=.99f;
        if(kind&&test==0){ok(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)static_cast<unsigned char*>(lock.pBits)[y*lock.Pitch+x*4]=static_cast<unsigned char>((x/4+y/4)%2?230:26);ok(height->UnlockRect(0));}
        if(kind==2&&test==0){ok(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)static_cast<unsigned char*>(lock.pBits)[y*lock.Pitch+x*4]=static_cast<unsigned char>((x/4+y/4)%2?255:0);ok(height->UnlockRect(0));}
        if(!kind&&test>=9)c[1][0]=.036f;
        if(!kind&&test>=33){ok(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)static_cast<unsigned char*>(lock.pBits)[y*lock.Pitch+x*4]=test<37?255:0;ok(height->UnlockRect(0));}
        // Signed-origin round-trip counterexample: retain the independent full-tap reference.
        if(!kind&&test==41){c[1][2]=8;ok(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)static_cast<unsigned char*>(lock.pBits)[y*lock.Pitch+x*4]=static_cast<unsigned char>((x/4+y/4)%2?230:26);ok(height->UnlockRect(0));}
        const auto address=!kind&&test>=9&&test%2&&test!=41?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP;ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,address));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSV,address));
        const float scale=!kind&&test==41?.7f+8*.43f:!kind&&test>=9?test%3==0?1.f:test%3==1?4.f:7.9f:1.f;
        if((!kind&&test>=17)||kind==2)c[0][2]=-.1f;
        if(kind==2)c[1][0]=.036f;
        for(unsigned i=0;i<4;++i){const float u=i&1?1.f:0.f,v=i&2?1.f:0.f;quad[i].u=u*scale+(!kind&&test==41?67*.013579f:!kind&&test>=9?.99999f:0);quad[i].v=v*scale+(!kind&&test==41?67*.019137f:-.00001f);quad[i].px=u*2-1+((!kind&&test>=17)||kind==2?4.f:0);quad[i].py=v*2-1;quad[i].pw=distance+(u-.5f)*.004f;}
        ok(d->SetPixelShaderConstantF(0,c[0],3));std::vector<float> reference;float error=0;
        for(unsigned mode=0;mode<2;++mode){ok(d->SetPixelShader(shaders[kind*2+mode]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());auto values=readPixels(d,target);
            if(!mode)reference=std::move(values);else for(unsigned i=0;i<values.size();++i){assert(std::isfinite(values[i])&&std::isfinite(reference[i]));error=std::max(error,std::fabs(values[i]-reference[i]));}}
        assert(error<=.00001f);
        if(kind==1&&test<3)assert(*std::min_element(reference.begin(),reference.end())<.01f);
        if(kind==1&&test>=6)assert(*std::min_element(reference.begin(),reference.end())>.99f);
        if(kind==2){unsigned selected=0;for(unsigned i=3;i<reference.size();i+=4)selected+=reference[i]>.5f;assert(selected>0);}
        std::vector<double> timings[2];
        // Interleave the two variants so clock/power drift does not select one side.
        for(unsigned block=0;block<10;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;ok(d->SetPixelShader(shaders[kind*2+mode]));
            ok(d->BeginScene());ok(disjoint->Issue(D3DISSUE_BEGIN));ok(freq->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
            for(unsigned n=0;n<8;++n)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(event->Issue(D3DISSUE_END));ok(d->EndScene());waitFor(event);
            UINT64 a=0,b=0,f=0;BOOL bad=FALSE;ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(freq->GetData(&f,sizeof f,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&f);
            if(block>=3)timings[mode].push_back(double(b-a)/f*1000/8);
        }
        for(auto& times:timings)std::sort(times.begin(),times.end());const double before=timings[0][timings[0].size()/2],after=timings[1][timings[1].size()/2];
        if(cases++)report<<",\n";report<<"{\"kind\":\""<<(kind==2?"zero":kind?"shadow":"fade")<<"\",\"case\":"<<test<<",\"max_error\":"<<error<<",\"reference_ms\":"<<before<<",\"optimized_ms\":"<<after<<"}";
        std::printf("MATERIAL_RAY_WORK kind=%u case=%u maxError=%.9g referenceMs=%.6f optimizedMs=%.6f\n",kind,test,error,before,after);std::fflush(stdout);
    }
    report<<"\n]\n";report.close();
    for(auto* shader:shaders)shader->Release();for(auto* query:{event,start,end,freq,disjoint})query->Release();height->Release();target->Release();d->Release();api->Release();DestroyWindow(window);
}
