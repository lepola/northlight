// An authored capture/retrace plumbing oracle, not a game-scene performance test.
#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static void ok(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"D3D9 HRESULT=0x%08lx\n",(unsigned long)hr);std::abort();}}
static void wait(IDirect3DQuery9* q,void* output,DWORD size){
    const auto begin=GetTickCount64();HRESULT hr;
    while((hr=q->GetData(output,size,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount64()-begin<10000);SwitchToThread();}
    ok(hr);
}
static std::string quoted(const char* value){
    std::string out="\"";for(const unsigned char* p=(const unsigned char*)value;*p;++p){
        if(*p=='\\'||*p=='\"')out+='\\';assert(*p>=32);out+=char(*p);
    }return out+'\"';
}
static std::string modules(){
    HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());assert(h!=INVALID_HANDLE_VALUE);
    MODULEENTRY32 entry{};entry.dwSize=sizeof entry;std::string out="[";bool first=true;
    for(BOOL next=Module32First(h,&entry);next;next=Module32Next(h,&entry)){
        if(!first)out+=',';first=false;out+=quoted(entry.szExePath);
    }CloseHandle(h);return out+"]";
}
static void describe(const char* name,IDirect3DSurface9* surface){
    D3DSURFACE_DESC desc{};ok(surface->GetDesc(&desc));
    std::fprintf(stderr,"SURFACE %s identity=%p size=%ux%u format=%u pool=%u usage=%08lx msaa=%u/%lu\n",name,(void*)surface,desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.Pool),(unsigned long)desc.Usage,unsigned(desc.MultiSampleType),(unsigned long)desc.MultiSampleQuality);
}
static void parameters(const char* stage,IDirect3DDevice9* d,HWND window,const D3DPRESENT_PARAMETERS& pp){
    IDirect3DSwapChain9* chain=nullptr;ok(d->GetSwapChain(0,&chain));D3DPRESENT_PARAMETERS actual{};ok(chain->GetPresentParameters(&actual));chain->Release();
    RECT client{};assert(GetClientRect(window,&client));
    std::fprintf(stderr,"PRESENT_PARAMETERS %s caller=%ux%u/%u/%u actual=%ux%u/%u/%u msaa=%u windowed=%u interval=%08lx hidden=%u client=%ldx%ld\n",stage,pp.BackBufferWidth,pp.BackBufferHeight,unsigned(pp.BackBufferFormat),pp.BackBufferCount,actual.BackBufferWidth,actual.BackBufferHeight,unsigned(actual.BackBufferFormat),actual.BackBufferCount,unsigned(actual.MultiSampleType),unsigned(actual.Windowed),(unsigned long)actual.PresentationInterval,unsigned(!IsWindowVisible(window)),client.right-client.left,client.bottom-client.top);
}
static void bindings(IDirect3DDevice9* d,IDirect3DSurface9* back,const char* stage){
    IDirect3DSurface9 *fresh=nullptr,*bound=nullptr;ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&fresh));ok(d->GetRenderTarget(0,&bound));
    std::fprintf(stderr,"BINDINGS %s heldBack=%p freshBack=%p selectedRT=%p heldEqualsFresh=%u heldEqualsSelected=%u\n",stage,(void*)back,(void*)fresh,(void*)bound,unsigned(back==fresh),unsigned(back==bound));
    describe("freshBack",fresh);describe("selectedRT",bound);fresh->Release();bound->Release();
}
static bool resetCopyDiagnostic=false;
static bool resetRectDiagnostic=false;
static bool diagnosticResult(const char* operation,HRESULT hr){
    std::fprintf(stderr,"RESET_COPY_HR operation=%s hr=%08lx\n",operation,(unsigned long)hr);return SUCCEEDED(hr);
}
static void probe(IDirect3DDevice9* d,IDirect3DSurface9* surface,const char* name,unsigned w,unsigned h){
    describe(name,surface);IDirect3DSurface9* staging=nullptr;
    if(!diagnosticResult("CreateOffscreenPlainSurface",d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr)))return;
    describe("probe-staging",staging);D3DLOCKED_RECT lock{};const DWORD poison=0xffe13579;
    if(!diagnosticResult("staging-poison-LockRect",staging->LockRect(&lock,nullptr,0))){staging->Release();return;}
    assert(lock.pBits&&lock.Pitch>=int(w*4));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)std::memcpy((char*)lock.pBits+std::size_t(y)*lock.Pitch+x*4,&poison,4);
    if(!diagnosticResult("staging-poison-UnlockRect",staging->UnlockRect())||
       !diagnosticResult("GetRenderTargetData",d->GetRenderTargetData(surface,staging))||
       !diagnosticResult("staging-read-LockRect",staging->LockRect(&lock,nullptr,D3DLOCK_READONLY))){staging->Release();return;}
    assert(lock.pBits&&lock.Pitch>=int(w*4));DWORD center=0,first=0,last=0;unsigned lit=0,unmodified=0;
    std::memcpy(&center,(const char*)lock.pBits+std::size_t(h/2)*lock.Pitch+(w/2)*4,4);
    std::memcpy(&first,lock.pBits,4);std::memcpy(&last,(const char*)lock.pBits+std::size_t(h-1)*lock.Pitch+(w-1)*4,4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){DWORD p=0;std::memcpy(&p,(const char*)lock.pBits+std::size_t(y)*lock.Pitch+x*4,4);lit+=bool(p&0xffffff);unmodified+=p==poison;}
    std::fprintf(stderr,"RESET_COPY_PIXELS stage=%s pitch=%d center=%08lx first=%08lx last=%08lx lit=%u poison=%u pixels=%u\n",name,int(lock.Pitch),(unsigned long)center,(unsigned long)first,(unsigned long)last,lit,unmodified,w*h);
    diagnosticResult("staging-read-UnlockRect",staging->UnlockRect());staging->Release();
}
static void diagnoseResetCopy(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* back,unsigned w,unsigned h){
    // Run only after preserving the failed mandatory image; these probes cannot pass that gate.
    std::fprintf(stderr,"RESET_COPY_DIAGNOSTIC_BEGIN mandatoryParityFailed=1\n");
    probe(d,target,"original-scene-target",w,h);probe(d,back,"original-backbuffer",w,h);
    IDirect3DSurface9* relay=nullptr;
    if(!diagnosticResult("CreateRenderTarget-relay",d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&relay,nullptr)))return;
    if(diagnosticResult("StretchRect-original-back-to-relay",d->StretchRect(back,nullptr,relay,nullptr,D3DTEXF_NONE)))probe(d,relay,"original-back-relay",w,h);
    if(diagnosticResult("ColorFill-backbuffer-sentinel",d->ColorFill(back,nullptr,0xff19d7a3))){
        probe(d,back,"sentinel-backbuffer",w,h);
        if(diagnosticResult("StretchRect-sentinel-back-to-relay",d->StretchRect(back,nullptr,relay,nullptr,D3DTEXF_NONE)))probe(d,relay,"sentinel-back-relay",w,h);
    }
    if(diagnosticResult("StretchRect-scene-to-back-again",d->StretchRect(target,nullptr,back,nullptr,D3DTEXF_NONE))){
        probe(d,back,"recopied-backbuffer",w,h);
        if(diagnosticResult("StretchRect-recopied-back-to-relay",d->StretchRect(back,nullptr,relay,nullptr,D3DTEXF_NONE)))probe(d,relay,"recopied-back-relay",w,h);
    }
    relay->Release();std::fprintf(stderr,"RESET_COPY_DIAGNOSTIC_END mandatoryParityFailed=1\n");
}
static void diagnoseResetRects(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* back,unsigned w,unsigned h){
    std::fprintf(stderr,"RESET_RECT_DIAGNOSTIC_BEGIN mandatoryParityFailed=1\n");
    probe(d,target,"rect-original-scene",w,h);probe(d,back,"rect-original-back",w,h);
    IDirect3DSurface9* ordinary=nullptr;
    if(!diagnosticResult("CreateRenderTarget-ordinary",d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&ordinary,nullptr)))return;
    const RECT full{0,0,LONG(w),LONG(h)};
    for(unsigned kind=0;kind<2;++kind)for(unsigned explicitRect=0;explicitRect<2;++explicitRect){
        IDirect3DSurface9* destination=kind?back:ordinary;const DWORD sentinel=0xff153247+kind*0x00200000+explicitRect*0x00001000;
        std::fprintf(stderr,"RESET_RECT_CASE destination=%s rect=%s left=0 top=0 right=%u bottom=%u sentinel=%08lx\n",kind?"currentBack":"ordinaryRT",explicitRect?"explicit":"null",w,h,(unsigned long)sentinel);
        if(!diagnosticResult("ColorFill-destination",d->ColorFill(destination,nullptr,sentinel)))continue;
        probe(d,destination,"rect-sentinel-destination",w,h);
        const RECT* rectangle=explicitRect?&full:nullptr;
        if(diagnosticResult("StretchRect-scene-to-destination",d->StretchRect(target,rectangle,destination,rectangle,D3DTEXF_NONE)))probe(d,destination,"rect-copied-destination",w,h);
    }
    ordinary->Release();std::fprintf(stderr,"RESET_RECT_DIAGNOSTIC_END mandatoryParityFailed=1\n");
}
static void save(IDirect3DDevice9* d,IDirect3DSurface9* surface,unsigned w,unsigned h,DWORD expected,const std::string& file,unsigned aa,unsigned round,unsigned frame,IDirect3DSurface9* sourceTarget){
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    DWORD center=0;std::memcpy(&center,(const char*)lock.pBits+(h/2)*lock.Pitch+(w/2)*4,4);
    const bool matched=(center&0xffffff)==(expected&0xffffff);
    if(!matched)std::fprintf(stderr,"Depth/color guard: %ux%u xy=%u,%u aa=%u round=%u frame=%u center=%08lx expected=%08lx image=%s\n",w,h,w/2,h/2,aa,round,frame,(unsigned long)center,(unsigned long)expected,file.c_str());
    std::ofstream out(file,std::ios::binary);assert(out);out<<"P6\n"<<w<<' '<<h<<"\n255\n";
    unsigned lit=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const auto* p=(const unsigned char*)lock.pBits+std::size_t(y)*lock.Pitch+x*4;
        const unsigned char rgb[]={p[2],p[1],p[0]};out.write((const char*)rgb,3);lit+=bool(p[0]|p[1]|p[2]);
    }ok(staging->UnlockRect());staging->Release();out.close();assert(out.good());
    if(!matched&&sourceTarget&&!aa)save(d,sourceTarget,w,h,expected,file+".target.ppm",aa,round,frame,nullptr);
    if(!matched&&sourceTarget&&!aa&&resetCopyDiagnostic)diagnoseResetCopy(d,sourceTarget,surface,w,h);
    if(!matched&&sourceTarget&&!aa&&resetRectDiagnostic)diagnoseResetRects(d,sourceTarget,surface,w,h);
    // Preserve the same strict oracle, then let the tracer flush through normal CRT exit.
    if(!(lit>w*h/4&&lit<w*h)||!matched)std::exit(EXIT_FAILURE);
}
struct Vertex{float x,y,z,w,u,v;};
static constexpr DWORD vsCode[]={0xfffe0200,0x0200001f,0x80000000,0x900f0000,
    0x0200001f,0x80000005,0x90030001,0x02000001,0xc00f0000,0x90e40000,
    0x02000001,0xe0030000,0x90e40001,0x0000ffff};
