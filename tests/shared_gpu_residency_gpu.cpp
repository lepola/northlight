#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "replay_gpu_batches.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
using Mesh=NorthlightDrawSnapshot::Mesh;using Owner=std::shared_ptr<const Mesh>;
static void ok(HRESULT result){assert(SUCCEEDED(result));}
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static std::vector<unsigned char> read(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct Geometry {Owner mesh;float center[4]{};};
static std::vector<Geometry> load(const std::string& path){
 std::ifstream f(path,std::ios::binary);assert(f);uint32_t count;f.read(reinterpret_cast<char*>(&count),4);assert(count==177);std::vector<Geometry> result;
 for(uint32_t i=0;i<count;++i){uint32_t n[2];f.read(reinterpret_cast<char*>(n),8);auto m=std::make_shared<Mesh>();m->streams[0].stride=48;m->streams[0].bytes.resize(n[0]);m->indices.resize(n[1]);
  f.read(reinterpret_cast<char*>(m->streams[0].bytes.data()),n[0]);f.read(reinterpret_cast<char*>(m->indices.data()),n[1]*4);assert(f);
  m->indexed=true;m->sharedCapture=true;m->vertexCount=n[0]/48;m->primitiveCount=n[1]/3;m->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,i});Geometry g;g.mesh=m;
  float lo[3]={1e20f,1e20f,1e20f},hi[3]={-1e20f,-1e20f,-1e20f};for(unsigned v=0;v<m->vertexCount;++v){float p[3];std::memcpy(p,m->streams[0].bytes.data()+v*48,12);for(unsigned k=0;k<3;++k){assert(std::isfinite(p[k]));lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}}
  for(unsigned k=0;k<3;++k)g.center[k]=(lo[k]+hi[k])*.5f;g.center[3]=1.5f/std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2],.001f});result.push_back(g);
 }return result;
}
struct Binding {IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;~Binding(){for(auto& p:vb)drop(p);drop(ib);}};
struct Targets {
 IDirect3DDevice9* d;IDirect3DSurface9* colour[2]={};IDirect3DSurface9* depth=nullptr,*resolve=nullptr,*stage=nullptr;
 Targets(IDirect3DDevice9* device,unsigned samples):d(device){for(auto& p:colour)ok(d->CreateRenderTarget(96,96,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&p,nullptr));
  ok(d->CreateDepthStencilSurface(96,96,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,TRUE,&depth,nullptr));
  ok(d->CreateRenderTarget(96,96,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolve,nullptr));ok(d->CreateOffscreenPlainSurface(96,96,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stage,nullptr));}
 ~Targets(){ok(d->SetRenderTarget(1,nullptr));ok(d->SetDepthStencilSurface(nullptr));for(auto& p:colour)drop(p);drop(depth);drop(resolve);drop(stage);}
 void bind(){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,colour[0]));ok(d->SetRenderTarget(1,colour[1]));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp{0,0,96,96,0,1};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));}
 void pixels(std::vector<uint32_t>& out){for(auto p:colour){ok(d->StretchRect(p,nullptr,resolve,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolve,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
   for(unsigned y=0;y<96;++y){const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch);out.insert(out.end(),row,row+96);}ok(stage->UnlockRect());}}
};
struct Result {std::vector<uint32_t> pixels;double cpuMs=0;size_t uploads=0,peak=0,shared=0,fallback=0;};
static Result run(IDirect3DDevice9* d,const std::vector<Geometry>& meshes,unsigned samples,bool shared,bool capture){
 using namespace NorthlightReplayGPU;Cache material;BatchedCache world;material.setLimit(32u<<20);createBudgetMs()=1e6;
 const auto materialSource=residentSource(material),worldSource=residentSource(world);Result r;Targets targets(d,samples);
 for(unsigned frame=0;frame<(capture?12u:48u);++frame){
  const auto start=std::chrono::steady_clock::now();material.beginFrame();world.beginFrame();
  for(unsigned pass=0;pass<2;++pass){targets.bind();ok(d->BeginScene());Binding binding;
   const float angle=float(frame)*.47f;const float rows[4][4]={{std::cos(angle),0,std::sin(angle),0},{0,1,0,0},{-std::sin(angle),0,std::cos(angle),0},{.08f*std::sin(angle*2),float(pass),0,0}};
   ok(d->SetVertexShaderConstantF(1,rows[0],4));
   for(auto& g:meshes){binding.at={};bool borrowed=false,ready=false;
    if(!pass){borrowed=shared&&worldSource.borrow(g.mesh,binding.vb,binding.ib,binding.at);ready=borrowed||material.bind(d,g.mesh,binding.vb,binding.ib,[](size_t){return true;});}
    else{borrowed=shared&&materialSource.borrow(g.mesh,binding.vb,binding.ib,binding.at);ready=borrowed||world.bind(d,g.mesh,binding.vb,binding.ib,[](size_t){return true;},binding.at);}
    r.shared+=borrowed;ok(d->SetVertexShaderConstantF(0,g.center,1));
    if(ready){ok(d->SetStreamSource(0,binding.vb[0],0,48));ok(d->SetIndices(binding.ib));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,INT(binding.at.vertexBase),0,g.mesh->vertexCount,binding.at.indexStart,g.mesh->primitiveCount));}
    else{++r.fallback;ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,g.mesh->vertexCount,g.mesh->primitiveCount,g.mesh->indices.data(),D3DFMT_INDEX32,g.mesh->streams[0].bytes.data(),48));}
   }
   ok(d->EndScene());if(pass)world.commitPending();
   if(capture){r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();targets.pixels(r.pixels);}
  }
  if(!capture)r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  r.uploads+=material.uploaded()+world.uploaded();r.peak=std::max(r.peak,material.bytes()+world.bytes());assert(material.chargedBytes()<=material.limit()&&world.bytes()<=world.limit());
  if(frame%11==10){material.trimTo(0);world.trimTo(0);} // Force exact UP fallbacks after residency loss.
 }
 ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));world.clear();material.clear();return r;
}
int main(int argc,char** argv){
 assert(argc>=4);const std::string folder=argv[2];auto meshes=load(argv[3]);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSharedGPU";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=96;pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;auto bytes=read(folder+"vs.bin");ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(bytes.data()),&vs));bytes=read(folder+"ps.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps));
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
 IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
 for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
 if(argc==5){const bool shared=std::string(argv[4])=="shared";auto r=run(d,meshes,0,shared,false);std::printf("SHARED_GPU_NATIVE_TIMING shared=%u cpuMs=%.6f uploads=%zu peak=%zu sharedDraws=%zu fallback=%zu\n",shared,r.cpuMs,r.uploads,r.peak,r.shared,r.fallback);}
 else for(unsigned samples:{0u,4u,8u}){DWORD quality=0;ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_TYPE(samples),&quality));auto old=run(d,meshes,samples,false,true),next=run(d,meshes,samples,true,true);
  assert(old.pixels==next.pixels&&next.shared&&next.uploads<old.uploads&&next.peak<old.peak);
  assert(std::count_if(old.pixels.begin(),old.pixels.end(),[](uint32_t p){return p!=0;})>1000);
  const size_t framePixels=96*96*4;assert(!std::equal(old.pixels.begin(),old.pixels.begin()+framePixels,old.pixels.begin()+framePixels));
  std::printf("SHARED_GPU_NATIVE_EXACT msaa=%u pixels=%zu uploads=%zu->%zu peak=%zu->%zu sharedDraws=%zu\n",samples,next.pixels.size(),old.uploads,next.uploads,old.peak,next.peak,next.shared);
 }
 ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetVertexDeclaration(nullptr));drop(vs);drop(ps);drop(decl);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);std::puts("SHARED_GPU_NATIVE_PASS");return 0;
}
