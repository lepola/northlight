// Hidden-device issue #106 oracle: production AO, preparation and bilateral blur.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <iomanip>
#include "ao_actor_policy.h"

template<class T> struct ActorCom {
    T* p=nullptr;~ActorCom(){if(p)p->Release();}T** out(){assert(!p);return &p;}
    T* operator->()const{return p;}operator T*()const{return p;}
};
static void store(const std::string& path,const std::vector<float>& data){
    std::ofstream f(path,std::ios::binary);assert(f);
    f.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(float));assert(f);
}
static IDirect3DPixelShader9* actorShader(IDirect3DDevice9* d,const std::string& dir,const char* name){
    auto bytes=read(dir+"/"+name+".bin");IDirect3DPixelShader9* ps=nullptr;
    ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps));return ps;
}
int main(int argc,char** argv){
    assert(argc==5||argc==6);SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    const std::string compiled=argv[1],backend=argv[2];
    assert(backend.find(":")!=std::string::npos); // Never search for the installed proxy.
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightActorAOOracle";wc.hInstance=GetModuleHandleW(nullptr);
    assert(RegisterClassW(&wc));HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(backend.c_str());assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    ActorCom<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);assert(api.p);
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=32;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ActorCom<IDirect3DDevice9> d;ok(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,d.out()));
    ActorCom<IDirect3DPixelShader9> baseline[2],candidate[2],alpha[2],prepare,blur,legacy;
    baseline[0].p=actorShader(d,compiled,"HBAOPlusContactBloom");baseline[1].p=actorShader(d,compiled,"GTAOContactBloom");
    candidate[0].p=actorShader(d,compiled,"HBAOActorContactBloom");candidate[1].p=actorShader(d,compiled,"GTAOActorContactBloom");
    prepare.p=actorShader(d,compiled,"AOActorPrepare");blur.p=actorShader(d,compiled,"AOBlur");
    legacy.p=actorShader(d,compiled,"AO");
    alpha[0].p=actorShader(d,compiled,"HBAOActorOnly");alpha[1].p=actorShader(d,compiled,"GTAOActorOnly");
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
                    D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetVertexShader(nullptr));
    ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(unsigned s=0;s<5;++s){
        ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        ok(d->SetSamplerState(s,D3DSAMP_MINFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));ok(d->SetSamplerState(s,D3DSAMP_MAGFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));
        ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));
    }
    std::ifstream cases(argv[3]);assert(cases);std::ofstream report(argv[4]);assert(report);unsigned count=0;
    std::string prefix;unsigned w,h;float radius,nearZ,farZ,partition,sx,sy,strength;
    while(cases>>std::quoted(prefix)>>w>>h>>radius>>nearZ>>farZ>>partition>>sx>>sy>>strength){
        const unsigned aw=w/2,ah=h/2;assert(aw&&ah);
        Image dep{w,h,D3DFMT_R32F,4,read(prefix+".depth.f32")},owner{w,h,D3DFMT_R32F,4,read(prefix+".actor.f32")};
        assert(dep.pixels.size()==std::size_t(w)*h*4&&owner.pixels.size()==dep.pixels.size());
        Image sc{w,h,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(std::size_t(w)*h*4,255)};
        // Nonuniform scene RGB exercises the real bloom/bounce sampling. A
        // constant white scene would hide UV or discarded-RGB regressions.
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const std::size_t index=std::size_t(y)*w+x;float actor=0;
            std::memcpy(&actor,owner.pixels.data()+index*4,4);
            const bool bright=((x/37+y/29)&1)!=0;
            sc.pixels[index*4]=actor>0?70:bright?250:100;
            sc.pixels[index*4+1]=actor>0?55:bright?235:120;
            sc.pixels[index*4+2]=actor>0?90:bright?255:160;
        }
        ActorCom<IDirect3DTexture9> depth,mask,scene,normal,raw,filtered;
        depth.p=texture(d,dep);mask.p=texture(d,owner);scene.p=texture(d,sc);
        for(auto* target:{&normal,&raw,&filtered})ok(d->CreateTexture(aw,ah,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,target->out(),nullptr));
        ActorCom<IDirect3DSurface9> ns,rs,fs,staging;
        ok(normal->GetSurfaceLevel(0,ns.out()));ok(raw->GetSurfaceLevel(0,rs.out()));ok(filtered->GetSurfaceLevel(0,fs.out()));
        ok(d->CreateOffscreenPlainSurface(aw,ah,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,staging.out(),nullptr));
        float c[7][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=nearZ;c[0][3]=farZ;
        c[1][0]=sx;c[1][1]=sy;c[1][2]=strength;c[2][0]=.08f;c[2][1]=radius;c[3][1]=1.f/partition;c[3][2]=partition;
        ok(d->SetPixelShaderConstantF(0,c[0],7));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{aw-.5f,-.5f,0,1,1,0},{-.5f,ah-.5f,0,1,0,1},{aw-.5f,ah-.5f,0,1,1,1}};
        auto draw=[&](IDirect3DSurface9* target,IDirect3DPixelShader9* ps){ok(d->SetRenderTarget(0,target));ok(d->SetPixelShader(ps));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));};
        auto pipeline=[&](unsigned method,bool actor,bool fallback=false){
            actor=actor&&NorthlightAOActorPolicy::enabled(true,true,method+2,strength,radius);
            ok(d->SetTexture(0,scene));ok(d->SetTexture(1,depth));ok(d->SetTexture(2,nullptr));ok(d->SetTexture(3,nullptr));ok(d->SetTexture(4,mask));
            if(actor)draw(ns,prepare);
            ok(d->SetRenderTarget(0,rs));if(actor)ok(d->SetTexture(2,normal));
            if(fallback){
                ok(d->SetPixelShader(legacy));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
                ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA));
            }
            ok(d->SetPixelShader(actor?(fallback?alpha[method].p:candidate[method].p):baseline[method].p));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            if(fallback)ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
            ok(d->SetTexture(2,nullptr));ok(d->SetTexture(4,nullptr));
            ok(d->SetRenderTarget(0,fs));ok(d->SetTexture(0,raw));ok(d->SetTexture(2,raw));
            ok(d->SetPixelShader(blur));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            ok(d->SetTexture(0,nullptr));ok(d->SetTexture(2,nullptr));
        };
        for(unsigned fallback=0;fallback<2;++fallback)for(unsigned method=0;method<2;++method)for(unsigned actor=0;actor<2;++actor){
            c[1][3]=fallback?.12f:0;ok(d->SetPixelShaderConstantF(1,c[1],1));
            ok(d->BeginScene());pipeline(method,actor!=0,fallback!=0);ok(d->EndScene());
            ok(d->GetRenderTargetData(fs,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            std::vector<float> result(std::size_t(aw)*ah*4);
            for(unsigned y=0;y<ah;++y)for(unsigned x=0;x<aw;++x){
                std::uint16_t pixel[4];std::memcpy(pixel,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,8);
                for(unsigned k=0;k<4;++k){float value=half(pixel[k]);assert(std::isfinite(value));result[(std::size_t(y)*aw+x)*4+k]=value;}
            }
            ok(staging->UnlockRect());store(prefix+(method?".gtao":".hbao")+(fallback?".legacy":"")+(actor?".candidate.f32":".baseline.f32"),result);++count;
        }
        c[1][3]=0;ok(d->SetPixelShaderConstantF(1,c[1],1));
        if(argc==6&&w>=1000){
            ActorCom<IDirect3DQuery9> disjoint,frequency,begin,end,fence;
            ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,disjoint.out()));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,frequency.out()));
            ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,begin.out()));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,end.out()));ok(d->CreateQuery(D3DQUERYTYPE_EVENT,fence.out()));
            LARGE_INTEGER clock{};assert(QueryPerformanceFrequency(&clock));
            std::ofstream timing(prefix+".perf.tsv");assert(timing);
            for(unsigned method=0;method<2;++method){
                ok(d->BeginScene());for(unsigned warm=0;warm<4;++warm)pipeline(method,(warm&1)!=0);ok(d->EndScene());wait(fence);
                for(unsigned round=0;round<10;++round)for(unsigned pair=0;pair<2;++pair){
                    const unsigned actor=(pair+round)&1;
                    ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));
                    LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);
                    ok(d->BeginScene());for(unsigned iteration=0;iteration<8;++iteration)pipeline(method,actor!=0);ok(d->EndScene());
                    QueryPerformanceCounter(&b);ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(fence);
                    UINT64 hz=0,t0=0,t1=0;BOOL invalid=TRUE;
                    ok(disjoint->GetData(&invalid,sizeof invalid,0));ok(frequency->GetData(&hz,sizeof hz,0));
                    ok(begin->GetData(&t0,sizeof t0,0));ok(end->GetData(&t1,sizeof t1,0));assert(!invalid&&hz&&t1>=t0);
                    timing<<method<<'\t'<<actor<<'\t'<<round<<'\t'<<double(t1-t0)*1e6/double(hz)/8<<'\t'<<double(b.QuadPart-a.QuadPart)*1e6/double(clock.QuadPart)/8<<'\n';
                }
            }
        }
        for(unsigned s=0;s<5;++s)ok(d->SetTexture(s,nullptr));
        ActorCom<IDirect3DSurface9> back;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.out()));ok(d->SetRenderTarget(0,back));
        report<<prefix<<"\tPASS\n";report.flush();
    }
    assert(cases.eof()&&count);std::printf("PASS %u production AO+blur renders; game not launched\n",count);
    DestroyWindow(window);return 0;
}
