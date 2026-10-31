// Hidden-device regression using local crystal topology and native M2 shaders.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <string>
#include <vector>
template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "spell_layer.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static std::vector<DWORD> words(const std::string& path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();assert(n>0&&n%4==0);std::vector<DWORD> b(size_t(n)/4);f.seekg(0);f.read((char*)b.data(),n);assert(f);return b;}
struct MaterialCaptureStateTestAccess {static NorthlightAutoDisplacement& geometry(NorthlightAutoBump& b){return b.displacement;}};
struct DisplacementStateTestAccess {static void cpu(NorthlightAutoDisplacement& g){g.gpuHeightChecked=true;g.gpuHeightSupported=false;}};
struct V {float p[3];unsigned char weights[4],bones[4];float normal[3],uv[2],uv1[2];};
static_assert(sizeof(V)==48);
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCrystalOverlay";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    constexpr unsigned W=256,H=256;IDirect3DSurface9 *target=nullptr,*depth=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})ok(d->SetRenderState(k,TRUE));for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    std::ifstream f(folder+"crystal.mesh",std::ios::binary);unsigned nv=0,ni=0;f.read((char*)&nv,4);f.read((char*)&ni,4);assert(f&&nv&&ni%3==0);std::vector<V> vertices(nv);std::vector<unsigned> indices(ni);f.read((char*)vertices.data(),nv*sizeof(V));f.read((char*)indices.data(),ni*4);assert(f);
    float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};for(const auto& v:vertices)for(unsigned k=0;k<3;++k){lo[k]=std::min(lo[k],v.p[k]);hi[k]=std::max(hi[k],v.p[k]);}const float scale=1.7f/std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});for(auto& v:vertices)for(unsigned k=0;k<3;++k)v.p[k]=(v.p[k]-(lo[k]+hi[k])*.5f)*scale;
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* upload=nullptr;ok(d->CreateVertexBuffer(nv*sizeof(V),0,0,D3DPOOL_MANAGED,&vb,nullptr));ok(vb->Lock(0,0,&upload,0));std::memcpy(upload,vertices.data(),nv*sizeof(V));ok(vb->Unlock());ok(d->CreateIndexBuffer(ni*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));ok(ib->Lock(0,0,&upload,0));std::memcpy(upload,indices.data(),ni*4);ok(ib->Unlock());
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));ok(d->SetVertexDeclaration(decl));ok(d->SetStreamSource(0,vb,0,sizeof(V)));ok(d->SetIndices(ib));
    std::ifstream tex(folder+"crystal.rgba",std::ios::binary);unsigned tw=0,th=0;tex.read((char*)&tw,4);tex.read((char*)&th,4);assert(tex&&tw&&th);std::vector<unsigned char> pixels(tw*th*4);tex.read((char*)pixels.data(),pixels.size());assert(tex);IDirect3DTexture9* texture=nullptr;ok(d->CreateTexture(tw,th,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock{};ok(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<th;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,pixels.data()+y*tw*4,tw*4);ok(texture->UnlockRect(0));ok(d->SetTexture(0,texture));
    for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,k,D3DTEXF_LINEAR));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(0,k,D3DTADDRESS_WRAP));
    auto psWords=words(folder+"CrystalPS.bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(psWords.data(),&ps));ok(d->SetPixelShader(ps));
    IDirect3DQuery9* query=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query));auto count=[&](auto draw){ok(query->Issue(D3DISSUE_BEGIN));ok(draw());ok(query->Issue(D3DISSUE_END));DWORD n=0;HRESULT hr=S_FALSE;for(unsigned i=0;i<5000&&hr==S_FALSE;++i){hr=query->GetData(&n,sizeof n,D3DGETDATA_FLUSH);if(hr==S_FALSE)Sleep(1);}ok(hr);return n;};
    NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,nv,0,ni/3,true}};auto native=[&]{return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,nv,0,ni/3);};
    unsigned cases=0,negative=0;
    for(unsigned sm:{2u,3u})for(unsigned gpu=0;gpu<=(sm==3);++gpu)for(unsigned variant:{10u,11u}){
        NorthlightAutoBump bump(d);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpModels=3;settings.autoModelDisplacementStrength=100;settings.autoModelDisplacementDistance=16;settings.autoModelDisplacementDetail=3;bump.configure(settings);
        bump.setGeometryTracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);
        auto& g=MaterialCaptureStateTestAccess::geometry(bump);if(!gpu)DisplacementStateTestAccess::cpu(g);
        IDirect3DVertexShader9* shaders[3]={};for(unsigned j=0;j<3;++j){const auto b=words(folder+(j==1?"diffuse_env-":"diffuse_t1-")+std::to_string(sm)+"-"+std::to_string(variant)+".bin");ok(d->CreateVertexShader(b.data(),&shaders[j]));bump.registerVertex(shaders[j],NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(b.data()),b.size()),b.data(),b.size(),3);}
        NorthlightSpellLayer::Layer layer;layer.attach(d);NorthlightSpellLayer::Config config;config.on=true;unsigned mirrorEntries=0;NorthlightSpellLayer::MirrorView mirror{};mirror.ctx=&mirrorEntries;mirror.enter=[](void* p){++*static_cast<unsigned*>(p);return true;};mirror.leave=[](void*){};config.source=&mirror;SyncedSlots slots{};config.synced=&slots;layer.configure(config);
        for(unsigned rotation=0;rotation<12;++rotation){layer.beginFrame(false);
            float bank[256*4]={};const float angle=float(rotation)*.12f,c=std::cos(angle),s=std::sin(angle);bank[2*4]=1;bank[3*4+1]=1;bank[4*4+2]=-.05f;bank[4*4+3]=.2f;bank[5*4+3]=1;bank[6*4]=bank[7*4+1]=1;std::fill(bank+28*4,bank+29*4,1.f);bank[31*4]=c;bank[31*4+2]=s;bank[32*4+1]=1;bank[33*4]=-s;bank[33*4+2]=c;bank[33*4+3]=-5;
            ok(d->SetVertexShaderConstantF(0,bank,256));ok(d->SetVertexShader(shaders[0]));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));bump.beginDraw();assert(g.prepare(shaders[0],4,7,args));assert(g.preparedHeightGPU()==bool(gpu));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());ok(bump.geometryDraw(native,true));
            ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));const auto reference=count([&]{return g.draw(native);});assert(reference>100);
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            for(unsigned j=1;j<3;++j){ok(d->SetVertexShader(shaders[j]));ok(d->SetRenderState(D3DRS_DESTBLEND,j==2&&variant==11?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));const auto unsynced=count(native);negative+=unsynced!=reference;bump.beginDraw();bump.capture(shaders[j],native,false,&args);assert(g.preparedModelOverlay());const auto synced=count([&]{return bump.geometryDraw(native,true);});assert(synced==reference);layer.beginFrame(false);const auto deferred=count([&]{assert(bump.deferModelOverlay([&](unsigned vertices,unsigned primitives){auto draw=NorthlightSpellLayer::DrawArgs::indexed(D3DPT_TRIANGLELIST,0,0,vertices,0,primitives);draw.psMajor=3;draw.deviceStateOnly=true;const bool accepted=layer.defer(draw,target,depth,NorthlightSpellLayer::SubAdditive);if(!accepted){std::printf("DEFER_REFUSED sm=%u gpu=%u variant=%u reason=%s armed=%u\n",sm,gpu,variant,NorthlightSpellLayer::reasonName(layer.lastEvent().reason),unsigned(layer.armed()));std::fflush(stdout);}return accepted;}));assert(layer.pending()==1&&mirrorEntries==0);layer.replay();assert(layer.pending()==0&&layer.frameCounters().replaySkipped==0);return D3D_OK;});assert(deferred==reference);float restored[256*4];ok(d->GetVertexShaderConstantF(0,restored,256));assert(!std::memcmp(bank,restored,sizeof bank));++cases;}
            // A changed pose must never borrow the previous object's geometry.
            bank[31*4+3]=.125f;ok(d->SetVertexShaderConstantF(31,bank+31*4,1));bump.beginDraw();bump.capture(shaders[2],native,false,&args);assert(!g.prepared());ok(d->EndScene());bump.endFrame();
        }
        for(auto& p:shaders)drop(p);
    }
    assert(negative==cases);std::printf("CRYSTAL_OVERLAY_PASS cases=%u unsyncedFailures=%u rotations=12 sm2_cpu sm3_cpu_vtf lit_unlit alpha_additive native_uvs constants_restored deferred_replay pose_guard\n",cases,negative);
    drop(query);drop(ps);drop(texture);drop(decl);drop(vb);drop(ib);drop(target);drop(depth);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);
}
