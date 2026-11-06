// Production point-pass batching and offscreen static retention; no game.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <chrono>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointTerrainCoverageTest {
 static void run(IDirect3DDevice9* d){
  WorldRenderer w(d);w.valid=true;w.frames=1;w.quality.pointShadowLimit=64;w.quality.pointShadowFacesPerFrame=6;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="point-stutter-proof";w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.uploadedMap=w.active->map;w.meshGeneration=1;
  NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,8},{0,0,-1},1,1},{{-1,1,8},{0,0,-1},0,1},{{1,-1,8},{0,0,-1},1,0}};
  constexpr unsigned triangles=2048;std::vector<WORD> indices;
  for(unsigned i=0;i<triangles;++i)for(WORD index:{WORD(i%2?3:0),WORD(i%2?4:1),WORD(i%2?5:2)})indices.push_back(index);
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices.data(),indices.size()*2)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(UINT(indices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));
  void* data=nullptr;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,0,&data,0));std::memcpy(data,indices.data(),indices.size()*2);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&w.shadowDecl));must(d->CreatePixelShader(kShadowUnionShader,&w.unionPS));
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);
  for(unsigned i=0;i<triangles;++i){WorldRenderer::Batch b{};b.page=0;b.start=3*i;b.count=1;b.minVertex=i%2?3:0;b.vertexCount=3;b.boundsLow={-1,-1,i%2?8.f:0.f};b.boundsHigh={1,1,i%2?8.f:0.f};w.batches.push_back(b);}
  const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID}};
  for(const auto& state:states)must(d->SetRenderState(state.first,state.second));
  D3DCAPS9 caps{};must(d->GetDeviceCaps(&caps));assert(caps.MaxPrimitiveCount>=triangles);
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));
  auto drain=[&](){must(fence->Issue(D3DISSUE_END));const DWORD started=GetTickCount();HRESULT hr;while((hr=fence->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-started)<10000);SwitchToThread();}must(hr);};
  auto image=[&](){std::vector<float> result(6*256*256);for(unsigned f=0;f<6;++f){must(d->GetRenderTargetData(w.pointSlot().faces[f],readback));D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(result.data()+f*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());}return result;};
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
  auto render=[&](){must(d->BeginScene());assert(w.renderPointShadow(light,true,false));must(d->EndScene());drain();};
  std::vector<float> expected;unsigned referenceDraws=0,mergedDraws=0;
  for(unsigned mode=0;mode<2;++mode){w.pointPrimitiveLimit=mode?caps.MaxPrimitiveCount:1;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();w.pointWork={};render();
   const auto actual=image();if(!mode){expected=actual;referenceDraws=w.pointDraws;}else{assert(actual==expected);mergedDraws=w.pointDraws;assert(w.pointWork.bridged>0&&w.pointWork.bridged<=w.pointWork.triangles/2);}}
  assert(referenceDraws>=triangles&&mergedDraws<referenceDraws/16);
  std::printf("POINT_GAP_GPU_PASS faces=6 exact=1 referenceDraws=%u mergedDraws=%u\n",referenceDraws,mergedDraws);
  // Unknown/excluded gaps, page/index discontinuity, overflow and excessive
  // clipped work remain separate. The real GPU test above supplies the masks.
  const auto saved=w.batches;const auto masks=w.pointBatchMasks;
  const std::vector<size_t> pair={0,2};
  for(unsigned reason=0;reason<7;++reason){w.batches={saved[0],saved[1],saved[2]};w.pointBatchMasks={63,0,63};w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
   if(reason==1)w.pointBatchMasks[1]=63;if(reason==2)++w.batches[1].page;if(reason==3)++w.batches[1].start;
   if(reason==4){w.batches[1].minVertex=UINT_MAX;w.batches[1].vertexCount=2;}
   if(reason==5){w.batches[1].count=3;w.batches[2].start=12;}if(reason==6)w.pointPrimitiveLimit=2;
   size_t at=0;const auto run=w.pointBatchRun(pair,at,5);assert(at==(reason?1u:2u));assert(run.count==(reason?1u:3u));
  }
  w.batches=saved;w.pointBatchMasks=masks;w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
  // ABBA blocks exercise actual cold static faces, with unchanged GPU inputs.
  for(unsigned block=0;block<8;++block){const bool optimized=block%4==1||block%4==2;w.pointPrimitiveLimit=optimized?caps.MaxPrimitiveCount:1;
   double submit=0,complete=0;unsigned draws=0;for(unsigned frame=0;frame<8;++frame){w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();drain();auto start=std::chrono::steady_clock::now();must(d->BeginScene());assert(w.renderPointShadow(light,true,false));must(d->EndScene());submit+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();draws+=w.pointDraws;drain();complete+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
   assert(image()==expected);std::printf("POINT_GAP_TIMING block=%u optimized=%u submitMs=%.6f completeMs=%.6f draws=%u\n",block,unsigned(optimized),submit/8,complete/8,draws/8);std::fflush(stdout);
  }
  w.pointPrimitiveLimit=caps.MaxPrimitiveCount;w.releasePointGPU();w.localScissorView={};
  NorthlightLocalLightSelection::Selection selection;selection.count=1;selection.sources[0]=light;
  auto pool=[&](bool fresh=true){must(d->BeginScene());w.renderPointShadows(selection,fresh,false);must(d->EndScene());drain();w.pointIndex=0;};
  auto keeps=[&](){unsigned n=0;for(const auto& keep:w.pointKeeps){n+=keep.slot.cacheValid;assert(!keep.slot.cube);for(auto* face:keep.slot.faces)assert(!face);}return n;};
  pool();const auto original=image();auto* retained=w.pointSlot().cacheFace[5];assert(retained);
  unsigned staticFaces=0;for(auto* t:w.pointSlot().cacheFace)staticFaces+=t!=nullptr;assert(staticFaces==2);
  std::printf("POINT_LAZY_STATIC_GPU_PASS staticFaces=%u staticKiB=%u oldStaticKiB=1536 cubePixels=exact\n",staticFaces,staticFaces*256);
  selection.count=0;pool();assert(keeps()==1&&!w.pointSlot().cube);
  selection.count=1;pool(false);assert(w.pointWork.restored==1&&!w.pointSlot().ready&&!w.pointSlot().schedule.usable&&w.captureDemand);
  pool();assert(w.pointWork.statics==0&&w.pointSlot().cacheFace[5]==retained&&image()==original&&w.pointSlot().schedule.complete);
  for(unsigned mutation=0;mutation<5;++mutation){selection.count=0;pool();assert(keeps()==1);
   if(mutation==0){for(auto& b:w.batches)b.boundsLow.z=b.boundsHigh.z+=.25f;for(auto& v:vertices)v.position.z+=.25f;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());++w.meshGeneration;}
   if(mutation==1)selection.sources[0].position[0]+=4.f;
   if(mutation==2){w.setMemoryPressure(true,true);assert(!keeps());w.setMemoryPressure(false,false);}
   if(mutation==3){w.active->map="replacement-map";w.uploadedMap=w.active->map;}
   if(mutation==4){w.invalidateShadowCache();assert(!keeps());}
   selection.count=1;pool();assert(w.pointWork.statics>0&&w.pointSlot().ready);const auto changed=image();
   w.releasePointGPU();pool();assert(image()==changed);
  }
  const auto populated=w.batches;w.batches.clear();++w.meshGeneration;pool();
  for(auto* t:w.pointSlot().cacheFace)assert(!t);const auto empty=image();assert(std::all_of(empty.begin(),empty.end(),[](float v){return v==1.f;}));
  selection.count=0;pool();assert(keeps()==1);selection.count=1;pool();assert(w.pointWork.restored==1&&image()==empty);
  w.batches=populated;++w.meshGeneration;pool();assert(w.pointWork.statics>0);
  // Hard limits, zero budget, LRU saturation and device-resource release.
  selection.count=9;for(unsigned i=0;i<9;++i){selection.sources[i]=light;selection.sources[i].sourceId=100+i;}
  pool();selection.count=0;pool();assert(keeps()==8);
  w.quality.pointShadowLimit=1;pool();assert(keeps()<=1);selection.count=1;pool();assert(!keeps());
  w.quality.pointShadowLimit=0;pool();assert(!keeps());for(const auto& slot:w.pointSlots)assert(!slot.cube);
  w.releasePointGPU();assert(!keeps());
  std::puts("POINT_STATIC_KEEP_GPU_PASS unchangedReentry=exact freshDynamic=required geometryLightMapMutation=exact pressureLimitInvalidation=cleared lruMax=8");
  // Consecutive native model packets can share one exact draw after upload.
  // A strip, emitter exclusion, pose/material/state boundary remains separate.
  w.batches.clear();w.quality.pointShadowLimit=64;w.quality.pointShadowRefreshMs=0;
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
  const uint32_t shaderSource[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000005,0x900f0001,
   0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x80000005,0xe0030001,
   0x03000009,0xe0010000,0xa0e40002,0x90e40000,0x03000009,0xe0020000,0xa0e40003,0x90e40000,
   0x03000009,0xe0040000,0xa0e40004,0x90e40000,0x03000009,0xe0080000,0xa0e40005,0x90e40000,
   0x02000001,0xe0030001,0x90e40001,0xffff};
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(shaderSource,sizeof shaderSource/4,patched));IDirect3DVertexShader9* shader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader));
  for(unsigned i=0;i<32;++i){auto p=std::make_unique<WorldRenderer::Replay>();p->shader=shader;shader->AddRef();p->decl=w.shadowDecl;w.shadowDecl->AddRef();p->stream[0]=page.vb;page.vb->AddRef();p->stride[0]=sizeof(NorthlightGI::WorldVertex);p->index=page.ib;page.ib->AddRef();
   p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=0;p->min=0;p->vertices=6;p->start=3*i*64;p->count=64;p->projectionKind=2;p->constantUsage.floats={2,4};p->pointBounds.valid=true;
   const float low[]={-1,-1,0},high[]={1,1,9};std::memcpy(p->pointBounds.low,low,12);std::memcpy(p->pointBounds.high,high,12);w.replays.push_back(std::move(p));}
  shader->Release();
  IDirect3DTexture9* cutout=nullptr;must(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cutout,nullptr));D3DLOCKED_RECT lock{};must(cutout->LockRect(0,&lock,nullptr,0));
  for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=(x==y)?0xffffffffu:0x00ffffffu;must(cutout->UnlockRect(0));
  unsigned replayBefore=0,replayAfter=0;
  for(unsigned alpha=0;alpha<2;++alpha){for(auto& p:w.replays){p->cutoff=alpha?.5f:-1.f;if(p->texture)p->texture->Release();p->texture=alpha?cutout:nullptr;if(p->texture)p->texture->AddRef();}
   std::vector<float> reference;std::array<unsigned,6> weights{};
   for(unsigned mode=0;mode<2;++mode){w.pointPrimitiveLimit=mode?caps.MaxPrimitiveCount:1;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();w.pointWork={};must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
    if(!mode){reference=image();replayBefore=w.pointWork.replay;std::copy_n(w.pointSlot().faceWeights,6,weights.begin());}
    else{assert(image()==reference);replayAfter=w.pointWork.replay;assert(replayAfter<replayBefore/16&&w.pointWork.replayMerged>0);assert(std::equal(weights.begin(),weights.end(),w.pointSlot().faceWeights));}
   }
  }
  const std::vector<size_t> adjacent={0,1};std::map<size_t,WorldRenderer::PointReplayRuns> splits;
  auto& next=*w.replays[1];const auto& firstReplay=*w.replays[0];
  for(unsigned reason=0;reason<10;++reason){
   if(reason==1)++next.constantGroup;if(reason==2)++next.stride[0];if(reason==3)++next.start;if(reason==4)next.type=D3DPT_TRIANGLESTRIP;
   if(reason==5)next.cutoff=-1;if(reason==6)next.constantUsage.booleans={0,1};if(reason==7)next.offset[0]=4;
   if(reason==8)splits[1]={{0,1}};if(reason==9){next.min=UINT_MAX;next.vertices=2;}
   size_t at=0;w.pointPrimitiveLimit=caps.MaxPrimitiveCount;const auto run=w.pointReplayRun(adjacent,at,splits,false);assert(at==(reason?1u:2u)&&run.count==(reason?64u:128u));
   next.constantGroup=firstReplay.constantGroup;next.stride[0]=firstReplay.stride[0];next.start=192;next.type=firstReplay.type;next.cutoff=firstReplay.cutoff;next.constantUsage.booleans={};next.offset[0]=0;next.min=0;next.vertices=6;splits.clear();
  }
  w.replays.clear();cutout->Release();
  std::printf("POINT_REPLAY_COALESCE_GPU_PASS opaqueCutout=exact faces=6 beforeDraws=%u afterDraws=%u poseStateStripEmitterOverflow=barriers faceWeights=unchanged\n",replayBefore,replayAfter);
  readback->Release();fence->Release();
 }
};
int main(int argc,char** argv){
 assert(argc==3);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);std::wstring path(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointStutter";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));PointTerrainCoverageTest::run(d);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
