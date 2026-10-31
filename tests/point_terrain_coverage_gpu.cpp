// Production point-shadow camera coverage regression; no game/cache or timings.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointTerrainCoverageTest {
 static void movingLightCapture(IDirect3DDevice9* d){
  WorldRenderer w(d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="moving-lamp";
  std::vector<uint32_t> words={0xfffe0300};
  auto op=[&](unsigned code,std::initializer_list<uint32_t> args){words.push_back(code|(unsigned(args.size())<<24));words.insert(words.end(),args);};
  op(81,{0xa00f0000,0x40400000,0x3f800000,0,0});
  op(31,{0x80000000,0x900f0000});op(31,{0x80000002,0x900f0001});op(31,{0x80000005,0x900f0003});
  op(31,{0x80000000,0xe00f0000});op(31,{0x80000005,0xe0030001});
  op(1,{0x80080000,0xa0550000});op(5,{0x80010001,0x90000001,0xa0000000});op(46,{0xb0010000,0x80000001});
  for(unsigned row=0;row<3;++row)op(9,{0x80000000u|(1u<<(16+row)),0xa0e4201fu+row,0xb0000000,0x90e40000});
  for(unsigned row=0;row<4;++row)op(9,{0xe0000000u|(1u<<(16+row)),0xa0e40002u+row,0x80e40000});
  op(1,{0xe0030001,0x90e40003});words.push_back(0xffff);
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(words.data(),words.size(),*program));assert(program->kernel.singleSkin());
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(words.data(),words.size(),patched));
  IDirect3DVertexShader9* original=nullptr;WorldRenderer::CaptureShader metadata;
  must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(words.data()),&original));
  must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&metadata.replacement));
  metadata.program=program;metadata.skinned=true;metadata.projectionKind=2;metadata.usage=NorthlightShaderConstants::analyze(words.data(),words.size(),2);
  w.captureShaders.emplace(original,metadata);w.actorPrograms.emplace(original,program);
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));must(d->SetVertexDeclaration(declaration));
  struct Vertex {float x,y,z;uint32_t bone;float u,v;};std::vector<Vertex> vertices;
  for(unsigned i=0;i<99;++i)for(auto v:{Vertex{0,0,0,1,0,0},Vertex{1,0,0,1,1,0},Vertex{0,1,0,1,0,1}})vertices.push_back(v);
  NorthlightLightAttachments::Attachment attachment;attachment.sourceId=42;attachment.bone=1;attachment.reference=0;attachment.witness={0,0,0,1,0,0,0,1,0};attachment.point[2]=2;
  attachment.placement[0]=attachment.placement[4]=attachment.placement[8]=1;attachment.placement[9]=10;attachment.placement[10]=20;attachment.placement[11]=30;
  w.active->lightAttachments=std::make_shared<const NorthlightLightAttachments::Records>(NorthlightLightAttachments::Records{attachment});
  w.active->localLights.push_back({{10,20,32},{.2f,.5f,1},1,12,42,3,NorthlightLocalLights::CastShadow});
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;w.projection[0]=w.projection[1]=w.projection[2]=1;
  float constants[1024]{};constants[8]=constants[13]=constants[18]=constants[22]=1;
  for(unsigned bone=0;bone<2;++bone){float* r=constants+4*(31+3*bone);r[0]=r[5]=r[10]=1;r[3]=10;r[7]=20;r[11]=30;}
  must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
  must(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));must(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP));must(d->SetTexture(0,nullptr));
  w.quality.actorShadows=0;w.quality.gi=0;w.captureMode=WorldRenderer::CaptureFresh;w.minSkinnedShadowTriangles=100;
  assert(w.replaySnapshots.configureBudget(32*1048576,16*1048576));
  std::vector<NorthlightLocalLights::Light> moved;
  for(float x:{11.f,12.f}){constants[4*34+3]=x;must(d->SetVertexShaderConstantF(0,constants,256));
   w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
   assert(!w.replays.empty()&&w.replays.back()->count==99&&w.replays.back()->shadowSelected&&!w.replays.back()->shadowSmall);
   const auto& lights=w.lightAttachments.apply(w.active->localLights,moved);assert(lights[0].sourceId==42&&lights[0].position[0]==x&&lights[0].position[2]==32);
  }
  // The same shader can draw a multi-bone character section: keep its threshold.
  vertices.back().bone=0;const auto retained=w.replays.size();
  w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
  assert(w.replays.size()==retained);
  w.minSkinnedShadowTriangles=99;
  w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
  assert(w.replays.size()==retained+1);
  original->Release();declaration->Release();std::puts("POINT_MOVING_LIGHT_CAPTURE_PASS triangles=99 minimum=100 poses=2 sameSource=1");
 }
 static void run(IDirect3DDevice9* d){
  movingLightCapture(d);
  // One cached terrain blocker. The camera may see a different native portion
  // of the same chunk: that is not proof that this blocker was captured live.
  WorldRenderer w(d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->map="authored-point-coverage";w.uploadedMap=w.active->map;w.uploaded=w.active->bvh;w.meshGeneration=1;
  const NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,0},{0,0,1},1,1}};const WORD indices[]={0,1,2,2,1,3};
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices,sizeof indices)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
  must(page.vb->Lock(0,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,sizeof indices,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&w.shadowDecl));must(d->CreatePixelShader(kShadowUnionShader,&w.unionPS));
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);WorldRenderer::Batch batch{};batch.page=0;batch.start=0;batch.count=2;batch.minVertex=0;batch.vertexCount=4;batch.material=0;batch.terrain=true;batch.chunkX=7;batch.chunkY=9;batch.boundsLow={-1,-1,0};batch.boundsHigh={1,1,0};w.batches.push_back(batch);
  w.pointLiveBatches.push_back({0,1,{30,30,0},{31,31,1}});w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;
  const NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,12345,3,NorthlightLocalLights::CastShadow};
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));std::vector<float> reference;
  for(unsigned frame=0;frame<4;++frame){w.liveTerrainChunks.clear();if(frame&1)w.liveTerrainChunks.emplace(7,9);
   must(d->BeginScene());const bool rendered=w.renderPointShadow(light,true,false);must(d->EndScene());assert(rendered&&w.pointSlot().ready);IDirect3DSurface9* face=nullptr;must(w.pointSlot().cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Z,0,&face));must(d->GetRenderTargetData(face,readback));face->Release();D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> image(256*256);for(unsigned y=0;y<256;++y)std::memcpy(image.data()+256*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());
   std::printf("POINT_TERRAIN_FRAME nativeChunkVisible=%u centerDepth=%.9f draws=%u\n",frame&1,image[128*256+128],w.pointDraws);std::fflush(stdout);
   assert(image[128*256+128]<1.f);if(reference.empty())reference=image;else assert(image==reference);
  }
  // An authored model VS using the client's row projection layout, patched by
  // the production patcher. Its plane must match the static reference above.
  const uint32_t original[]={0xfffe0300,
   0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000005,0x900f0001,
   0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x80000005,0xe0030001,
   0x03000009,0xe0010000,0xa0e40002,0x90e40000,0x03000009,0xe0020000,0xa0e40003,0x90e40000,
   0x03000009,0xe0040000,0xa0e40004,0x90e40000,0x03000009,0xe0080000,0xa0e40005,0x90e40000,
   0x02000001,0xe0030001,0x90e40001,0xffff};
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(original,sizeof(original)/4,patched));
  auto replay=std::make_unique<WorldRenderer::Replay>();must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&replay->shader));
  replay->decl=w.shadowDecl;replay->decl->AddRef();replay->stream[0]=page.vb;page.vb->AddRef();replay->stride[0]=sizeof(NorthlightGI::WorldVertex);
  replay->index=page.ib;page.ib->AddRef();replay->type=D3DPT_TRIANGLELIST;replay->indexed=true;
  replay->base=0;replay->min=0;replay->start=0;replay->vertices=4;replay->count=2;
  replay->projectionKind=2;replay->constantUsage.floats={2,4};replay->constantUsage.booleans={};replay->constantUsage.integers={};
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=(i%5)==0?1.f:0.f;
  auto image=[&](){
   IDirect3DSurface9* face=nullptr;must(w.pointSlot().cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Z,0,&face));must(d->GetRenderTargetData(face,readback));face->Release();
   D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> pixels(256*256);
   for(unsigned y=0;y<256;++y)std::memcpy(pixels.data()+256*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());return pixels;
  };
  auto render=[&](const NorthlightLocalLights::Light& lamp,bool fresh,bool actors){
   must(d->BeginScene());const bool rendered=w.renderPointShadow(lamp,fresh,actors);must(d->EndScene());return rendered;
  };
  unsigned transitions=0;
  for(float height:{.11f,.2f,.5f,1.f,2.f,4.f}){
   auto lamp=light;lamp.position[2]=height;w.batches={batch};assert(render(lamp,true,false));const auto expected=image();assert(expected[128*256+128]<1.f);
   w.batches.clear();w.replays.push_back(std::move(replay));assert(render(lamp,true,true));assert(image()==expected);
   replay=std::move(w.replays.back());w.replays.clear();
   w.quality.pointShadowRefreshMs=60000;w.captureDemand=false;
   assert(render(lamp,false,true)&&w.pointSlot().ready&&!w.captureDemand);assert(image()==expected);++transitions;
   w.quality.pointShadowRefreshMs=0;
   auto retained=[&](){w.captureDemand=false;assert(render(lamp,false,true)&&w.pointSlot().ready&&w.captureDemand);const auto actual=image();
    if(actual!=expected){std::printf("POINT_DYNAMIC_DROPOUT expectedDepth=%.9f actualDepth=%.9f\n",expected[128*256+128],actual[128*256+128]);std::fflush(stdout);}
    assert(actual==expected);++transitions;};
   retained();
   // A usable cube mid-cycle remains valid when capture is skipped, including
   // a change back to the default six faces per frame.
   w.pointSlot().schedule.stale();retained();
   for(unsigned change=0;change<5;++change){auto changed=lamp;
    if(change==0)++changed.sourceId;else if(change==1)changed.position[2]+=.1f;else if(change==2)changed.attenuationEnd+=1;
    else if(change==3)changed.attenuationStart+=.1f;else changed.flags^=NorthlightLocalLights::CastShadow;
    w.captureDemand=false;assert(!render(changed,false,true)&&!w.pointSlot().ready&&w.captureDemand);assert(image()==expected);++transitions;
   }
   // Invalidation cannot turn the previous character shadow into an empty
   // static-only cube, nor publish it as ready. The next capture recovers it.
   w.pointSlot().schedule.invalidate();w.pointSlot().cacheValid=false;w.captureDemand=false;
   assert(!render(lamp,false,true)&&!w.pointSlot().ready&&w.captureDemand);assert(image()==expected);++transitions;
   w.replays.push_back(std::move(replay));assert(render(lamp,true,true)&&w.pointSlot().ready&&w.pointSlot().schedule.complete);assert(image()==expected);
   replay=std::move(w.replays.back());w.replays.clear();
  }
  std::printf("POINT_DYNAMIC_CAPTURE_GPU_PASS distances=6 transitions=%u staticReplayExact=1 incompleteNotPublished=1 timings=0\n",transitions);
  // Publication precedes the bounded GPU upload. The old same-map pages,
  // materials and generation remain committed while active names a new BVH.
  // Keep rendering them with this frame's casters throughout that handoff.
  const auto previousScene=w.active;
  w.active=std::make_shared<WorldRenderer::Snapshot>(*previousScene);
  w.active->bvh=std::make_shared<NorthlightGI::BVH>();
  assert(w.uploaded.lock()==previousScene->bvh&&w.uploaded.lock()!=w.active->bvh);
  w.batches={batch};w.quality.pointShadowRefreshMs=0;
  const bool handoff=render(light,true,false);
  std::printf("POINT_UPLOAD_HANDOFF rendered=%u ready=%u\n",unsigned(handoff),unsigned(w.pointSlot().ready));std::fflush(stdout);
  assert(handoff&&w.pointSlot().ready&&image()==reference);
  w.batches.clear();w.replays.push_back(std::move(replay));
  assert(render(light,true,true)&&image()==reference);
  replay=std::move(w.replays.back());w.replays.clear();
  assert(render(light,true,true));const auto empty=image();
  assert(std::all_of(empty.begin(),empty.end(),[](float depth){return depth==1.f;}));
  w.replays.push_back(std::move(replay));
  assert(render(light,true,true)&&image()==reference);
  replay=std::move(w.replays.back());w.replays.clear();
  w.captureDemand=false;
  assert(render(light,false,true)&&w.pointSlot().ready&&w.captureDemand&&image()==reference);
  // Committing the replacement keeps the same result. A different map must
  // still reject the old pages without overwriting the complete cube.
  w.uploaded=w.active->bvh;++w.meshGeneration;w.batches={batch};
  assert(render(light,true,false)&&image()==reference);
  w.active->map="another-map";
  assert(!render(light,true,false)&&!w.pointSlot().ready&&image()==reference);
  w.active->map=w.uploadedMap;
  std::printf("POINT_UPLOAD_HANDOFF_GPU_PASS staticExact=1 currentCasters=1 skippedCaptureRetained=1 commitExact=1 mapMismatchRejected=1\n");
  // A sphere outside the receiver scissor needs no cube work. Preserve its
  // static resources, then force all dynamic faces current when it returns.
  auto lamp=light;w.batches={batch};w.quality.pointShadowRefreshMs=60000;
  w.context.inverseView[14]=1000;
  const float projection[]={1,1,1};
  w.localScissorView=NorthlightLocalLightScissor::view(w.context.inverseView,projection,.1f,3.5f,128,128);
  assert(w.localScissorView.valid);auto* retainedCube=w.pointSlot().cube;const auto updates=w.pointUpdates;
  assert(!render(lamp,true,false)&&!w.pointSlot().ready&&w.pointUpdates==updates);
  assert(w.pointSlot().cube==retainedCube&&!w.pointSlot().schedule.usable);
  w.context.inverseView[14]=0;
  w.localScissorView=NorthlightLocalLightScissor::view(w.context.inverseView,projection,.1f,3.5f,128,128);
  w.quality.pointShadowFacesPerFrame=2;w.captureDemand=false;
  assert(!render(lamp,false,false)&&w.captureDemand&&!w.pointSlot().ready);
  assert(render(lamp,true,false)&&w.pointSlot().ready&&w.pointSlot().schedule.complete);
  assert(!w.pointSlot().faceCycle.active()&&image()==reference);
  // An unknown projection fails open, even for an apparently distant source.
  w.localScissorView.valid=false;lamp.position[0]=1000;
  assert(render(lamp,true,false)&&w.pointSlot().ready);
  std::printf("POINT_VISIBILITY_TRANSITIONS_PASS offscreenNoUpdate=1 resourcesRetained=1 skippedCaptureDeferred=1 reentryAllFaces=1 invalidViewFailOpen=1\n");
  readback->Release();std::printf("POINT_TERRAIN_COVERAGE_GPU_PASS cameraMembershipStates=4 cachedBlockerRetained=1 exactCube=1 sameSource=1 timings=0\n");
 }
 };
int main(int argc,char** argv){assert(argc==3);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointCoverage";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d));PointTerrainCoverageTest::run(d);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);}
