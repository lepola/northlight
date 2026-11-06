// Real static cache geometry and production point-shadow submission; no game.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include "scene_pressure_fixture.h"
#include <cassert>
#include <chrono>
#include <fstream>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
using Clock=std::chrono::steady_clock;
static double milliseconds(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
 static void metadata(const Frame& frame,bool tracked){
  assert(frame.sources_->entries.size()==1);const auto& e=*frame.sources_->entries.begin()->second;
  assert(e.compacted&&e.key.tracked==tracked);
  assert(e.rawIndices.empty()==tracked&&e.runs.empty()==tracked);
 }
};
}
namespace NorthlightStream {
struct ReplayUploadTestAccess {
 static void upload(Replayer& replay,ProxyBase* proxy,UINT off,UINT size,const unsigned char* data,DWORD flags=0){replay.writeBuffer(proxy,off,size,flags,data);}
 static void profile(Replayer& replay,bool begin){
  if(begin){replay.replayProfile_.begin(true,127);return;}
  const auto& p=replay.replayProfile_.upload;
  if(p.calls!=3||p.bytes!=3||p.skipped!=1||p.failed||p.clockReads!=10){std::printf("UPLOAD_PROFILE calls=%u bytes=%llu skipped=%u failed=%u clocks=%u\n",p.calls,(unsigned long long)p.bytes,p.skipped,p.failed,p.clockReads);std::fflush(stdout);}
  assert(p.calls==3&&p.bytes==3&&p.skipped==1&&!p.failed&&p.clockReads==10);
  replay.replayProfile_.begin(false,128);
 }
};
}
struct PointTerrainCoverageTest {
 static void drain(IDirect3DQuery9* q){must(q->Issue(D3DISSUE_END));const auto start=GetTickCount();HRESULT hr;while((hr=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-start)<10000);SwitchToThread();}must(hr);}
 static void snapshotMetadata(IDirect3DDevice9* d){
  using namespace NorthlightDrawSnapshot;
  IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
  must(d->CreateVertexBuffer(256*12,0,0,D3DPOOL_MANAGED,&vb,nullptr));must(d->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
  const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&decl));
  must(d->SetStreamSource(0,vb,0,12));must(d->SetIndices(ib));
  static uint64_t revision=1;
  for(bool tracked:{false,true}){
   Frame frame;frame.setIdentityProvider([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));});frame.setVersionProvider([](void*,bool){return revision;});revision=tracked?1:0;
   Draw draw;draw.vertices=256;draw.primitives=1;Mesh scratch;
   auto read=[&](){frame.clearFrame();std::shared_ptr<const Mesh> mesh;assert(frame.read(d,decl,draw,scratch,nullptr,false,&mesh)&&mesh);return mesh;};
   void* data=nullptr;must(vb->Lock(0,0,&data,0));for(unsigned i=0;i<256*3;++i)static_cast<float*>(data)[i]=float(i);must(vb->Unlock());
   uint16_t indices[]={0,128,255};must(ib->Lock(0,0,&data,0));std::memcpy(data,indices,6);must(ib->Unlock());
   auto first=read();assert(first->vertexCount==3);SnapshotTestAccess::metadata(frame,tracked);assert(read()==first);
   // The untracked entry must still compare sparse copy runs and original IB bytes.
   must(vb->Lock(128*12,4,&data,0));*static_cast<float*>(data)=9876;must(vb->Unlock());if(tracked)++revision;
   auto changed=read();assert(changed->streams[0].bytes!=first->streams[0].bytes);assert(read()==changed);
   indices[1]=129;must(ib->Lock(0,0,&data,0));std::memcpy(data,indices,6);must(ib->Unlock());if(tracked)++revision;
   auto remapped=read();assert(remapped->streams[0].bytes!=changed->streams[0].bytes);assert(read()==remapped);
   frame.setVersionProvider([](void*,bool){return uint64_t(0);});assert(read()->streams[0].bytes==remapped->streams[0].bytes);SnapshotTestAccess::metadata(frame,false);
  }
  must(d->SetStreamSource(0,nullptr,0,0));must(d->SetIndices(nullptr));vb->Release();ib->Release();decl->Release();
  std::puts("SNAPSHOT_METADATA_PASS tracked untracked sparse-vb-change ib-change provider-change");
 }
 static void snapshotPressure(IDirect3DDevice9* d,WorldRenderer& w,const std::string& output){
  std::ofstream rows(output+"/snapshot-trim.tsv");
  NorthlightDrawSnapshot::Frame cache;cache.setIdentityProvider([](void* buffer,bool){return uint64_t(reinterpret_cast<uintptr_t>(buffer));});
  cache.setVersionProvider([](void*,bool){return uint64_t(1);});cache.setSnapshotCacheLimit(64u<<20);
  for(unsigned trial=0;trial<6;++trial){
   UINT page=UINT_MAX;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> held;NorthlightDrawSnapshot::Mesh scratch;
   for(size_t i=0;i<w.batches.size();++i){const auto& batch=w.batches[i];cache.clearFrame();assert(w.bindMeshPage(batch,page));
    NorthlightDrawSnapshot::Draw draw;draw.minimum=batch.minVertex;draw.vertices=batch.vertexCount;draw.start=batch.start;draw.primitives=batch.count;
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;NorthlightDrawSnapshot::Diagnostics why;
    IDirect3DVertexDeclaration9* declaration=nullptr;must(d->GetVertexDeclaration(&declaration));assert(declaration);
    const bool ok=cache.read(d,declaration,draw,scratch,&why,false,&mesh);declaration->Release();
    if(!ok||!mesh){std::printf("SNAPSHOT_FAILED batch=%zu error=%s hr=%08lx shared=%u\n",i,NorthlightDrawSnapshot::errorName(why.error),(unsigned long)why.hr,unsigned(bool(mesh)));std::fflush(stdout);}
    assert(ok&&mesh);if(!held)held=mesh;
   }
   const size_t sourceBytes=cache.sharedSourceBytes(),indexBytes=cache.indexCacheBytes(),sources=cache.snapshotCacheEntries();const unsigned indices=cache.indexCacheEntries();
   assert(sources&&indices&&held);const auto before=*held;
   const auto started=Clock::now();cache.clearSnapshotCache();
   cache.clearIndexCache();const double clearMs=milliseconds(started);
   assert(!cache.snapshotCacheEntries()&&!cache.indexCacheEntries()&&cache.sourceLiveObjects()==1);
   assert(held->indices==before.indices);for(unsigned s=0;s<4;++s)assert(held->streams[s].bytes==before.streams[s].bytes);
   rows<<trial<<'\t'<<sources<<'\t'<<indices<<'\t'<<sourceBytes<<'\t'<<indexBytes<<'\t'<<clearMs<<'\n';
   std::printf("TELREDOR_SNAPSHOT_TRIM trial=%u sources=%zu indices=%u sourceMiB=%.3f indexMiB=%.3f clearMs=%.6f heldExact=1\n",trial,sources,indices,double(sourceBytes)/(1u<<20),double(indexBytes)/(1u<<20),clearMs);std::fflush(stdout);
  }
 }
 template<class T> static void replayBuffer(IDirect3DDevice9* device,T* raw,NorthlightStream::Replayer& replay,IDirect3DQuery9* fence,std::ofstream& rows,bool clone){
  namespace C=NorthlightReplayCopies;namespace S=NorthlightStream;
  constexpr bool index=std::is_same<T,IDirect3DIndexBuffer9>::value;
  using Forward=std::conditional_t<index,ForwardIDirect3DIndexBuffer9,ForwardIDirect3DVertexBuffer9>;
  std::conditional_t<index,D3DINDEXBUFFER_DESC,D3DVERTEXBUFFER_DESC> desc{};must(raw->GetDesc(&desc));
  void* data=nullptr;must(raw->Lock(0,desc.Size,&data,D3DLOCK_READONLY));std::vector<unsigned char> payload(static_cast<unsigned char*>(data),static_cast<unsigned char*>(data)+desc.Size);must(raw->Unlock());
  if(clone){
   const bool dynamic=!(desc.Usage&D3DUSAGE_DYNAMIC);const DWORD usage=dynamic?D3DUSAGE_DYNAMIC:0;const auto pool=dynamic?D3DPOOL_DEFAULT:D3DPOOL_MANAGED;
   if constexpr(index)must(device->CreateIndexBuffer(desc.Size,usage,desc.Format,pool,&raw,nullptr));
   else must(device->CreateVertexBuffer(desc.Size,usage,0,pool,&raw,nullptr));
   must(raw->GetDesc(&desc));must(raw->Lock(0,desc.Size,&data,dynamic?D3DLOCK_DISCARD:0));std::memcpy(data,payload.data(),desc.Size);must(raw->Unlock());
  }
  const bool dynamic=(desc.Usage&D3DUSAGE_DYNAMIC)!=0;
  const DWORD flags=dynamic?D3DLOCK_NOOVERWRITE:0;
  raw->AddRef();T* tracked=raw;NorthlightTrackedBuffers::wrap<T,Forward>(&tracked,device,index);assert(tracked!=raw);
  S::ProxyBase proxy(&replay.core,index?S::Kind::IndexBuffer:S::Kind::VertexBuffer);proxy.inner=tracked;
  // Fill through the real capture reader, including the two-frame large-buffer gate.
  for(unsigned i=0;i<2;++i){C::advanceFrame();C::Reader<T> reader(raw);must(reader.lock(0,desc.Size,&data));assert(!std::memcmp(data,payload.data(),desc.Size));must(reader.unlock());}
  for(unsigned block=0;block<8;++block){const bool optimized=block%4==1||block%4==2;double submit=0;const auto skips=NorthlightTrackedBuffers::skippedWrites.load();drain(fence);const auto start=Clock::now();
   for(unsigned i=0;i<32;++i){const UINT size=std::min<UINT>(desc.Size,i%2?256:65536),off=(desc.Size-size)?UINT((uint64_t(i)*104729)%(desc.Size-size+1)):0;const auto call=Clock::now();
    if(optimized)S::ReplayUploadTestAccess::upload(replay,&proxy,off,size,payload.data()+off,flags);
    else {C::UnlockSourceScope source(payload.data()+off,off,size);must(tracked->Lock(off,size,&data,flags));std::memcpy(data,payload.data()+off,size);must(tracked->Unlock());}
    submit+=milliseconds(call);
   }
   drain(fence);rows<<index<<'\t'<<desc.Size<<'\t'<<block<<'\t'<<optimized<<'\t'<<submit<<'\t'<<milliseconds(start)<<'\t'<<(NorthlightTrackedBuffers::skippedWrites.load()-skips)<<'\t'<<dynamic<<'\n';
  }
  // A changed byte must reach the real backend; restoring it must restore every byte.
  S::ReplayUploadTestAccess::profile(replay,true);
  for(unsigned i=0;i<2;++i){payload.back()^=1;const auto skips=NorthlightTrackedBuffers::skippedWrites.load();
   S::ReplayUploadTestAccess::upload(replay,&proxy,desc.Size-1,1,payload.data()+desc.Size-1,flags);assert(NorthlightTrackedBuffers::skippedWrites.load()==skips);drain(fence);
   must(raw->Lock(0,desc.Size,&data,D3DLOCK_READONLY));assert(!std::memcmp(data,payload.data(),desc.Size));must(raw->Unlock());}
  S::ReplayUploadTestAccess::upload(replay,&proxy,desc.Size-1,1,payload.data()+desc.Size-1,flags);S::ReplayUploadTestAccess::profile(replay,false);
  assert(!S::get(replay.core.q.stats.replayFailures));tracked->Release();if(clone)raw->Release();
 }
 static void replayUploads(IDirect3DDevice9* d,WorldRenderer& w,const std::string& output){
  const bool enabled=NorthlightReplayCopies::enabled.exchange(true);const auto before=NorthlightTrackedBuffers::skippedWrites.load();
  auto core=std::make_unique<NorthlightStream::StreamCore>();auto replay=std::make_unique<NorthlightStream::Replayer>(*core);
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));std::ofstream rows(output+"/buffer-uploads.tsv");unsigned buffers=0;
  for(bool clone:{false,true})for(auto& page:w.meshBuffers){if(page.vb){replayBuffer(d,page.vb,*replay,fence,rows,clone);++buffers;}if(page.ib){replayBuffer(d,page.ib,*replay,fence,rows,clone);++buffers;}}
  fence->Release();NorthlightReplayCopies::enabled.store(enabled);const auto skipped=NorthlightTrackedBuffers::skippedWrites.load()-before;assert(buffers&&skipped);
  std::printf("TELREDOR_UPLOAD_PASS buffers=%u skipped=%llu changedRestoredExact=1 productionReplayer=1\n",buffers,(unsigned long long)skipped);std::fflush(stdout);
 }
 static void run(IDirect3DDevice9* d,const std::string& cache,const std::string& output,bool benchmark,
                 const std::string& map="Expansion01",NorthlightGI::Vec3 center={278.582f,6001.27f,144.73f}){
  snapshotMetadata(d);
  const auto built=NorthlightScenePressureFixture::build(cache,{},center,{},map);
  auto renderer=std::make_unique<WorldRenderer>(d);auto& w=*renderer;w.valid=true;
  w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;w.frames=1;
  assert(w.resources(128,128,D3DFMT_A8R8G8B8));
  auto snapshot=std::make_shared<WorldRenderer::Snapshot>();snapshot->map=map;snapshot->bvh=built.bvh;snapshot->meshPlan=built.plan;snapshot->serial=1;snapshot->atlas.resize(NorthlightGI::probeLayout().atlasSize());snapshot->center=center;
  NorthlightLocalLights::Cache lights(cache+"/lights");assert(lights.loadLights(snapshot->map,&center.x,288,snapshot->localLights));w.active=snapshot;
  const auto uploadStart=GetTickCount();while(w.uploaded.lock()!=snapshot->bvh){NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(!w.failed&&DWORD(GetTickCount()-uploadStart)<30000);must(d->Present(nullptr,nullptr,nullptr,nullptr));}
  snapshotPressure(d,w,output);
  replayUploads(d,w,output);
  // renderPointShadows is an inner pass: reproduce WorldRenderer::render's
  // shadow setup, including linear alpha sampling on the very first face.
  const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff}};
  for(const auto& state:states)must(d->SetRenderState(state.first,state.second));
  for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,filter,D3DTEXF_LINEAR));
  must(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));must(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
  std::memcpy(w.context.camera,&center.x,12);w.projection[0]=1;w.projection[1]=16.f/9;w.projection[2]=1;
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  auto image=[&](IDirect3DSurface9* surface){must(d->GetRenderTargetData(surface,readback));D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> pixels(256*256*4);for(unsigned y=0;y<256;++y)std::memcpy(pixels.data()+y*256*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());return pixels;};
  std::ofstream rows(output+"/timings.tsv"),digests(output+"/faces.tsv");unsigned comparisons=0,omitted=0,nonempty=0,recoveryComparisons=0;
  for(unsigned limit:{4u,16u,64u,128u})for(unsigned pose=0;pose<8;++pose){
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
      if(mode==0)reference[l.sourceId][face]=std::move(pixels);else if(reference.count(l.sourceId)){assert(reference.at(l.sourceId)[face]==pixels);++comparisons;}}
    }
    // Visibility-aware selection may now fill slots formerly wasted offscreen.
    if(mode==1){assert(visible>=reference.size()&&ready==visible&&ready<=limit);omitted+=unsigned(w.atmosphere.lights.count)-ready;}
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
   // Keep the exact selected sources for this view across pressure/recreation;
   // a reset of selection hysteresis is not an image-quality comparison.
   w.localScissorView=view;must(d->BeginScene());w.renderPointShadows(w.atmosphere.lights,true,false);must(d->EndScene());drain(fence);
   std::map<unsigned,NorthlightLocalLights::Light> selected;reference.clear();
   for(unsigned index=0;index<w.pointSlots.size();++index){const auto& slot=w.pointSlots[index];if(!slot.ready)continue;selected[index]=slot.selected;
    for(unsigned face=0;face<6;++face){auto pixels=image(slot.faces[face]);NorthlightBackendLoader::Sha256 hash;hash.update(pixels.data(),pixels.size());
     digests<<limit<<'\t'<<pose<<'\t'<<slot.selected.sourceId<<'\t'<<face<<'\t'<<hash.hex()<<'\n';reference[slot.selected.sourceId][face]=std::move(pixels);}
   }
   for(unsigned recovery=0;recovery<4;++recovery){
    if(recovery<2){w.trimMemory(recovery==1);w.setMemoryPressure(true,recovery==1);}
    else if(recovery==2)w.releasePointGPU();
    else {++w.meshGeneration;w.rebuildTerrainLists();for(auto& slot:w.pointSlots)slot.cacheValid=false;}
    for(const auto& item:selected){w.pointIndex=item.first;must(d->BeginScene());assert(w.renderPointShadow(item.second,true,false));must(d->EndScene());drain(fence);
     for(unsigned face=0;face<6;++face){const auto actual=image(w.pointSlot().faces[face]);const auto& expected=reference.at(item.second.sourceId)[face];
      if(actual!=expected){size_t different=0;for(size_t i=0;i<actual.size();++i)different+=actual[i]!=expected[i];std::printf("RECOVERY_MISMATCH limit=%u pose=%u recovery=%u source=%llu face=%u differentBytes=%zu\n",limit,pose,recovery,(unsigned long long)item.second.sourceId,face,different);std::fflush(stdout);}
      assert(actual==expected);++recoveryComparisons;}}
    w.setMemoryPressure(false,false);w.pointIndex=0;
   }
  }
  if(benchmark){
   std::ofstream orbit(output+"/orbit.tsv"),faces(output+"/orbit-faces.tsv");
   w.releasePointGPU();w.quality.pointShadowLimit=64;
   for(unsigned step=0;step<32;++step){const unsigned pose=(step%16)<8?step%8:7-step%8;
    const float angle=float(pose)*3.14159265359f/4,c=std::cos(angle),s=std::sin(angle),up=std::sqrt(.96f);
    const float inverse[]={-s,c,0,0,.2f*c,.2f*s,up,0,up*c,up*s,-.2f,0,center.x,center.y,center.z,1};std::memcpy(w.context.inverseView,inverse,sizeof inverse);
    // Fixed selection history makes cross-build comparisons independent of
    // wall-clock fade steps. Source transforms and triangle inputs stay real.
    w.pointSelection.reset();w.localLightTracker.reset();w.localLightQpc=0;w.atmosphereWork(w.atmosphere,.1f,3840,2160,0);
    drain(fence);const auto rebuilt=w.pointFacesRebuilt;const auto start=Clock::now();
    must(d->BeginScene());w.renderPointShadows(w.atmosphere.lights,true,false);must(d->EndScene());const double submit=milliseconds(start);drain(fence);const double complete=milliseconds(start);
    unsigned count=0,staticFaces=0;for(const auto& slot:w.pointSlots)if(slot.ready){++count;for(auto* t:slot.cacheFace)staticFaces+=t!=nullptr;
     for(unsigned face=0;face<6;++face){const auto pixels=image(slot.faces[face]);NorthlightBackendLoader::Sha256 hash;hash.update(pixels.data(),pixels.size());faces<<step<<'\t'<<slot.selected.sourceId<<'\t'<<face<<'\t'<<hash.hex()<<'\n';}}
    orbit<<step<<'\t'<<pose<<'\t'<<count<<'\t'<<w.pointDraws<<'\t'<<(w.pointFacesRebuilt-rebuilt)<<'\t'<<staticFaces<<'\t'<<submit<<'\t'<<complete<<'\n';
   }
  }
  assert(comparisons>100&&omitted>0&&nonempty>0);readback->Release();fence->Release();
  std::printf("POINT_VISIBILITY_GPU_PASS exactFaces=%u recoveryFaces=%u offscreenSlots=%u nonemptyFaces=%u realCache=1 gameLaunched=0\n",comparisons,recoveryComparisons,omitted,nonempty);
 }
};
int main(int argc,char** argv){
 assert(argc==5||argc==6||argc==9||argc==10);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[3]);std::wstring path(argv[3],argv[3]+std::strlen(argv[3]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointVisibility";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DADAPTER_IDENTIFIER9 adapter{};must(api->GetAdapterIdentifier(0,0,&adapter));std::printf("GPU_ADAPTER description=%s vendor=%04lx device=%04lx driver=%s\n",adapter.Description,(unsigned long)adapter.VendorId,(unsigned long)adapter.DeviceId,adapter.Driver);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));
 const std::string map=argc>=9?argv[5]:"Expansion01";
 const NorthlightGI::Vec3 center=argc>=9?NorthlightGI::Vec3(float(std::atof(argv[6])),float(std::atof(argv[7])),float(std::atof(argv[8]))):NorthlightGI::Vec3(278.582f,6001.27f,144.73f);
 assert(std::isfinite(center.x)&&std::isfinite(center.y)&&std::isfinite(center.z));
 PointTerrainCoverageTest::run(d,argv[2],argv[4],argc==6||argc==10,map,center);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
