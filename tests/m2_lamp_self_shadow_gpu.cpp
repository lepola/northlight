// Real FGS lamps and production cubes/correction. The optional capture replay
// uses recorded receivers. Actor mode uses actual stock HumanMale skin geometry
// and its native four-bone shader under controlled poses, not a gameplay capture.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#if __has_include("native_fixture_shadow.h")
#define NORTHLIGHT_LIVE_LAMP_TEST 1
#endif
static void must(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT %08lx\n",(unsigned long)hr);std::abort();}}
static std::vector<unsigned char> readBytes(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct Fgr{unsigned magic=0x31524746,w=0,h=0,format=0,stride=0;std::vector<unsigned char> bytes;};
static Fgr readFgr(const std::string& path){auto bytes=readBytes(path);assert(bytes.size()>=20);Fgr im;unsigned h[5];std::memcpy(h,bytes.data(),20);im.magic=h[0];im.w=h[1];im.h=h[2];im.format=h[3];im.stride=h[4];assert(im.magic==0x31524746&&bytes.size()==20+size_t(im.w)*im.h*im.stride);im.bytes.assign(bytes.begin()+20,bytes.end());return im;}
static IDirect3DTexture9* texture(IDirect3DDevice9* d,const Fgr& im){IDirect3DTexture9* t=nullptr;must(d->CreateTexture(im.w,im.h,1,0,D3DFORMAT(im.format),D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l;must(t->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<im.h;++y)std::memcpy((char*)l.pBits+y*l.Pitch,im.bytes.data()+size_t(y)*im.w*im.stride,im.w*im.stride);must(t->UnlockRect(0));return t;}
static Fgr readSurface(IDirect3DDevice9* d,IDirect3DSurface9* s,unsigned stride){D3DSURFACE_DESC desc;must(s->GetDesc(&desc));Fgr im;im.w=desc.Width;im.h=desc.Height;im.format=desc.Format;im.stride=stride;im.bytes.resize(size_t(im.w)*im.h*stride);IDirect3DSurface9* copy=nullptr;must(d->CreateOffscreenPlainSurface(im.w,im.h,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->GetRenderTargetData(s,copy));D3DLOCKED_RECT l;must(copy->LockRect(&l,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<im.h;++y)std::memcpy(im.bytes.data()+size_t(y)*im.w*stride,(char*)l.pBits+y*l.Pitch,im.w*stride);must(copy->UnlockRect());copy->Release();return im;}
static void save(const Fgr& im,const std::string& path){std::ofstream f(path,std::ios::binary);f.write((const char*)&im,20);f.write((const char*)im.bytes.data(),im.bytes.size());assert(f);}
struct PointTerrainCoverageTest{
 static void states(IDirect3DDevice9* d,bool shadow){
  for(const auto& p:std::initializer_list<std::pair<D3DRENDERSTATETYPE,DWORD>>{{D3DRS_ZENABLE,shadow},{D3DRS_ZWRITEENABLE,shadow},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_SRCBLEND,D3DBLEND_ONE},{D3DRS_DESTBLEND,D3DBLEND_ONE},{D3DRS_BLENDOP,D3DBLENDOP_ADD}})must(d->SetRenderState(p.first,p.second));
  for(auto f:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,f,D3DTEXF_LINEAR));
 }
 static void load(WorldRenderer& w,const std::string& root,bool capture,const NorthlightGI::Vec3& center={-8992.5927734375f,786.9718017578f,109.5155334473f}){
  using namespace NorthlightGI;std::vector<std::string> tiles;
  if(capture){for(const auto& file:std::filesystem::directory_iterator(root+"/Azeroth"))if(file.path().extension()==".fg3")tiles.push_back(file.path().string());}
  else tiles.push_back(root+"/tile.fg3");
  const Vec3 radius{144,144,192};
  std::string error;WorldScene scene;assert(loadInstancedScenes(tiles,root+"/models",center-radius,center+radius,scene,error));
  auto snap=std::make_shared<WorldRenderer::Snapshot>();snap->bvh=std::make_shared<BVH>();snap->meshPlan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
  assert(snap->bvh->build(std::move(scene),error,true));assert(NorthlightWorldMesh::buildUploadPlan(snap->bvh->scene(),*snap->meshPlan,error));
  auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();assert(NorthlightWorldMeshPages::build(*snap->meshPlan,*pages,error));assert(NorthlightWorldMeshPages::seal(*snap->meshPlan,pages,error));
  snap->map="Azeroth";snap->serial=1;snap->center=center;snap->atlas.resize(NorthlightGI::probeLayout().atlasSize());
  assert(NorthlightLocalLights::readFile(root+"/sources.fgl",snap->localLights));w.valid=true;w.active=snap;w.frames=1;
  assert(w.resources(128,128,D3DFMT_A8R8G8B8));w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;w.quality.softShadows=false;
  const auto started=GetTickCount();while(w.uploaded.lock()!=snap->bvh){NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(!w.failed&&GetTickCount()-started<30000);}
  w.projection[0]=w.projection[1]=w.projection[2]=1;for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
  std::memcpy(w.context.camera,&center.x,12);
 }
 static std::vector<float> cube(WorldRenderer& w,const NorthlightLocalLights::Light& l,bool replays=false){
  ++w.meshGeneration;w.pointSlot().cacheValid=false;states(w.d,true);must(w.d->BeginScene());assert(w.renderPointShadow(l,true,replays));must(w.d->EndScene());assert(w.pointSlot().ready);
  std::vector<float> result;for(auto* face:w.pointSlot().faces){auto im=readSurface(w.d,face,4);const auto n=result.size();result.resize(n+im.w*im.h);std::memcpy(result.data()+n,im.bytes.data(),im.bytes.size());}return result;
 }
 static void replayProof(WorldRenderer& w,const WorldRenderer::Batch& shell,bool mixed=false,bool indexed=false,bool strip=false,const NorthlightLocalLights::Light* native=nullptr){
  std::vector<uint32_t> words={0xfffe0300};auto op=[&](unsigned c,std::initializer_list<uint32_t> a){words.push_back(c|(unsigned(a.size())<<24));words.insert(words.end(),a);};
  op(81,{0xa00f0000,0x40400000,0x3f800000,0,0});op(31,{0x80000000,0x900f0000});op(31,{0x80000002,0x900f0001});op(31,{0x80000005,0x900f0003});op(31,{0x80000000,0xe00f0000});op(31,{0x80000005,0xe0030001});
  op(1,{0x80080000,0xa0550000});op(5,{0x80010001,0x90000001,0xa0000000});op(46,{0xb0010000,0x80000001});
  for(unsigned r=0;r<3;++r)op(9,{0x80000000u|(1u<<(16+r)),0xa0e4201fu+r,0xb0000000,0x90e40000});
  for(unsigned r=0;r<4;++r)op(9,{0xe0000000u|(1u<<(16+r)),0xa0e40002u+r,0x80e40000});op(1,{0xe0030001,0x90e40003});words.push_back(0xffff);
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(words.data(),words.size(),*program));
  auto p=std::make_unique<WorldRenderer::Replay>();p->constantGroup=unsigned(w.replays.size()+1);must(w.d->CreateVertexShader((DWORD*)words.data(),&p->originalShader));w.actorPrograms[p->originalShader]=program;
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(words.data(),words.size(),patched));must(w.d->CreateVertexShader((DWORD*)patched.data(),&p->shader));
  D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(w.d->CreateVertexDeclaration(decl,&p->decl));
  struct Vertex{float x,y,z;uint32_t bone;float u,v;};std::vector<Vertex> vertices;
  for(const auto& t:*shell.lampTriangles)for(auto v:t)vertices.push_back({v.x,v.y,v.z,0,0,0});
  if(mixed){const auto& scene=w.active->bvh->scene();bool added=false;
    for(const auto& t:scene.triangles)if(!scene.materials[t.material].lampSourceId&&scene.materials[t.material].alphaCutoff==0){
      for(unsigned i:{t.v0,t.v1,t.v2}){const auto v=scene.vertices[i].position;vertices.push_back({v.x,v.y,v.z,0,0,0});}added=true;break;}assert(added);}
  if(strip){std::vector<Vertex> chain;for(size_t i=0;i<vertices.size();i+=3){
    if(!chain.empty()){chain.push_back(chain.back());chain.push_back(vertices[i]);}
    chain.insert(chain.end(),vertices.begin()+i,vertices.begin()+i+3);}vertices=std::move(chain);}
  p->count=unsigned(strip?vertices.size()-2:vertices.size()/3);p->vertices=unsigned(vertices.size());p->type=strip?D3DPT_TRIANGLESTRIP:D3DPT_TRIANGLELIST;p->snapshot.primitiveCount=p->count;p->snapshot.vertexCount=unsigned(vertices.size());p->snapshot.indexed=indexed;p->indexed=indexed;
  auto& s=p->snapshot.streams[0];s.stride=sizeof(Vertex);s.bytes.resize(vertices.size()*sizeof(Vertex));std::memcpy(s.bytes.data(),vertices.data(),s.bytes.size());
  void* upload=nullptr;must(w.d->CreateVertexBuffer(UINT(s.bytes.size()),0,0,D3DPOOL_MANAGED,&p->stream[0],nullptr));must(p->stream[0]->Lock(0,0,&upload,0));std::memcpy(upload,s.bytes.data(),s.bytes.size());must(p->stream[0]->Unlock());p->stride[0]=sizeof(Vertex);
  if(indexed){for(unsigned i=0;i<vertices.size();++i)p->snapshot.indices.push_back(i);
    must(w.d->CreateIndexBuffer(UINT(vertices.size()*4),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&p->index,nullptr));must(p->index->Lock(0,0,&upload,0));std::memcpy(upload,p->snapshot.indices.data(),vertices.size()*4);must(p->index->Unlock());}
  p->constantStorage[31*4]=p->constantStorage[32*4+1]=p->constantStorage[33*4+2]=1;
  p->constantStorage[8]=p->constantStorage[13]=p->constantStorage[18]=p->constantStorage[22]=1;
  if(native){
    p->projectionKind=2;p->constantUsage=NorthlightShaderConstants::analyze(words.data(),words.size(),2);
    p->pointBounds.valid=true;for(unsigned k=0;k<3;++k){p->pointBounds.low[k]=INFINITY;p->pointBounds.high[k]=-INFINITY;
      for(const auto& v:vertices){const float value[]={v.x,v.y,v.z};p->pointBounds.low[k]=std::min(p->pointBounds.low[k],value[k]);p->pointBounds.high[k]=std::max(p->pointBounds.high[k],value[k]);}
      auto* row=p->constantStorage+4*(31+k);for(unsigned j=0;j<3;++j)row[j]=w.context.inverseView[k*4+j];row[3]=0;for(unsigned j=0;j<3;++j)row[3]-=row[j]*w.context.camera[j];}
    p->shared=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(p->snapshot));
#ifdef NORTHLIGHT_LIVE_LAMP_TEST
    WorldRenderer::PointReplayRuns runs;assert(w.pointOwnNativeShell(*p,*native,runs));unsigned kept=0;for(auto run:runs)kept+=run.second;assert(kept>0&&kept<p->count);
    std::printf("NATIVE_LIVE_ENCLOSURE totalTriangles=%u excludedBulb=%u retainedFrame=%u cachedLampCasters=0\n",p->count,p->count-kept,kept);
#endif
    w.replays.push_back(std::move(p));return;
  }
  WorldRenderer::PointReplayRuns runs;assert(w.pointOwnShell(*p,shell,runs));
  if(strip){unsigned kept=0;for(auto r:runs)kept+=r.second;assert(p->count-kept==shell.count);}
  else if(mixed)assert(runs.size()==1&&runs[0].first==shell.count&&runs[0].second==1);else assert(runs.empty());
  p->constantStorage[31*4+3]=.02f;assert(!w.pointOwnShell(*p,shell,runs));p->constantStorage[31*4+3]=0;
  p->cutoff=.5f;assert(w.pointOwnShell(*p,shell,runs));p->cutoff=.75f;assert(!w.pointOwnShell(*p,shell,runs));p->cutoff=-1;
  // Real city coordinates, model-local vertices and a moving view palette.
  // The luminous triangles and the retained metal must not change with the camera.
  assert(w.pointOwnShell(*p,shell,runs));const auto expected=runs;
  const auto savedBytes=s.bytes;float savedInverse[16],savedBank[1024];
  std::memcpy(savedInverse,w.context.inverseView,sizeof savedInverse);std::memcpy(savedBank,p->constantStorage,sizeof savedBank);
  const float origin[]={shell.boundsLow.x,shell.boundsLow.y,shell.boundsLow.z};
  for(auto& v:vertices){v.x-=origin[0];v.y-=origin[1];v.z-=origin[2];}
  std::memcpy(s.bytes.data(),vertices.data(),s.bytes.size());
  for(unsigned frame=0;frame<180;++frame){
    const float yaw=frame*.037f,pitch=.15f+frame*.004f,c=std::cos(yaw),sn=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const float axes[3][3]={{c,sn,0},{-sn*cp,c*cp,sp},{sn*sp,-c*sp,cp}};
    const float eye[]={origin[0]+12*c,origin[1]+12*sn,origin[2]+5+2*sp};
    for(unsigned k=0;k<3;++k){
      auto* row=p->constantStorage+4*(31+k);row[3]=0;
      for(unsigned j=0;j<3;++j){w.context.inverseView[k*4+j]=axes[k][j];row[j]=axes[k][j];row[3]+=axes[k][j]*(origin[j]-eye[j]);}
      w.context.inverseView[12+k]=eye[k];
    }
    assert(w.pointOwnShell(*p,shell,runs)&&runs==expected);
  }
  s.bytes=savedBytes;std::memcpy(w.context.inverseView,savedInverse,sizeof savedInverse);std::memcpy(p->constantStorage,savedBank,sizeof savedBank);
  w.replays.push_back(std::move(p));
 }
 static void fixture(IDirect3DDevice9* d,const std::string& root){
  WorldRenderer w(d);load(w,root,false);const auto l=w.active->localLights[0];const auto owners=NorthlightWmoLampShadow::parts(l,w.batches);assert(!owners.empty());
  auto shell=w.batches[owners.front()];auto proof=std::make_shared<WorldRenderer::Batch::LampTriangles>();shell.count=0;
  for(auto i:owners){auto& b=w.batches[i];proof->insert(proof->end(),b.lampTriangles->begin(),b.lampTriangles->end());shell.count+=b.count;b.lampSourceId=0;
    shell.boundsLow={std::min(shell.boundsLow.x,b.boundsLow.x),std::min(shell.boundsLow.y,b.boundsLow.y),std::min(shell.boundsLow.z,b.boundsLow.z)};
    shell.boundsHigh={std::max(shell.boundsHigh.x,b.boundsHigh.x),std::max(shell.boundsHigh.y,b.boundsHigh.y),std::max(shell.boundsHigh.z,b.boundsHigh.z)};}
  shell.lampTriangles=proof;const auto before=cube(w,l);for(auto i:owners)w.batches[i].lampSourceId=l.sourceId;const auto after=cube(w,l);
  unsigned opened=0,blocked=0,improved=0;for(size_t i=0;i<before.size();++i){opened+=before[i]<.9999f&&after[i]==1.f;blocked+=after[i]<.9999f;improved+=after[i]>before[i]+1e-5f;assert(after[i]+1e-6f>=before[i]);}
  std::printf("CUBE_CHECK parts=%zu batches=%zu draws=%u opened=%u blocked=%u improved=%u minBefore=%g minAfter=%g\n",owners.size(),w.batches.size(),w.pointDraws,opened,blocked,improved,*std::min_element(before.begin(),before.end()),*std::min_element(after.begin(),after.end()));std::fflush(stdout);assert(improved>1000&&blocked>1000);
  auto other=l;other.sourceId^=1;assert(cube(w,other)==before);
  replayProof(w,shell);assert(cube(w,l,true)==after);
  for(bool strip:{false,true})for(bool indexed:{false,true}){w.replays.clear();replayProof(w,shell,true,indexed,strip);const auto mixed=cube(w,l,true);
    for(size_t i=0;i<after.size();++i)assert(std::fabs(mixed[i]-after[i])<1e-5f);}
  // GI can leave replays while ActorShadows=0. A null entry makes any accidental
  // bounds/enclosure/draw read fail, including the new pointOwnShell path.
  w.replays.clear();w.replays.push_back(nullptr);w.captureDemand=false;
  assert(cube(w,l,false)==after&&!w.pointReplayCount&&!w.captureDemand);
  for(const auto& candidates:w.pointReplayCandidates)assert(candidates.empty());
  w.replays.clear();
  std::printf("LAMP_GPU_PASS %s opened=%u frameBlocked=%u foreignExact=1 liveShellExact=1 mixedIndexedAndUP=1 triangleListsAndStrips=1 actorOffNoReplayReads=1\n",root.c_str(),opened,blocked);std::fflush(stdout);
 }
#ifdef NORTHLIGHT_LIVE_LAMP_TEST
 static void nativeMemoBenchmark(WorldRenderer& w,WorldRenderer::Replay& p,const NorthlightLocalLights::Light& light,const std::string& root){
  float inverse[16],camera[3],bank[1024];std::memcpy(inverse,w.context.inverseView,sizeof inverse);std::memcpy(camera,w.context.camera,sizeof camera);std::memcpy(bank,p.constants,sizeof bank);const auto bounds=p.pointBounds;
  WorldRenderer::PointReplayRuns expected;assert(w.pointOwnNativeShell(p,light,expected));LARGE_INTEGER frequency;assert(QueryPerformanceFrequency(&frequency));constexpr unsigned Iterations=1024;
  unsigned kept=0;for(auto run:expected)kept+=run.second;bool compact=false;for(const auto& memo:w.nativeShellMemo)if(memo.mesh.lock()==p.shared){std::size_t bytes=0;for(const auto& stream:memo.ownedMesh.streams)bytes+=stream.bytes.size();assert(memo.ownedMesh.primitiveCount==p.count-kept&&memo.ownedMesh.vertexCount<p.mesh().vertexCount);std::printf("LAMP_NATIVE_WORK {\"case\":\"%s\",\"original_vertices\":%u,\"owned_vertices\":%u,\"owned_triangles\":%u,\"owned_stream_bytes\":%zu,\"all_owned_vertices_verified\":true}\n",std::filesystem::path(root).filename().string().c_str(),p.mesh().vertexCount,memo.ownedMesh.vertexCount,memo.ownedMesh.primitiveCount,bytes);compact=true;break;}assert(compact);
  for(unsigned mode=0;mode<3;++mode){const auto proofs=w.nativeShellProofs,hits=w.nativeShellMemoHits,checks=w.nativeShellPoseChecks;LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    for(unsigned i=0;i<Iterations;++i){auto source=light;
      if(mode){const float angle=mode==1?.01f*float(int(i%11)-5):0.f,c=std::cos(angle),s=std::sin(angle),move=.002f*float(int(i%17)-8);std::memcpy(w.context.inverseView,inverse,sizeof inverse);
        for(unsigned k=0;k<3;++k){w.context.inverseView[k]=c*inverse[k]+s*inverse[4+k];w.context.inverseView[4+k]=-s*inverse[k]+c*inverse[4+k];w.context.camera[k]=camera[k]+(mode==1?move*inverse[k]:0);w.context.inverseView[12+k]=w.context.camera[k];}
        const float translation[]={mode==2?move:0,mode==2?move*.4f:0,0};p.pointBounds=bounds;
        for(unsigned k=0;k<3;++k){source.position[k]+=translation[k];p.pointBounds.low[k]+=translation[k];p.pointBounds.high[k]+=translation[k];auto* row=p.constantStorage+4*(31+k);row[3]=0;for(unsigned j=0;j<3;++j){row[j]=w.context.inverseView[k*4+j];row[3]+=row[j]*(translation[j]-w.context.camera[j]);}}
      }
      WorldRenderer::PointReplayRuns runs;assert(w.pointOwnNativeShell(p,source,runs)&&runs==expected);
    }
    QueryPerformanceCounter(&end);assert(w.nativeShellProofs==proofs&&w.nativeShellMemoHits-hits==Iterations);if(mode)assert(w.nativeShellPoseChecks-checks>0);
    std::printf("LAMP_NATIVE_CPU {\"case\":\"%s\",\"mode\":\"%s\",\"iterations\":%u,\"membership_proofs_added\":%u,\"memo_hits_added\":%u,\"pose_checks_added\":%u,\"cpu_ms_per_call\":%.9f,\"includes_transform_setup\":true}\n",std::filesystem::path(root).filename().string().c_str(),mode==0?"warm-bank":mode==1?"moving-camera":"rigid-pose",Iterations,w.nativeShellProofs-proofs,w.nativeShellMemoHits-hits,w.nativeShellPoseChecks-checks,double(end.QuadPart-begin.QuadPart)*1000/double(frequency.QuadPart)/Iterations);std::fflush(stdout);
    std::memcpy(w.context.inverseView,inverse,sizeof inverse);std::memcpy(w.context.camera,camera,sizeof camera);std::memcpy(p.constantStorage,bank,sizeof bank);p.pointBounds=bounds;
  }
 }
#endif
 static void character(IDirect3DDevice9* d,const std::string& root,const std::string& output,bool soft=false,bool benchmark=false,bool blockedFloor=false,bool partialFloor=false,unsigned daylight=0){
  std::vector<NorthlightLocalLights::Light> sources;assert(NorthlightLocalLights::readFile(root+"/sources.fgl",sources)&&sources.size()==1);const auto l=sources[0];WorldRenderer w(d);load(w,root,false,{l.position[0],l.position[1],l.position[2]});w.quality.softShadows=soft;
  // A corrected emitter position must not move the receiver or its controls.
  float anchor[3];std::memcpy(anchor,l.position,12);if(std::filesystem::exists(root+"/stand-origin.f32")){const auto origin=readBytes(root+"/stand-origin.f32");assert(origin.size()==12);std::memcpy(anchor,origin.data(),12);for(float value:anchor)assert(std::isfinite(value));}
  float basis[9]={1,0,0,0,1,0,0,0,1};if(!blockedFloor&&std::filesystem::exists(root+"/stand-basis.f32")){const auto data=readBytes(root+"/stand-basis.f32");assert(data.size()==sizeof basis);std::memcpy(basis,data.data(),sizeof basis);}
  for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){float dot=0;for(unsigned k=0;k<3;++k){assert(std::isfinite(basis[i*3+k]));dot+=basis[i*3+k]*basis[j*3+k];}assert(std::fabs(dot-float(i==j))<1e-6f);w.context.inverseView[i*4+j]=basis[i*3+j];}
  assert(std::fabs(basis[0]*(basis[4]*basis[8]-basis[5]*basis[7])-basis[1]*(basis[3]*basis[8]-basis[5]*basis[6])+basis[2]*(basis[3]*basis[7]-basis[4]*basis[6])-1)<1e-6f);
  float receiverAnchor[3];std::memcpy(receiverAnchor,anchor,12);if(!blockedFloor&&std::filesystem::exists(root+"/stand-ground.f32")){const auto data=readBytes(root+"/stand-ground.f32");float ground;assert(data.size()==4);std::memcpy(&ground,data.data(),4);assert(std::isfinite(ground)&&basis[6]==0&&basis[7]==0&&basis[8]==1);receiverAnchor[2]=ground+4;}
  const auto world=[&](float x,float y,float z){return NorthlightGI::Vec3{receiverAnchor[0]+basis[0]*x+basis[3]*y+basis[6]*z,receiverAnchor[1]+basis[1]*x+basis[4]*y+basis[7]*z,receiverAnchor[2]+basis[2]*x+basis[5]*y+basis[8]*z};};
  const auto camera=world(2,0,5);std::memcpy(w.context.camera,&camera.x,12);std::memcpy(w.context.inverseView+12,&camera.x,12);w.projection[0]=w.projection[1]=1.4f;w.projection[2]=-1;
  if(std::filesystem::exists(root+"/native-lamp.bin")){
#ifdef NORTHLIGHT_LIVE_LAMP_TEST
    w.nativeFixtures=NorthlightNativeFixtures::load(std::filesystem::path(root).parent_path().string()+"/native-fixtures.bin");assert(w.nativeFixtures);
    const auto snapshot=readBytes(root+"/native-snapshot.bin");NorthlightNativeLights::Snapshot scene;assert(snapshot.size()==sizeof scene);std::memcpy(&scene,snapshot.data(),sizeof scene);assert(NorthlightNativeLights::wellFormed(scene));
    for(unsigned frame=0;frame<30;++frame){const unsigned now=1000+frame*20;w.equipmentLights.observeScene(scene,now,{},w.context.camera);w.equipmentLights.publish(now);}assert(w.equipmentLights.outputCount==1&&w.equipmentLights.fixture(l.sourceId));
#endif
    const auto bytes=readBytes(root+"/native-lamp.bin");unsigned count=0;assert(bytes.size()>=8&&!std::memcmp(bytes.data(),"NLL1",4));std::memcpy(&count,bytes.data()+4,4);assert(count>4&&count<=4096&&bytes.size()==8+count*36);
    WorldRenderer::Batch shell{};auto triangles=std::make_shared<WorldRenderer::Batch::LampTriangles>();triangles->resize(count);std::memcpy(triangles->data(),bytes.data()+8,count*36);shell.lampTriangles=triangles;shell.count=count;w.batches.clear();replayProof(w,shell,false,false,false,&l);
  }
  const unsigned nativeReplays=unsigned(w.replays.size());const auto lampOnly=cube(w,l,nativeReplays!=0);
  Fgr cubeImage;cubeImage.w=256;cubeImage.h=256*6;cubeImage.format=D3DFMT_R32F;cubeImage.stride=4;cubeImage.bytes.resize(lampOnly.size()*4);std::memcpy(cubeImage.bytes.data(),lampOnly.data(),cubeImage.bytes.size());save(cubeImage,output+"/"+std::filesystem::path(root).filename().string()+"-lamp-cube.fgr");
  // A separate authored stand obstacle, not part of the lamp or character.
  std::vector<NorthlightGI::WorldVertex> box;for(unsigned i=0;i<8;++i){const float x=anchor[0]+2.9f+(i&1)*.6f,y=anchor[1]-1.2f+((i>>1)&1)*.7f,z=anchor[2]-4.f+((i>>2)&1)*1.5f;box.push_back({world(x-anchor[0],y-anchor[1],z-anchor[2]),{basis[6],basis[7],basis[8]},0,0});}
  const WORD boxIndices[]={0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
  std::vector<NorthlightMeshPageKey::Key> keys;for(auto id:w.meshStore.table())keys.push_back(w.meshStore.entries()[id].key);const unsigned boxPage=unsigned(keys.size());keys.push_back(NorthlightMeshPageKey::keyOf(box.data(),box.size()*sizeof(box[0]),boxIndices,sizeof boxIndices));
  assert(w.meshStore.beginStaging(keys));w.syncMeshBuffers();const auto boxId=w.meshStore.staging().table.back();auto& gpu=w.meshBuffers[boxId];void* boxUpload=nullptr;
  drop(gpu.vb);drop(gpu.ib);gpu.vbPool=gpu.ibPool=D3DPOOL_MANAGED;
  must(d->CreateVertexBuffer(UINT(box.size()*sizeof(box[0])),0,0,D3DPOOL_MANAGED,&gpu.vb,nullptr));must(d->CreateIndexBuffer(sizeof boxIndices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&gpu.ib,nullptr));
  must(gpu.vb->Lock(0,0,&boxUpload,0));std::memcpy(boxUpload,box.data(),box.size()*sizeof(box[0]));must(gpu.vb->Unlock());must(gpu.ib->Lock(0,0,&boxUpload,0));std::memcpy(boxUpload,boxIndices,sizeof boxIndices);must(gpu.ib->Unlock());
  assert(w.meshStore.setCapacity(boxId,true,UINT(box.size()*sizeof(box[0])))&&w.meshStore.setCapacity(boxId,false,sizeof boxIndices));w.meshStore.commit();
  WorldRenderer::Batch obstacle{};obstacle.page=boxPage;obstacle.count=12;obstacle.vertexCount=8;obstacle.material=unsigned(w.materials.size());obstacle.boundsLow={INFINITY,INFINITY,INFINITY};obstacle.boundsHigh={-INFINITY,-INFINITY,-INFINITY};for(const auto& v:box){obstacle.boundsLow={std::min(obstacle.boundsLow.x,v.position.x),std::min(obstacle.boundsLow.y,v.position.y),std::min(obstacle.boundsLow.z,v.position.z)};obstacle.boundsHigh={std::max(obstacle.boundsHigh.x,v.position.x),std::max(obstacle.boundsHigh.y,v.position.y),std::max(obstacle.boundsHigh.z,v.position.z)};}w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);w.batches.push_back(obstacle);
  const auto empty=cube(w,l,nativeReplays!=0);unsigned boxTexels=0;for(size_t i=0;i<empty.size();++i)boxTexels+=empty[i]+1e-5f<lampOnly[i];
  unsigned boxFaces=0;for(const auto& candidates:w.pointStaticCandidates)boxFaces+=std::find(candidates.begin(),candidates.end(),w.batches.size()-1)!=candidates.end();
  const auto plane=world(0,0,-4);std::printf("BOX_CONTROL texels=%u faces=%u draws=%u lampMin=%g boxMin=%g source=%.9g,%.9g,%.9g standAnchor=%.9g,%.9g,%.9g receiverPoint=%.9g,%.9g,%.9g receiverNormal=%g,%g,%g range=%g owners=%zu\n",boxTexels,boxFaces,w.pointDraws,*std::min_element(lampOnly.begin(),lampOnly.end()),*std::min_element(empty.begin(),empty.end()),l.position[0],l.position[1],l.position[2],anchor[0],anchor[1],anchor[2],plane.x,plane.y,plane.z,basis[6],basis[7],basis[8],l.attenuationEnd,NorthlightWmoLampShadow::parts(l,w.batches).size());std::fflush(stdout);
  const auto data=readBytes(output+"/character.bin");assert(data.size()>=20);unsigned h[5];std::memcpy(h,data.data(),20);
  assert(h[0]==0x31414c4e&&h[1]>100&&h[2]>300&&h[2]%3==0&&h[3]>1&&h[3]<=75&&h[4]<h[3]&&data.size()==20+h[1]*48+h[2]*4);
  if(!blockedFloor&&std::filesystem::exists(root+"/stand-pose-bone.u32")){const auto bone=readBytes(root+"/stand-pose-bone.u32");assert(bone.size()==4);std::memcpy(h+4,bone.data(),4);assert(h[4]<h[3]);}
  const auto shader=readBytes(output+"/character-vs.bin");assert(shader.size()%4==0);std::vector<uint32_t> words(shader.size()/4);std::memcpy(words.data(),shader.data(),shader.size());
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(words.data(),words.size(),*program)&&program->skinned);
  auto p=std::make_unique<WorldRenderer::Replay>();p->constantGroup=unsigned(w.replays.size()+1);must(d->CreateVertexShader((DWORD*)words.data(),&p->originalShader));w.actorPrograms[p->originalShader]=program;
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(words.data(),words.size(),patched));must(d->CreateVertexShader((DWORD*)patched.data(),&p->shader));
  D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  must(d->CreateVertexDeclaration(decl,&p->decl));p->vertices=h[1];p->count=h[2]/3;p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=p->min=p->start=0;p->shadowSkinned=true;p->projectionKind=2;
  p->snapshot.vertexCount=h[1];p->snapshot.primitiveCount=p->count;p->snapshot.indexed=true;p->snapshot.indices.resize(h[2]);std::memcpy(p->snapshot.indices.data(),data.data()+20+h[1]*48,h[2]*4);
  auto& stream=p->snapshot.streams[0];stream.stride=p->stride[0]=48;stream.bytes.assign(data.begin()+20,data.begin()+20+h[1]*48);void* upload=nullptr;
  must(d->CreateVertexBuffer(h[1]*48,0,0,D3DPOOL_MANAGED,&p->stream[0],nullptr));must(p->stream[0]->Lock(0,0,&upload,0));std::memcpy(upload,stream.bytes.data(),stream.bytes.size());must(p->stream[0]->Unlock());
  must(d->CreateIndexBuffer(h[2]*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&p->index,nullptr));must(p->index->Lock(0,0,&upload,0));std::memcpy(upload,p->snapshot.indices.data(),h[2]*4);must(p->index->Unlock());
  p->constantUsage=NorthlightShaderConstants::analyze(words.data(),words.size(),2);p->constantStorage[8]=p->constantStorage[13]=p->constantStorage[18]=p->constantStorage[23]=1;
  float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
  for(unsigned i=0;i<h[1];++i){float point[3];std::memcpy(point,stream.bytes.data()+i*48,12);for(unsigned k=0;k<3;++k){low[k]=std::min(low[k],point[k]);high[k]=std::max(high[k],point[k]);}}
  p->shared=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(p->snapshot));
  w.replays.push_back(std::move(p));auto& actor=*w.replays.back();
  const auto pose=[&](float move,float bend){for(unsigned bone=0;bone<h[3];++bone){auto* c=actor.constantStorage+4*(31+3*bone);std::fill(c,c+12,0.f);c[0]=c[5]=c[10]=.9f;
      c[3]=1.5f-(low[0]+high[0])*.45f+move+(bone==h[4]?bend:0);c[7]=.5f-(low[1]+high[1])*.45f;c[11]=-9.f-low[2]*.9f;}
    actor.pointBounds={};w.captureMode=WorldRenderer::CaptureFresh;w.shadowSelectionDone=true;
    for(unsigned warm=0;warm<8&&!actor.pointBounds.valid;++warm){w.replayBoundsKick();w.replayBoundsJoin();}assert(actor.pointBounds.valid);};
  constexpr unsigned Size=512,Full=Size*2;const auto prefix=output+"/"+std::filesystem::path(root).filename().string();
  Fgr normal;normal.w=normal.h=Size;normal.format=D3DFMT_A32B32G32R32F;normal.stride=16;normal.bytes.resize(Size*Size*16);
  for(unsigned i=0;i<Size*Size;++i){const float n[]={basis[6],basis[7],basis[8],1};std::memcpy(normal.bytes.data()+i*16,n,16);}drop(w.normalBuffer);w.normalBuffer=texture(d,normal);
  Fgr depth;depth.w=depth.h=Full;depth.format=D3DFMT_R32F;depth.stride=4;depth.bytes.resize(Full*Full*4);const float z=64.f/(64.f-.1f)*(1.f-.1f/9.f);
  for(unsigned i=0;i<Full*Full;++i)std::memcpy(depth.bytes.data()+i*4,&z,4);auto* depths=texture(d,depth);
  IDirect3DSurface9 *target=nullptr,*visualDepth=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
  must(d->CreateDepthStencilSurface(Size,Size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&visualDepth,nullptr));
  const float sunlitCut=daylight?.7f:0.f,sunEnergy=daylight?4.f:0.f;w.sourceDirections[0]={basis[6],basis[7],basis[8]};must(d->SetRenderTarget(0,w.baselineSurface));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,daylight?0xff000000:0,1,0));
  const auto softSelected=[&](){IDirect3DPixelShader9* current=nullptr;must(d->GetPixelShader(&current));assert(current==(soft?w.pointSoftLightingPS:w.pointLightingPS));if(soft)assert(w.pointSoftResolvePS&&w.softVisibility);drop(current);};
  const auto correction=[&](float energy=-1.f){w.pointSlot().selectionGain=1;must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));states(d,false);must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));must(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,Full,Full,.1f,64,0,1,1,sunlitCut,1,energy<0?sunEnergy:energy));must(d->EndScene());softSelected();
    const auto image=readSurface(d,target,16);std::vector<float> pixels(Size*Size);for(unsigned i=0;i<pixels.size();++i){float rgb[4];std::memcpy(rgb,image.bytes.data()+i*16,16);pixels[i]=-rgb[0]-rgb[1]-rgb[2];assert(std::isfinite(pixels[i])&&pixels[i]>=-1e-6f);}return pixels;};
  if(benchmark){
    // Equal selected workload on both source revisions, including revisions
    // that did not admit the native light. This does not measure city FPS.
    w.quality.pointShadowLimit=1;pose(0,0);
#ifdef NORTHLIGHT_LIVE_LAMP_TEST
    if(nativeReplays)nativeMemoBenchmark(w,*w.replays[0],l,root);
#endif
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr,*done=nullptr;
    must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));must(d->CreateQuery(D3DQUERYTYPE_EVENT,&done));
    const auto wait=[&](IDirect3DQuery9* query,void* data,DWORD size){const DWORD begun=GetTickCount();for(;;){const HRESULT hr=query->GetData(data,size,D3DGETDATA_FLUSH);if(hr==S_OK)return;must(hr);assert(GetTickCount()-begun<10000);Sleep(0);}};
    LARGE_INTEGER cpuFrequency;assert(QueryPerformanceFrequency(&cpuFrequency));constexpr unsigned Samples=8,Repeats=8;
    for(bool facing:{true,false}){
      for(unsigned i=0;i<Size*Size;++i){const float sign=facing?1.f:-1.f,n[]={sign*basis[6],sign*basis[7],sign*basis[8],1};std::memcpy(normal.bytes.data()+i*16,n,16);}drop(w.normalBuffer);w.normalBuffer=texture(d,normal);
      for(bool refresh:{false,true}){
        const auto frame=[&](){
          if(refresh){++w.meshGeneration;w.pointSlot().cacheValid=false;}
          states(d,true);must(d->BeginScene());assert(w.renderPointShadow(l,true,true));must(d->EndScene());assert(w.pointSlot().ready&&w.pointReplayCount==nativeReplays+1);
          w.pointSlot().selectionGain=1;must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));states(d,false);must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
          must(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,Full,Full,.1f,64,0,1,1,sunlitCut,1,sunEnergy));must(d->EndScene());
        };
        for(unsigned warm=0;warm<4;++warm)frame();must(done->Issue(D3DISSUE_END));wait(done,nullptr,0);softSelected();
        const auto visibilityControl=correction();unsigned shadowPixels=0;float correctionPeak=0;for(float pixel:visibilityControl){shadowPixels+=pixel>1e-5f;correctionPeak=std::max(correctionPeak,pixel);}if(!facing)assert(!shadowPixels&&correctionPeak<=1e-6f);
        for(unsigned sample=0;sample<Samples;++sample){
          const auto rebuilt=w.pointFacesRebuilt;must(disjoint->Issue(D3DISSUE_BEGIN));must(frequency->Issue(D3DISSUE_END));must(start->Issue(D3DISSUE_END));LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
          for(unsigned repeat=0;repeat<Repeats;++repeat)frame();QueryPerformanceCounter(&b);must(end->Issue(D3DISSUE_END));must(disjoint->Issue(D3DISSUE_END));must(done->Issue(D3DISSUE_END));wait(done,nullptr,0);
          UINT64 beginTicks=0,endTicks=0,hz=0;BOOL changed=FALSE;wait(start,&beginTicks,sizeof beginTicks);wait(end,&endTicks,sizeof endTicks);wait(frequency,&hz,sizeof hz);wait(disjoint,&changed,sizeof changed);assert(!changed&&hz&&endTicks>=beginTicks);
          const auto rebuiltFaces=w.pointFacesRebuilt-rebuilt;assert(rebuiltFaces==(refresh?Repeats*6:0));softSelected();
          std::printf("LAMP_BENCH {\"case\":\"%s\",\"soft\":%s,\"sunlit\":%s,\"sun_energy\":%g,\"sunlit_cut\":%g,\"facing\":%s,\"cache\":\"%s\",\"sample\":%u,\"repeats\":%u,\"selected_lights\":1,\"point_shadow_limit\":1,\"cube_resolution\":%u,\"receiver_width\":%u,\"receiver_height\":%u,\"static_faces_rebuilt\":%u,\"last_cube_draws\":%u,\"shadow_pixels\":%u,\"correction_peak\":%g,\"cpu_ms\":%.9f,\"gpu_ms\":%.9f}\n",std::filesystem::path(root).filename().string().c_str(),soft?"true":"false",daylight?"true":"false",sunEnergy,sunlitCut,facing?"true":"false",refresh?"forced-refresh":"warm-static",sample,Repeats,w.PointResolution,Size,Size,rebuiltFaces,w.pointDraws,shadowPixels,correctionPeak,double(b.QuadPart-a.QuadPart)*1000/double(cpuFrequency.QuadPart)/Repeats,double(endTicks-beginTicks)*1000/double(hz)/Repeats);std::fflush(stdout);
        }
      }
    }
    for(auto* query:{start,end,frequency,disjoint,done})query->Release();for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);visualDepth->Release();depths->Release();target->Release();return;
  }
  const DWORD solidCode[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};IDirect3DPixelShader9* solid=nullptr;must(d->CreatePixelShader(solidCode,&solid));
  float directPeak=0,dayDimmingError=0;
  const auto stand=[&](unsigned phase){
    float c[116][4]{};c[0][0]=c[0][1]=1.f/Full;c[0][2]=.1f;c[0][3]=64;c[1][0]=c[1][1]=w.projection[0];c[1][2]=-1;c[2][0]=1;std::memcpy(c[3],w.context.inverseView,64);c[20][2]=1;
    c[33][0]=c[33][1]=Full;c[33][2]=c[33][3]=Size;
    std::memcpy(c[36],l.position,12);c[36][3]=l.attenuationEnd;std::memcpy(c[44],l.diffuse,12);c[44][3]=1.f/(l.attenuationEnd-l.attenuationStart);c[52][0]=c[52][1]=1;
    const auto drawDirect=[&](float cut){c[52][2]=cut;must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));states(d,false);must(d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_COLORVALUE(.025f,.025f,.025f,1),1,0));
    for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);must(d->SetTexture(1,depths));must(d->SetTexture(12,w.baselineLight));must(d->SetTexture(14,w.normalBuffer));
    for(unsigned s:{1u,12u,14u})for(auto f:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(s,f,D3DTEXF_POINT));
    must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));must(d->SetPixelShaderConstantF(0,c[0],116));must(d->SetPixelShader(w.localDirectPS));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->BeginScene());must(w.quad(Size,Size));must(d->EndScene());};drawDirect(sunlitCut);
    if(phase==0){const auto image=readSurface(d,target,16);save(image,prefix+"-direct.fgr");float directLow=INFINITY;for(unsigned i=0;i<Size*Size;++i){float rgb[4];std::memcpy(rgb,image.bytes.data()+i*16,16);const float energy=rgb[0]+rgb[1]+rgb[2]-.075f;directPeak=std::max(directPeak,energy);directLow=std::min(directLow,energy);}assert(directPeak>.01f&&directPeak-directLow>.05f);
      if(daylight){drawDirect(0);const auto night=readSurface(d,target,16);save(night,prefix+"-undimmed-direct.fgr");const float background=float(D3DCOLOR_COLORVALUE(.025f,.025f,.025f,1)&255)/255.f;for(unsigned i=0;i<Size*Size;++i){float dayRGB[4],nightRGB[4];std::memcpy(dayRGB,image.bytes.data()+i*16,16);std::memcpy(nightRGB,night.bytes.data()+i*16,16);for(unsigned channel=0;channel<3;++channel)dayDimmingError=std::max(dayDimmingError,std::fabs((dayRGB[channel]-background)-(nightRGB[channel]-background)*.3f));}assert(dayDimmingError<2e-6f);drawDirect(sunlitCut);}}
    must(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,Full,Full,.1f,64,0,1,1,sunlitCut,1,sunEnergy));must(d->EndScene());softSelected();const auto receiver=readSurface(d,target,16);save(receiver,prefix+"-receiver-"+std::to_string(phase)+".fgr");
    for(unsigned i=0;i<Size*Size;++i){float rgba[4];std::memcpy(rgba,receiver.bytes.data()+i*16,16);for(float channel:rgba)assert(std::isfinite(channel)&&channel>=-.001f);}
    // Presentation only: true lamp and character meshes in neutral flat colors.
    // The floor underneath is the production direct pass plus its correction.
    must(d->SetDepthStencilSurface(visualDepth));must(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,z,0));states(d,true);must(d->SetPixelShader(solid));
    const float a=64.f/(64.f-.1f),b=.1f*a;float matrix[16]={};for(unsigned k=0;k<3;++k){matrix[k*4]=1.4f*basis[k];matrix[k*4+1]=1.4f*basis[3+k];matrix[k*4+2]=-a*basis[6+k];matrix[k*4+3]=-basis[6+k];matrix[12]-=1.4f*basis[k]*w.context.camera[k];matrix[13]-=1.4f*basis[3+k]*w.context.camera[k];matrix[14]+=a*basis[6+k]*w.context.camera[k];matrix[15]+=basis[6+k]*w.context.camera[k];}matrix[14]-=b;
    must(d->SetVertexShader(w.pointShadowVS));must(d->SetVertexShaderConstantF(0,matrix,4));must(d->SetVertexDeclaration(w.shadowDecl));must(d->BeginScene());UINT page=UINT_MAX;
    for(const auto& batch:w.batches){const bool block=batch.page==boxPage;const float color[]={block?.55f:batch.lampSourceId?.9f:.18f,block?.24f:batch.lampSourceId?.65f:.23f,block?.08f:batch.lampSourceId?.15f:.28f,1};must(d->SetPixelShaderConstantF(0,color,1));assert(w.bindMeshPage(batch,page));must(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count));}
    float actorConstants[1024];std::memcpy(actorConstants,actor.constants,sizeof actorConstants);const float projection[]={1.4f,0,0,0,0,1.4f,0,0,0,0,-a,-b,0,0,-1,0};std::memcpy(actorConstants+8,projection,sizeof projection);
    for(unsigned i=0;i<nativeReplays;++i){const auto& lamp=*w.replays[i];float bank[1024];std::memcpy(bank,lamp.constants,sizeof bank);std::memcpy(bank+8,projection,sizeof projection);const float grey[]={.3f,.34f,.4f,1};must(d->SetPixelShaderConstantF(0,grey,1));must(d->SetVertexShader(lamp.originalShader));must(d->SetVertexShaderConstantF(0,bank,256));must(d->SetVertexDeclaration(lamp.decl));must(d->SetStreamSource(0,lamp.stream[0],0,lamp.stride[0]));must(d->SetIndices(nullptr));must(d->DrawPrimitive(lamp.type,lamp.start,lamp.count));}
    const float actorColor[]={.16f,.46f,.7f,1};must(d->SetPixelShaderConstantF(0,actorColor,1));must(d->SetVertexShader(actor.originalShader));must(d->SetVertexShaderConstantF(0,actorConstants,256));must(d->SetVertexDeclaration(actor.decl));must(d->SetStreamSource(0,actor.stream[0],0,48));must(d->SetIndices(actor.index));must(d->DrawIndexedPrimitive(actor.type,0,0,actor.vertices,0,actor.count));must(d->EndScene());
    save(readSurface(d,target,16),prefix+"-stand-"+std::to_string(phase)+".fgr");must(d->SetDepthStencilSurface(nullptr));};
  const auto baseline=correction();auto offCube=empty;auto offBaseline=baseline;if(nativeReplays){offCube=cube(w,l,false);offBaseline=correction();auto held=std::move(w.replays.back());w.replays.pop_back();assert(cube(w,l,true)==empty);w.replays.push_back(std::move(held));}
  std::vector<float> images[3];unsigned changed[3]={},added[3]={},kept=0;double centers[3]={};
  for(unsigned phase=0;phase<3;++phase){pose(phase==1?1.1f:0,phase==2?.65f:0);const auto shadow=cube(w,l,true);assert(w.pointReplayCount==nativeReplays+1);
    std::printf("ACTOR_WORLD_CONTROL phase=%u poseBone=%u low=%.9g,%.9g,%.9g high=%.9g,%.9g,%.9g sourceZ=%.9g\n",phase,h[4],actor.pointBounds.low[0],actor.pointBounds.low[1],actor.pointBounds.low[2],actor.pointBounds.high[0],actor.pointBounds.high[1],actor.pointBounds.high[2],l.position[2]);
    for(size_t i=0;i<shadow.size();++i){assert(shadow[i]<=empty[i]+1e-6f);added[phase]+=shadow[i]+1e-5f<empty[i];if(phase==0)kept+=empty[i]<.9999f&&shadow[i]==empty[i];}
    images[phase]=correction();save(readSurface(d,target,16),prefix+"-correction-"+std::to_string(phase)+".fgr");if(daylight==1)assert(correction(0)==images[phase]&&correction(64)==images[phase]);for(unsigned i=0;i<images[phase].size();++i)if(images[phase][i]>baseline[i]+1e-5f){++changed[phase];centers[phase]+=i%Size;}
    if(changed[phase])centers[phase]/=changed[phase];stand(phase);}
  unsigned poseChanged=0;for(unsigned i=0;i<images[0].size();++i)poseChanged+=std::fabs(images[0][i]-images[2][i])>1e-5f;
  assert(cube(w,l,false)==offCube&&correction()==offBaseline);w.replays.pop_back();assert(cube(w,l,true)==empty&&correction()==baseline);
  w.batches.pop_back();assert(cube(w,l,true)==lampOnly);const auto noBox=correction();unsigned boxPixels=0;for(unsigned i=0;i<baseline.size();++i)boxPixels+=baseline[i]>noBox[i]+1e-5f;
  const bool covered=boxTexels>100&&boxPixels>5&&changed[0]>5&&changed[1]>5&&changed[2]>5&&added[0]>20&&added[1]>20&&added[2]>20&&centers[1]>centers[0]+3&&poseChanged>3&&kept>100;
  const bool blocked=!boxTexels&&!boxPixels&&!changed[0]&&!changed[1]&&!changed[2]&&!added[0]&&!added[1]&&!added[2]&&!poseChanged&&kept>100;
  const bool partial=!boxTexels&&!boxPixels&&changed[0]>5&&!changed[1]&&changed[2]==changed[0]&&!poseChanged&&kept>100;
  const bool legacy=!boxPixels&&!changed[0]&&!changed[1]&&!changed[2]&&!poseChanged&&boxTexels>100&&added[0]>20&&added[1]>20&&added[2]>20&&kept>100&&std::all_of(baseline.begin(),baseline.end(),[](float v){return v==0;});
  const bool passed=daylight==2?legacy:partialFloor?partial:blockedFloor?blocked:covered;std::printf("LAMP_%s_%s %s realHumanMaleTriangles=%u weightedBones=%u controlledPoses=3 poseBone=%u soft=%u sunlit=%u sunEnergy=%g sunlitCut=%g dayDimmingError=%g sunEnergyIndependent=%u directPeak=%g receiverShadowPixels=%u/%u/%u movedCentroid=%.2f/%.2f articulatedPixels=%u foreignStaticTexelsKept=%u separateBoxTexels=%u separateBoxShadowPixels=%u actorCubeTexels=%u/%u/%u actorOffExact=1 removalExact=1\n",daylight==2?"DAY_LEGACY":partialFloor?"FLOOR_PARTIAL":blockedFloor?"FLOOR_BLOCKED":"CHARACTER",passed?"PASS":"FAIL",root.c_str(),h[2]/3,h[3],h[4],unsigned(soft),unsigned(daylight!=0),sunEnergy,sunlitCut,dayDimmingError,unsigned(daylight==1),directPeak,changed[0],changed[1],changed[2],centers[0],centers[1],poseChanged,kept,boxTexels,boxPixels,added[0],added[1],added[2]);std::fflush(stdout);assert(passed);
  for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);solid->Release();visualDepth->Release();depths->Release();target->Release();
 }
