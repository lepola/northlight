// Real static Telredor geometry and production point-shadow submission; no game.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include "scene_pressure_fixture.h"
#include <cassert>
#include <chrono>
#include <fstream>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
using Clock=std::chrono::steady_clock;
static double milliseconds(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
struct PointTerrainCoverageTest {
 static void drain(IDirect3DQuery9* q){must(q->Issue(D3DISSUE_END));const auto start=GetTickCount();HRESULT hr;while((hr=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-start)<10000);SwitchToThread();}must(hr);}
 static void run(IDirect3DDevice9* d,const std::string& cache,const std::string& output,bool benchmark){
  const NorthlightGI::Vec3 center{278.582f,6001.27f,144.73f};
  const auto built=NorthlightScenePressureFixture::build(cache,{},center,{},"Expansion01");
  auto renderer=std::make_unique<WorldRenderer>(d);auto& w=*renderer;w.valid=true;
  w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;w.frames=1;
  assert(w.resources(128,128,D3DFMT_A8R8G8B8));
  auto snapshot=std::make_shared<WorldRenderer::Snapshot>();snapshot->map="Expansion01";snapshot->bvh=built.bvh;snapshot->meshPlan=built.plan;snapshot->serial=1;snapshot->atlas.resize(NorthlightGI::probeLayout().atlasSize());snapshot->center=center;
  NorthlightLocalLights::Cache lights(cache+"/lights");assert(lights.loadLights(snapshot->map,&center.x,288,snapshot->localLights));w.active=snapshot;
  const auto uploadStart=GetTickCount();while(w.uploaded.lock()!=snapshot->bvh){NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(!w.failed&&DWORD(GetTickCount()-uploadStart)<30000);must(d->Present(nullptr,nullptr,nullptr,nullptr));}
  std::memcpy(w.context.camera,&center.x,12);w.projection[0]=1;w.projection[1]=16.f/9;w.projection[2]=1;
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  auto image=[&](IDirect3DSurface9* surface){must(d->GetRenderTargetData(surface,readback));D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> pixels(256*256*4);for(unsigned y=0;y<256;++y)std::memcpy(pixels.data()+y*256*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());return pixels;};
  std::ofstream rows(output+"/timings.tsv");unsigned comparisons=0,omitted=0,nonempty=0;
  for(unsigned limit:{4u,128u})for(unsigned pose=0;pose<8;++pose){
   const float angle=float(pose)*3.14159265359f/4,c=std::cos(angle),s=std::sin(angle),up=std::sqrt(.96f);
   const float inverse[]={-s,c,0,0,.2f*c,.2f*s,up,0,up*c,up*s,-.2f,0,center.x,center.y,center.z,1};std::memcpy(w.context.inverseView,inverse,sizeof inverse);
   w.quality.pointShadowLimit=limit;w.quality.localLightLimit=48;w.localLightQpc=0;w.atmosphereWork(w.atmosphere,.1f,3840,2160,0);
   const auto view=w.localScissorView;assert(view.valid);
   std::map<uint64_t,std::array<std::vector<unsigned char>,6>> reference;
   for(unsigned mode=0;mode<2;++mode){
    w.localScissorView=view;if(mode==0)w.localScissorView.valid=false;
    must(d->BeginScene());w.renderPointShadows(w.atmosphere.lights,true,false);must(d->EndScene());drain(fence);
    unsigned ready=0,visible=0;
    for(const auto& slot:w.pointSlots){if(!slot.ready)continue;++ready;
     const auto& l=slot.selected;const std::array<std::array<float,4>,1> sphere={{{l.position[0],l.position[1],l.position[2],l.attenuationEnd}}};
     if(!NorthlightLocalLightScissor::batchRect(view,sphere,1).area())continue;++visible;
     for(unsigned face=0;face<6;++face){auto pixels=image(slot.faces[face]);const float* values=reinterpret_cast<const float*>(pixels.data());nonempty+=std::any_of(values,values+256*256,[](float v){return v<1;});
      if(mode==0)reference[l.sourceId][face]=std::move(pixels);else{assert(reference.at(l.sourceId)[face]==pixels);++comparisons;}}
    }
    if(mode==1){assert(visible==reference.size()&&ready==visible);omitted+=unsigned(w.atmosphere.lights.count)-ready;}
    std::printf("VISIBILITY limit=%u pose=%u mode=%u selected=%u ready=%u visible=%u batches=%zu triangles=%u\n",limit,pose,mode,w.atmosphere.lights.count,ready,visible,w.batches.size(),built.plan->triangleCount);std::fflush(stdout);
   }
   if(benchmark)for(unsigned block=0;block<8;++block){
    const bool optimized=block%4==1||block%4==2;w.localScissorView=view;if(!optimized)w.localScissorView.valid=false;
    // Prime visibility transitions and driver work before each paired block.
    for(unsigned warm=0;warm<2;++warm){must(d->BeginScene());w.renderPointShadows(w.atmosphere.lights,true,false);must(d->EndScene());}drain(fence);
    double submit=0;const auto start=Clock::now();const unsigned updates=w.pointUpdates;
    for(unsigned frame=0;frame<64;++frame){const auto cpu=Clock::now();must(d->BeginScene());w.renderPointShadows(w.atmosphere.lights,true,false);must(d->EndScene());submit+=milliseconds(cpu);}
    drain(fence);rows<<limit<<'\t'<<pose<<'\t'<<optimized<<'\t'<<block<<'\t'<<submit/64<<'\t'<<milliseconds(start)/64<<'\t'<<(w.pointUpdates-updates)/64<<'\n';
   }
  }
  assert(comparisons>100&&omitted>0&&nonempty>0);readback->Release();fence->Release();
  std::printf("POINT_VISIBILITY_GPU_PASS exactFaces=%u offscreenSlots=%u nonemptyFaces=%u realCache=1 gameLaunched=0\n",comparisons,omitted,nonempty);
 }
};
int main(int argc,char** argv){
 assert(argc==5||argc==6);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[3]);std::wstring path(argv[3],argv[3]+std::strlen(argv[3]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointVisibility";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DADAPTER_IDENTIFIER9 adapter{};must(api->GetAdapterIdentifier(0,0,&adapter));std::printf("GPU_ADAPTER description=%s vendor=%04lx device=%04lx driver=%s\n",adapter.Description,(unsigned long)adapter.VendorId,(unsigned long)adapter.DeviceId,adapter.Driver);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));PointTerrainCoverageTest::run(d,argv[2],argv[4],argc==6);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
