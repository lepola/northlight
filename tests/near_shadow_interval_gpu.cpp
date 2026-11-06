// Production render loop: retained environment must never retain moving poses.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct WorldScreenEffectsTest {
 static std::vector<float> image(IDirect3DDevice9* d,IDirect3DSurface9* surface){
  IDirect3DSurface9* read=nullptr;must(d->CreateOffscreenPlainSurface(1024,1024,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr));
  must(d->GetRenderTargetData(surface,read));D3DLOCKED_RECT lock{};must(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
  std::vector<float> pixels(1024*1024);for(unsigned y=0;y<1024;++y)std::memcpy(pixels.data()+y*1024,static_cast<char*>(lock.pBits)+y*lock.Pitch,4096);
  must(read->UnlockRect());read->Release();return pixels;
 }
 static void setup(WorldRenderer& w,unsigned interval,unsigned detail){
  w.valid=true;w.traceContext=2;w.gpuDiagnosticArmed=false;w.quality.nearShadowInterval=interval;w.quality.farShadowInterval=4;w.quality.actorShadowDetail=detail;
  w.quality.gi=0;w.quality.pointShadows=0;w.quality.fogClouds=0;w.effects.gi=w.effects.fog=false;
  for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=i%5==0?1.f:0.f;
  w.projection[0]=w.projection[1]=w.projection[2]=1;
  w.context.lightDirection[0]=.4f;w.context.lightDirection[1]=.2f;w.context.lightDirection[2]=.8944272f;
  w.sourceDirections[0]={.4f,.2f,.8944272f};w.sourceDirections[1]={-.2f,.3f,.9327379f};
  w.sourceColors[0]=w.sourceGlowColors[0]={.6f,.5f,.4f};w.sourceColors[1]=w.sourceGlowColors[1]={.1f,.2f,.3f};w.sourceWeights[0]=w.sourceWeights[1]=1;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->map="near-interval-fixture";w.lastRequest.map=w.active->map;
  w.uploaded=w.active->bvh;w.uploadedMap=w.active->map;w.meshGeneration=1;
  const NorthlightGI::WorldVertex vertices[]={{{-20,-20,-2},{0,0,1},0,0},{{20,-20,-2},{0,0,1},1,0},{{-20,20,-2},{0,0,1},0,1},{{20,20,-2},{0,0,1},1,1}};const WORD indices[]={0,1,2,2,1,3};
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices,sizeof indices)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(w.d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(w.d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
  must(page.vb->Lock(0,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,sizeof indices,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);WorldRenderer::Batch batch{};batch.page=0;batch.count=2;batch.vertexCount=4;batch.terrain=true;batch.chunkX=7;batch.chunkY=9;batch.boundsLow={-20,-20,-2};batch.boundsHigh={20,20,-2};w.batches.push_back(batch);w.rebuildTerrainLists();
  assert(w.resources(64,64,D3DFMT_A8R8G8B8)&&w.geometryResources(64,64));
 }
 static void pose(WorldRenderer& w,float x){
  if(!w.replays.empty()){w.replays[0]->constantStorage[4*31+3]=x;w.captureMode=WorldRenderer::CaptureFresh;w.shadowSelectionDone=true;return;}
  auto p=std::make_unique<WorldRenderer::Replay>();
  std::vector<uint32_t> original={0xfffe0300};
  auto op=[&](unsigned code,std::initializer_list<uint32_t> args){original.push_back(code|(unsigned(args.size())<<24));original.insert(original.end(),args);};
  op(81,{0xa00f0000,0x40400000,0x3f800000,0,0});
  op(31,{0x80000000,0x900f0000});op(31,{0x80000002,0x900f0001});op(31,{0x80000005,0x900f0003});
  op(31,{0x80000000,0xe00f0000});op(31,{0x80000005,0xe0030001});
  op(1,{0x80080000,0xa0550000});op(5,{0x80010001,0x90000001,0xa0000000});op(46,{0xb0010000,0x80000001});
  for(unsigned row=0;row<3;++row)op(9,{0x80000000u|(1u<<(16+row)),0xa0e4201fu+row,0xb0000000,0x90e40000});
  for(unsigned row=0;row<4;++row)op(9,{0xe0000000u|(1u<<(16+row)),0xa0e40002u+row,0x80e40000});
  op(1,{0xe0030001,0x90e40003});original.push_back(0xffff);
  must(w.d->CreateVertexShader(reinterpret_cast<const DWORD*>(original.data()),&p->originalShader));
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(original.data(),original.size(),*program));w.actorPrograms[p->originalShader]=program;
  for(unsigned i=0;i<16;++i)p->constantStorage[8+i]=i%5==0?1.f:0.f;
  p->constantStorage[4*31]=p->constantStorage[4*32+1]=p->constantStorage[4*33+2]=1;
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(original.data(),original.size(),patched));must(w.d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&p->shader));
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(w.d->CreateVertexDeclaration(elements,&p->decl));
  struct Vertex{float x,y,z;uint32_t bone;float u,v;};const Vertex face[]={{-1,-1,2,0,0,0},{1,-1,2,0,1,0},{-1,1,2,0,0,1},{-1,1,2,0,0,1},{1,-1,2,0,1,0},{1,1,2,0,1,1}};
  std::vector<Vertex> vertices;for(unsigned i=0;i<12;++i)vertices.insert(vertices.end(),std::begin(face),std::end(face));
  auto& mesh=p->snapshot;mesh.streams[0].bytes.resize(vertices.size()*sizeof(Vertex));std::memcpy(mesh.streams[0].bytes.data(),vertices.data(),mesh.streams[0].bytes.size());mesh.streams[0].stride=sizeof(Vertex);mesh.vertexCount=72;mesh.primitiveCount=24;p->shared=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(mesh));
  p->stride[0]=sizeof(Vertex);p->vertices=72;p->count=24;p->type=D3DPT_TRIANGLELIST;p->indexed=false;p->projectionKind=2;p->constantUsage.floats={2,32};
  p->shadowSkinned=true;p->constantStorage[4*31+3]=x;
  w.replays.push_back(std::move(p));w.captureMode=WorldRenderer::CaptureFresh;w.shadowSelectionDone=true;
  for(unsigned warm=0;warm<8&&!w.replays[0]->pointBounds.valid;++warm){w.replayBoundsKick();w.replayBoundsJoin();}
 }
 static void run(IDirect3DDevice9* d){
  IDirect3DTexture9* depth=nullptr;IDirect3DSurface9* target=nullptr;must(d->CreateTexture(64,64,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&depth,nullptr));D3DLOCKED_RECT lock{};must(depth->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=.5f;must(depth->UnlockRect(0));
  must(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));unsigned cases=0,reuses=0,changes=0;
  for(unsigned detail:{0u,1u})for(unsigned interval:{2u,4u,16u}){
   auto reference=std::make_unique<WorldRenderer>(d),retained=std::make_unique<WorldRenderer>(d);setup(*reference,1,detail);setup(*retained,interval,detail);std::vector<float> previous;
   for(unsigned frame=0;frame<21;++frame){const float x=frame%3==0?0.f:frame%3==1?14.f:-4.f;
    if(frame==18){retained->releaseNearEnvironment();for(auto& retry:retained->nearEnvironmentRetry)retry=GetTickCount()+60000;}
    if(frame>=19){NorthlightSettings::Config config;config.quality=retained->quality;config.quality.nearShadowInterval=frame==19?1:interval;retained->temporalValid=true;retained->applyLiveSettings(config);assert(!retained->temporalValid&&!retained->nearEnvironment[0]&&!retained->nearShadow[0].valid);}
    for(auto* w:{reference.get(),retained.get()}){pose(*w,x);must(d->BeginScene());const bool ok=w->render(target,depth,64,64,D3DFMT_A8R8G8B8,1,1000,0,1,1,nullptr,nullptr);must(d->EndScene());if(!ok)std::printf("render failed: %s\n",w->skipReason);assert(ok);}
    for(unsigned source=0;source<2;++source){const auto expected=image(d,reference->shadowSurface[source*2]);const auto actual=image(d,retained->shadowSurface[source*2]);
     if(expected!=actual){std::printf("NEAR_MISMATCH detail=%u interval=%u frame=%u source=%u\n",detail,interval,frame,source);std::fflush(stdout);}assert(expected==actual);
     assert(std::any_of(actual.begin(),actual.end(),[](float v){return v<1.f;}));
     if(detail){if(frame==0)assert(reference->actorCasterCount[source]>0&&retained->actorCasterCount[source]>0);assert(reference->replays[0]->pointBounds.valid&&retained->replays[0]->pointBounds.valid);assert(reference->actorValid[source]&&retained->actorValid[source]);assert(image(d,reference->actorShadowSurface[source])==image(d,retained->actorShadowSurface[source]));}
     if(!detail&&source==0){if(!previous.empty()){assert(previous!=actual);++changes;}previous=actual;}++cases;
    }
   }
   assert(retained->nearEnvironment[0]&&retained->nearEnvironment[1]);
   assert(retained->nearReuses>0);reuses+=retained->nearReuses;
   std::printf("NEAR_INTERVAL_CASE detail=%u interval=%u nearReuses=%u farReuses=%u\n",detail,interval,retained->nearReuses,retained->farReuses);std::fflush(stdout);
  }
  for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);depth->Release();target->Release();
  std::printf("NEAR_INTERVAL_PASS cases=%u movingChanges=%u environmentReuses=%u sources=2 detailOnOff=1 fallback=1 liveIntervalChange=1\n",cases,changes,reuses);
 }
};
int main(int argc,char** argv){
 assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NearInterval";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* device=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
 WorldScreenEffectsTest::run(device);device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