static constexpr DWORD psCode[]={0xffff0200,0x0200001f,0x90000000,0xa00f0800,
    0x0200001f,0x80000000,0xb0030000,0x03000042,0x800f0000,0xb0e40000,0xa0e40800,
    0x03000005,0x800f0000,0x80e40000,0xa0e40000,0x02000001,0x800f0800,0x80e40000,0x0000ffff};

int main(int argc,char** argv){
    assert(argc==3||argc==4);const std::string output=argv[1];const unsigned aa=unsigned(std::strtoul(argv[2],nullptr,10));assert(aa==0||aa==2);
    const bool afterCopyEvent=argc==4&&!std::strcmp(argv[3],"after-copy-event");
    resetCopyDiagnostic=argc==4&&!std::strcmp(argv[3],"reset-copy-diagnostic");
    resetRectDiagnostic=argc==4&&!std::strcmp(argv[3],"reset-rect-diagnostic");
    assert(argc==3||afterCopyEvent||resetCopyDiagnostic||resetRectDiagnostic);assert(!(resetCopyDiagnostic||resetRectDiagnostic)||!aa);
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="NorthlightTraceSmoke";
    assert(RegisterClassA(&cls));HWND window=CreateWindowA(cls.lpszClassName,"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);assert(window&&!IsWindowVisible(window));
    HMODULE module=LoadLibraryA("d3d9.dll");assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
    const auto sample=aa?D3DMULTISAMPLE_2_SAMPLES:D3DMULTISAMPLE_NONE;DWORD levels=0;
    ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,sample,&levels));
    ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,sample,&levels));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=96;pp.BackBufferHeight=80;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const auto moduleList=modules();unsigned images=0,nonzeroOcclusions=0;std::vector<unsigned> widths,heights;
    for(unsigned round=0;round<2;++round){
        if(round){pp.BackBufferWidth=112;pp.BackBufferHeight=96;parameters("before-reset",d,window,pp);ok(d->Reset(&pp));}
        parameters("round-begin",d,window,pp);
        const unsigned w=pp.BackBufferWidth,h=pp.BackBufferHeight;
        IDirect3DSurface9 *target=nullptr,*depth=nullptr,*back=nullptr;IDirect3DTexture9* tex=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DQuery9 *event=nullptr,*occlusion=nullptr;
        ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
        ok(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,sample,0,FALSE,&target,nullptr));
        ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,sample,0,FALSE,&depth,nullptr));
        describe("round-back",back);describe("round-target",target);bindings(d,back,"round-begin");
        D3DSURFACE_DESC description{};ok(target->GetDesc(&description));assert(description.MultiSampleType==sample);
        ok(d->CreateTexture(2,2,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,nullptr));
        const Vertex quad[]={{-.93f,.87f,.4f,1,0,0},{.91f,.87f,.4f,1,1,0},{-.93f,-.89f,.4f,1,0,1},{.91f,-.89f,.4f,1,1,1}};
        const WORD indices[]={0,1,2,2,1,3};
        ok(d->CreateVertexBuffer(sizeof quad,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr));
        ok(d->CreateIndexBuffer(sizeof indices,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
        void* data=nullptr;ok(vb->Lock(0,sizeof quad,&data,D3DLOCK_DISCARD));std::memcpy(data,quad,sizeof quad);ok(vb->Unlock());
        ok(ib->Lock(0,sizeof indices,&data,D3DLOCK_DISCARD));std::memcpy(data,indices,sizeof indices);ok(ib->Unlock());
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        ok(d->CreateVertexDeclaration(elements,&decl));ok(d->CreateVertexShader(vsCode,&vs));ok(d->CreatePixelShader(psCode,&ps));
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&occlusion));
        for(unsigned frame=0;frame<3;++frame){
            D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));
            const DWORD colour=D3DCOLOR_ARGB(255,64+frame*35,112+round*20,192-frame*25);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy((char*)lock.pBits+y*lock.Pitch+x*4,&colour,4);
            ok(tex->UnlockRect(0));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));
            D3DVIEWPORT9 viewport{0,0,w,h,0,1};ok(d->SetViewport(&viewport));
            ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
            ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
            ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_DITHERENABLE,FALSE));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
            ok(d->SetTexture(0,tex));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
            const float white[]={1,1,1,1};ok(d->SetPixelShaderConstantF(0,white,1));ok(d->SetStreamSource(0,vb,0,sizeof(Vertex)));ok(d->SetIndices(ib));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0));ok(d->BeginScene());
            ok(occlusion->Issue(D3DISSUE_BEGIN));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));ok(occlusion->Issue(D3DISSUE_END));
            ok(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));
            Vertex farQuad[4];std::memcpy(farQuad,quad,sizeof quad);for(auto& v:farQuad)v.z=.95f;
            const float green[]={0,1,0,1};ok(d->SetPixelShaderConstantF(0,green,1));
            ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,farQuad,sizeof(Vertex)));
            ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,farQuad,sizeof(Vertex)));
            ok(d->EndScene());DWORD samples=0;wait(occlusion,&samples,sizeof samples);assert(samples>0);++nonzeroOcclusions;
            ok(event->Issue(D3DISSUE_END));BOOL finished=FALSE;wait(event,&finished,sizeof finished);assert(finished);
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,back));ok(d->StretchRect(target,nullptr,back,nullptr,D3DTEXF_NONE));
            if(afterCopyEvent){ok(event->Issue(D3DISSUE_END));finished=FALSE;wait(event,&finished,sizeof finished);assert(finished);}
            bindings(d,back,"after-copy");
            char name[64];std::snprintf(name,sizeof name,"/frame%06u.ppm",images);save(d,back,w,h,colour,output+name,aa,round,frame,target);
            widths.push_back(w);heights.push_back(h);++images;ok(d->Present(nullptr,nullptr,nullptr,nullptr));
        }
        ok(d->SetTexture(0,nullptr));ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));ok(d->SetVertexDeclaration(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));
        event->Release();occlusion->Release();vs->Release();ps->Release();decl->Release();vb->Release();ib->Release();tex->Release();target->Release();depth->Release();back->Release();
    }
    std::ofstream report(output+"/source.json");assert(report);
    report<<"{\"format\":1,\"authored_plumbing_only\":true,\"game_launched\":false,\"full_scene_fps_claimed\":false,\"hidden_window\":true,\"presentation_interval\":2147483648,\"msaa\":"<<aa<<",\"frames\":"<<images<<",\"nonzero_occlusions\":"<<nonzeroOcclusions<<",\"modules\":"<<moduleList<<",\"images\":[";
    for(unsigned i=0;i<images;++i){if(i)report<<',';char name[64];std::snprintf(name,sizeof name,"frame%06u.ppm",i);report<<"{\"path\":"<<quoted(name)<<",\"width\":"<<widths[i]<<",\"height\":"<<heights[i]<<'}';}
    report<<"]}\n";assert(report.good());d->Release();api->Release();DestroyWindow(window);FreeLibrary(module);
    std::puts("PASS authored hidden D3D9 capture input: 6 frames, 4 draw APIs, uploads, shaders, queries, MSAA target, Reset");
}