#include "captured_lamp_actor.inc"
 static void capture(IDirect3DDevice9* d,const std::string& root,const std::string& prefix,const std::string& output,bool untagged){
  auto bank=readBytes(output+"/direct-constants.f32");assert(bank.size()==116*16);float c[116][4];std::memcpy(c,bank.data(),sizeof c);
  WorldRenderer w(d);load(w,root,true,{c[6][0],c[6][1],c[6][2]});
  std::memcpy(w.context.inverseView,c[3],64);std::memcpy(w.projection,c[1],12);
  drop(w.normalBuffer);w.normalBuffer=texture(d,readFgr(prefix+"normals.fgr"));drop(w.baselineLight);w.baselineLight=texture(d,readFgr(prefix+"baseline.fgr"));
  drop(w.materialDetail);w.materialDetail=texture(d,readFgr(prefix+"lighting-input-g0-s9.fgr"));
  w.materialBumpGain=c[73][0];w.materialBumpDistance=c[73][1];
  const auto depthImage=readFgr(prefix+"depth.fgr"),localImage=readFgr(prefix+"current-local-light.fgr");
  auto* depth=texture(d,depthImage);IDirect3DSurface9* target=nullptr;must(d->CreateRenderTarget(localImage.w,localImage.h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
  std::vector<std::vector<float>> unownedCubes;
  std::vector<uint64_t> ids;for(auto& b:w.batches)ids.push_back(b.lampSourceId);
  for(unsigned mode=0;mode<2;++mode){
   for(size_t i=0;i<w.batches.size();++i)w.batches[i].lampSourceId=mode?ids[i]:0;
   must(d->SetRenderTarget(0,target));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));unsigned owners=0;
   for(const auto& l:w.active->localLights){if(mode)owners+=!NorthlightWmoLampShadow::parts(l,w.batches).empty();const auto pixels=cube(w,l);
    if(!mode)unownedCubes.push_back(pixels);else{const auto& before=unownedCubes[&l-w.active->localLights.data()];unsigned opened=0,blocked=0,improved=0;
     for(size_t i=0;i<pixels.size();++i){opened+=before[i]<.9999f&&pixels[i]==1.f;blocked+=pixels[i]<.9999f;improved+=pixels[i]>before[i]+1e-5f;assert(pixels[i]+1e-6f>=before[i]);}
     std::printf("CAPTURE_CUBE source=%llu opened=%u blocked=%u improved=%u texels=%zu\n",(unsigned long long)l.sourceId,opened,blocked,improved,pixels.size());std::fflush(stdout);}
    w.pointSlot().selectionGain=1;
    must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));states(d,false);must(d->BeginScene());
    assert(w.renderPointLighting(depth,nullptr,depthImage.w,depthImage.h,c[0][2],c[0][3],c[1][3],c[1][3]+1/c[2][0],c[52][1],c[52][2],c[20][2],0));must(d->EndScene());
   }
   save(readSurface(d,target,8),output+(mode?"/correction-after.fgr":"/correction-before.fgr"));
   std::printf("CAPTURE_CORRECTION mode=%u sources=%zu owners=%u\n",mode,w.active->localLights.size(),owners);std::fflush(stdout);if(mode)assert(owners==(untagged?0:w.active->localLights.size()));
  }
  depth->Release();target->Release();
 }
};
int main(int argc,char** argv){assert(argc==6||(argc==7&&std::strcmp(argv[6],"untagged")==0));SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);std::wstring settings(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(settings.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightLampTest";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));
 const std::string mode=argv[4];if(mode=="housing-actor"||mode=="housing-actor-soft"||mode=="housing-actor-no-replay")PointTerrainCoverageTest::housingActor(d,argv[3],argv[5],mode=="housing-actor-soft",mode=="housing-actor-no-replay");else if(mode=="fixture")PointTerrainCoverageTest::fixture(d,argv[3]);else if(mode.rfind("actor",0)==0){const bool bench=mode.find("benchmark")!=std::string::npos,day=mode.find("day")!=std::string::npos,floor=mode.find("floor")!=std::string::npos,soft=mode.find("soft")!=std::string::npos||floor||(bench&&mode.find("hard")==std::string::npos);PointTerrainCoverageTest::character(d,argv[3],argv[5],soft,bench,floor,mode=="actor-floor-partial",day?(mode.find("legacy")!=std::string::npos?2:1):0);}else PointTerrainCoverageTest::capture(d,argv[3],argv[4],argv[5],argc==7);
 d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
