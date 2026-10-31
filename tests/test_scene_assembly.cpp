#include "frozen_world_gi.cpp"
#include "world_gi.cpp"
#include "shadow_terrain.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <new>
#include <cstdio>
#include <cstdlib>
namespace fs=std::filesystem;
using namespace NorthlightGI;
static int64_t failAllocation=-1;
void* operator new(size_t n){if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}if(failAllocation>0)--failAllocation;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
// std::stable_sort may request its temporary buffer through nothrow new.
// Keep that allocation in the same fault-injected malloc/free domain.
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}

template<class A,class B> static void bits(const A& a,const B& b){static_assert(sizeof(A)==sizeof(B));assert(!std::memcmp(&a,&b,sizeof a));}
template<class A,class B> static void placement(const A& a,const B& b){assert(a.uid==b.uid&&a.category==b.category&&a.modelKey==b.modelKey);bits(a.matrix,b.matrix);bits(a.translation,b.translation);bits(a.low,b.low);bits(a.high,b.high);}
template<class A,class B> static void scene(const A& a,const B& b,bool provenance=true){
 assert(a.sourceIdentity==b.sourceIdentity&&a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size()&&a.completePlacements.size()==b.completePlacements.size());
 for(size_t i=0;i<a.vertices.size();++i)bits(a.vertices[i],b.vertices[i]);for(size_t i=0;i<a.triangles.size();++i)bits(a.triangles[i],b.triangles[i]);
 for(size_t i=0;i<a.materials.size();++i){const auto&x=a.materials[i];const auto&y=b.materials[i];bits(x.albedo,y.albedo);bits(x.alphaCutoff,y.alphaCutoff);assert(x.width==y.width&&x.height==y.height&&x.rgba==y.rgba&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.terrain==y.terrain&&x.wmo==y.wmo);}
 for(size_t i=0;i<a.completePlacements.size();++i)placement(a.completePlacements[i],b.completePlacements[i]);
 if(provenance){assert(a.sourcePieces.size()==b.sourcePieces.size());for(size_t i=0;i<a.sourcePieces.size();++i){const auto&x=a.sourcePieces[i];const auto&y=b.sourcePieces[i];assert(x.sourceIdentity==y.sourceIdentity&&x.membership[0]==y.membership[0]&&x.membership[1]==y.membership[1]&&x.vertexBase==y.vertexBase&&x.vertexCount==y.vertexCount&&x.triangleBase==y.triangleBase&&x.triangleCount==y.triangleCount&&x.materialMap==y.materialMap&&x.complete==y.complete);placement(x.placement,y.placement);}}
}
template<class S> static S model(){S s;for(unsigned y=0;y<=8;++y)for(unsigned x=0;x<=24;++x){typename decltype(s.vertices)::value_type v;v.position={float(x)*8,float(y)*8,float((x+y)%3)*.125f};v.normal={.2f,.3f,1};v.u=float(x)*.17f;v.v=float(y)*.13f;s.vertices.push_back(v);}for(unsigned y=0;y<8;++y)for(unsigned x=0;x<24;++x){uint32_t a=y*25+x;s.triangles.push_back({a,a+1,a+26,(x+y)%4});s.triangles.push_back({a,a+26,a+25,(x+y)%4});}s.materials.resize(4);for(unsigned i=0;i<4;++i){auto&m=s.materials[i];m.albedo={float(i)*.2f,.7f,.1f};m.width=m.height=2;m.alphaCutoff=.5f;m.addressU=i%3;m.addressV=(i+1)%3;m.rgba={1,2,3,255,4,5,6,0,7,8,9,255,10,11,12,255};}return s;}
static void decodedEqual(const WorldScene& a,const WorldScene& b){
 assert(a.sourceIdentity==b.sourceIdentity&&a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size());
 for(size_t i=0;i<a.vertices.size();++i)bits(a.vertices[i],b.vertices[i]);
 for(size_t i=0;i<a.triangles.size();++i)bits(a.triangles[i],b.triangles[i]);
 for(size_t i=0;i<a.materials.size();++i){const auto& x=a.materials[i];const auto& y=b.materials[i];bits(x.albedo,y.albedo);bits(x.alphaCutoff,y.alphaCutoff);
  assert(x.width==y.width&&x.height==y.height&&x.texels()==y.texels()&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.terrain==y.terrain&&x.wmo==y.wmo&&x.pointOnly==y.pointOnly&&x.lampLocalId==y.lampLocalId&&x.lampSourceId==y.lampSourceId);}
 assert(a.completePlacements.size()==b.completePlacements.size());for(size_t i=0;i<a.completePlacements.size();++i)placement(a.completePlacements[i],b.completePlacements[i]);
}
// Allocation callback order is not part of the assembly output contract.
static unsigned assemblyChecks=0;
static void appendCases(){
 auto a=model<WorldScene>();auto b=model<FrozenGI::WorldScene>();
 for(unsigned test=0;test<80;++test){WorldScene x;FrozenGI::WorldScene y;NorthlightGI::Instance i;FrozenGI::Instance j;float matrix[9]={1,0,0,0,1,0,0,0,1};if(test%5==1){const float t[]={0,-1.2f,0,2,0,0,0,0,.75f};std::copy(t,t+9,matrix);}if(test%5==2)matrix[0]=-1;if(test%5==3)matrix[0]=0;if(test%5==4)matrix[0]=std::numeric_limits<float>::max();if(test==79)matrix[0]=std::numeric_limits<float>::quiet_NaN();std::copy(matrix,matrix+9,i.matrix);std::copy(matrix,matrix+9,j.matrix);i.translation={-17.f,3.f,-0.f};j.translation={-17.f,3.f,-0.f};i.terrain=j.terrain=test%2;i.wmo=j.wmo=test%3==0;
  const float edge=test%4==0?-1000.f:float(test%4)*32;Vec3 lo{edge,-20,-10},hi{edge+125,58,10};FrozenGI::Vec3 flo{lo.x,lo.y,lo.z},fhi{hi.x,hi.y,hi.z};std::vector<uint32_t> r(4,UINT32_MAX),s(4,UINT32_MAX);uint64_t tb=0,tc=0,m[2]={},n[2]={};std::string e,f;std::vector<uint64_t> allowedA,allowedB;
  auto aa=[&](uint64_t z){allowedA.push_back(z);return test<50||allowedA.size()!=test-49;};auto ab=[&](uint64_t z){allowedB.push_back(z);return test<50||allowedB.size()!=test-49;};
  bool ok=NorthlightGI::appendInstance(a,i,lo,hi,x,r,tb,e,aa,{},0,0,test%2,m),old=FrozenGI::appendInstance(b,j,flo,fhi,y,s,tc,f,ab,{},0,0,test%2,n);if(ok&&old){assert(e==f&&r==s&&tb==tc);bits(m,n);scene(x,y);}else if(ok){assert(!old);FrozenGI::WorldScene reference;std::vector<uint32_t> remap(4,UINT32_MAX);uint64_t texture=0,membership[2]={};std::string error;assert(FrozenGI::appendInstance(b,j,flo,fhi,reference,remap,texture,error,{}, {},0,0,test%2,membership));assert(r==remap&&tb==texture);bits(m,membership);scene(x,reference);}else{assert(!e.empty());assert(!old||test>=50);}++assemblyChecks;
 }
 // A model whose vertex remap exceeds the allowed contiguous request still produces the same scene.
 {auto large=a;auto frozen=b;large.vertices.resize(300000);frozen.vertices.resize(300000);WorldScene out;FrozenGI::WorldScene reference;NorthlightGI::Instance i;FrozenGI::Instance j;for(unsigned k=0;k<9;++k)i.matrix[k]=j.matrix[k]=(k%4==0)?1.f:0.f;std::vector<uint32_t> r(4,UINT32_MAX),q(4,UINT32_MAX);uint64_t tb=0,tc=0;std::string e,f;unsigned denied=0;auto limited=[&](uint64_t bytes){if(bytes>1024u*1024u){++denied;return false;}return true;};assert(NorthlightGI::appendInstance(large,i,{-1,-1,-1},{200,100,10},out,r,tb,e,limited));assert(FrozenGI::appendInstance(frozen,j,{-1,-1,-1},{200,100,10},reference,q,tc,f,{}));assert(denied&&r==q&&tb==tc);scene(out,reference);}
 // First-use rejection, repeated source indices, signed zero and finite subnormal positions.
 a.vertices[0].position={-0.f,std::numeric_limits<float>::denorm_min(),0};b.vertices[0].position={-0.f,std::numeric_limits<float>::denorm_min(),0};
 for(unsigned repeat=0;repeat<2;++repeat){WorldScene x;FrozenGI::WorldScene y;NorthlightGI::Instance i;FrozenGI::Instance j;std::fill(i.matrix,i.matrix+9,0);std::fill(j.matrix,j.matrix+9,0);i.matrix[0]=i.matrix[4]=i.matrix[8]=j.matrix[0]=j.matrix[4]=j.matrix[8]=1;std::vector<uint32_t>r(4,UINT32_MAX),s(4,UINT32_MAX);uint64_t tb=0,tc=0;std::string e,f;assert(NorthlightGI::appendInstance(a,i,{-1,-1,-1},{200,100,10},x,r,tb,e,{}));assert(FrozenGI::appendInstance(b,j,{-1,-1,-1},{200,100,10},y,s,tc,f,{}));scene(x,y);++assemblyChecks;}
}
static void u32(FILE*f,uint32_t n){unsigned char b[]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};assert(fwrite(b,1,4,f)==4);}static void f32(FILE*f,float n){uint32_t b;std::memcpy(&b,&n,4);u32(f,b);}static void vec(FILE*f,Vec3 p){f32(f,p.x);f32(f,p.y);f32(f,p.z);}
static void files(const fs::path& root,const WorldScene& s){fs::create_directories(root/"models");FILE*f=fopen((root/"models"/(std::string(64,'0')+".fgs")).string().c_str(),"wb");assert(f);fwrite("FGS2",1,4,f);u32(f,2);u32(f,s.vertices.size());u32(f,s.triangles.size());u32(f,s.materials.size());for(auto&v:s.vertices){vec(f,v.position);vec(f,v.normal);f32(f,v.u);f32(f,v.v);}for(auto&t:s.triangles){u32(f,t.v0);u32(f,t.v1);u32(f,t.v2);u32(f,t.material);}for(auto&m:s.materials){vec(f,m.albedo);u32(f,m.width);u32(f,m.height);f32(f,m.alphaCutoff);u32(f,m.rgba.size());fwrite(m.rgba.data(),1,m.rgba.size(),f);}fclose(f);
 f=fopen((root/"tile.fg3").string().c_str(),"wb");assert(f);fwrite("FGS3",1,4,f);u32(f,3);u32(f,1);uint8_t hash[32]={};fwrite(hash,1,32,f);u32(f,1);u32(f,0);u32(f,0);vec(f,{-1,-1,-1});vec(f,{220,80,10});for(float n:{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f})f32(f,n);vec(f,{});fclose(f);
}
static void provenance(const WorldScene&s,bool full){uint32_t end=0;size_t bytes=sizeof(s)+s.vertices.capacity()*sizeof(WorldVertex)+s.vertices.overhead()+s.triangles.capacity()*sizeof(WorldTriangle)+s.triangles.overhead()+s.materials.capacity()*sizeof(WorldMaterial)+s.completePlacements.capacity()*sizeof(WorldPlacementCoverage)+s.sourcePieces.capacity()*sizeof(SourcePieceRange);for(auto&m:s.materials)bytes+=m.rgba.capacity();for(auto&p:s.completePlacements)bytes+=p.modelKey.capacity();for(auto&p:s.sourcePieces){assert(p.sourceIdentity&&p.triangleBase>=end&&p.triangleCount);if(full)assert(p.triangleBase==end);end=p.triangleBase+p.triangleCount;assert(end<=s.triangles.size()&&uint64_t(p.vertexBase)+p.vertexCount<=s.vertices.size());for(size_t i=p.triangleBase;i<end;++i){auto&t=s.triangles[i];assert(std::find(p.materialMap.begin(),p.materialMap.end(),t.material)!=p.materialMap.end());for(auto v:{t.v0,t.v1,t.v2})assert(v>=p.vertexBase&&v<p.vertexBase+p.vertexCount);}bytes+=p.placement.modelKey.capacity()+p.materialMap.capacity()*4;}assert(bytes==s.retainedBytes());if(full)assert(end==s.triangles.size());}
static void planEqual(const NorthlightWorldMesh::WorldMeshUploadPlan&a,const NorthlightWorldMesh::WorldMeshUploadPlan&b){assert(a.indices==b.indices&&a.batches.size()==b.batches.size()&&a.triangleCount==b.triangleCount&&a.fixedTerrainChunks==b.fixedTerrainChunks);for(size_t i=0;i<a.batches.size();++i){auto&x=a.batches[i];auto&y=b.batches[i];assert(x.material==y.material&&x.start==y.start&&x.count==y.count&&x.terrain==y.terrain&&x.chunkX==y.chunkX&&x.chunkY==y.chunkY);bits(x.boundsLow,y.boundsLow);bits(x.boundsHigh,y.boundsHigh);}}
int main(int argc,char**argv){appendCases();auto root=fs::temp_directory_path()/("scene-assembly-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));auto input=model<WorldScene>();files(root,input);std::vector<std::string> list={(root/"tile.fg3").string()};auto directory=(root/"models").string();std::string e;auto source=std::make_shared<WorldScene>();assert(loadScene((root/"models"/(std::string(64,'0')+".fgs")).string().c_str(),*source,e));source->sourceIdentity=17;SceneSourceProvider provider=[&](const std::string&,std::string&,const AllocationAdmission&)->std::shared_ptr<const WorldScene>{return source;};
 WorldScene expected;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},expected,e,1,{}, {}, {},nullptr,provider));WorldScene chunks;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},chunks,e,1,{}, {}, {},nullptr,provider,true));scene(expected,chunks,false);assert(chunks.sourcePieces.size()>expected.sourcePieces.size());provenance(chunks,true);
 // Exercise the pressure decoder itself: a supplied decoded source would
 // bypass the file reader that formerly left a large CRT arena behind.
 {WorldScene raw,pressure;
  assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},raw,e,1));
  assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},pressure,e,1,{}, {}, {},nullptr,{},true,true,true));decodedEqual(raw,pressure);
  assert(pressure.vertices.spooled()&&pressure.triangles.spooled());for(const auto& material:pressure.materials)if(!material.texels().empty())assert(material.sharedTexels&&material.rgba.empty());
  unsigned refused=0,recovered=0;
  for(unsigned deny=1;deny<=64;++deny){WorldScene output=raw;unsigned calls=0;
   const bool ok=loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},output,e,1,[&](uint64_t){return ++calls!=deny;},{},{},nullptr,{},true,true,true);
   decodedEqual(raw,output);if(ok)++recovered;else ++refused;
  }
  assert(refused&&recovered);std::printf("PRESSURE_FILE_DECODE exact=1 immutableTexels=1 admissionRefusals=%u recoveries=%u oldSceneRetained=1\n",refused,recovered);
 }
 // Cold vector growth leaves over 4 MiB of triangle slack. A denied or failed
 // optional copy must still publish the same complete geometry.
 {auto large=std::make_shared<WorldScene>(input);large->sourceIdentity=0;const auto triangle=large->triangles.front();large->triangles.assign(550000,triangle);SceneSourceProvider supply=[&](const std::string&,std::string&,const AllocationAdmission&)->std::shared_ptr<const WorldScene>{return large;};const uint64_t exactBytes=uint64_t(large->triangles.size())*sizeof(WorldTriangle);WorldScene compacted,denied,failed;
  assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},compacted,e,1,{}, {}, {},nullptr,supply));assert(compacted.triangles.size()==550000);
  unsigned denials=0;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},denied,e,1,[&](uint64_t bytes){if(bytes==exactBytes){++denials;return false;}return true;},{},{},nullptr,supply));assert(denials==1&&denied.triangles.capacity()>compacted.triangles.capacity());scene(compacted,denied);
  unsigned injected=0;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},failed,e,1,[&](uint64_t bytes){if(bytes==exactBytes){++injected;failAllocation=0;}return true;},{},{},nullptr,supply));assert(injected==1&&failAllocation==-1&&failed.triangles.capacity()>failed.triangles.size());scene(compacted,failed);
  printf("COMPACTION triangleCapacity=%zu/%zu saved=%zu denial=1 allocationFailure=1\n",denied.triangles.capacity(),compacted.triangles.capacity(),(denied.triangles.capacity()-compacted.triangles.capacity())*sizeof(WorldTriangle));}
 unsigned exactFallbacks=0,failures=0;for(unsigned nth=0;nth<500;++nth){WorldScene output=expected;failAllocation=nth;bool ok=false;try{ok=loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},output,e,1,{}, {}, {},nullptr,provider,true);}catch(...){failAllocation=-1;assert(false&&"loader exception escaped");}failAllocation=-1;scene(expected,output,false);if(ok){++exactFallbacks;provenance(output,false);}else ++failures;}
 assert(exactFallbacks&&failures);
 // Every denied metadata block either preserves the old scene or succeeds with exact uncached geometry.
 for(unsigned deny=1;deny<=100;++deny){unsigned calls=0;WorldScene output=expected;bool ok=loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},output,e,1,[&](uint64_t){return ++calls!=deny;},{},{},nullptr,provider,true);scene(expected,output,false);if(ok)provenance(output,false);}
 for(unsigned mode=0;mode<3;++mode){if(mode==1)std::stable_sort(source->triangles.begin(),source->triangles.end(),[](auto&a,auto&b){return a.material<b.material;});for(unsigned step=0;step<5;++step){const float edge=float(step)*16;WorldScene a,b;assert(loadInstancedScenes(list,directory,{edge,-1,-1},{edge+115,80,10},a,e,1,{}, {}, {},nullptr,provider));assert(loadInstancedScenes(list,directory,{edge,-1,-1},{edge+115,80,10},b,e,1,{}, {}, {},nullptr,provider,true));scene(a,b,false);provenance(b,mode!=2);if(mode==2)assert(b.sourcePieces.empty());}if(mode==1)source->sourceIdentity=0;}
 source->sourceIdentity=18;WorldScene terrain;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},terrain,e,1,{}, {}, {},nullptr,provider,true));WorldScene local;NorthlightWorldMesh::WorldMeshUploadPlan oracle;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},oracle,e));NorthlightWorldMesh::PreparationCache prep;prep.setLimit(8*1024*1024);NorthlightWorldMesh::WorldMeshUploadPlan cached;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},cached,e,{},NorthlightShadowTerrain::Radius,{},false,nullptr,&prep));planEqual(oracle,cached);assert(prep.stats().bytes>0);
 // A consumed scratch scene may be discarded on any failure, but the old
 // published plan must remain intact. Verify exact indices and vertex bytes.
 {WorldScene paged;assert(loadInstancedScenes(list,directory,{-1,-1,-1},{220,80,10},paged,e,1,{}, {}, {},nullptr,provider,true,true));scene(terrain,paged);assert(paged.vertices.chunked()&&paged.triangles.chunked());
  NorthlightWorldMesh::WorldMeshUploadPlan reference;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},reference,e,{},NorthlightShadowTerrain::Radius,{},true));
  unsigned failed=0,passed=0;
  for(unsigned nth=0;nth<180;++nth){WorldScene scratch=paged;NorthlightWorldMesh::WorldMeshUploadPlan out;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e));const auto old=out.source;failAllocation=nth;bool ok=false;try{ok=NorthlightShadowTerrain::buildConsuming(local,scratch,{-1000,-1000,0},out,e,{},NorthlightShadowTerrain::Radius,{},nullptr);}catch(...){failAllocation=-1;assert(false&&"consuming assembly exception escaped");}failAllocation=-1;
   if(!ok){++failed;assert(out.source==old);planEqual(oracle,out);}else{++passed;assert(out.pagingIndexCount()==reference.pagingIndexCount()&&out.pagingVertexCount()==reference.pagingVertexCount()&&out.fixedTerrainChunks==reference.fixedTerrainChunks);for(size_t i=0;i<out.pagingIndexCount();++i)assert(out.pagingIndex(i)==reference.pagingIndex(i));for(size_t i=0;i<out.pagingVertexCount();++i)bits(out.pagingVertex(i),reference.pagingVertex(i));}
  }assert(failed&&passed);printf("CONSUMING failures=%u successes=%u exact=1 oldPlanRetained=1\n",failed,passed);
 }
 // Cross block boundaries and reference the first vertex block again at the end.
 {WorldScene large=terrain;large.vertices.resize(20000);const auto t=large.triangles.front();large.triangles.assign(40000,t);large.triangles.back()={0,19998,19999,t.material};large.sourcePieces.clear();WorldScene paged=large;paged.vertices.chunks(nullptr);paged.triangles.chunks(nullptr);
  NorthlightWorldMesh::WorldMeshUploadPlan reference;assert(NorthlightShadowTerrain::build(local,large,{-1000,-1000,0},reference,e,{},NorthlightShadowTerrain::Radius,{},true));unsigned failed=0,passed=0;
  for(unsigned deny=1;deny<35;++deny){WorldScene scratch=paged;NorthlightWorldMesh::WorldMeshUploadPlan out;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e));const auto old=out.source;unsigned calls=0;const bool ok=NorthlightShadowTerrain::buildConsuming(local,scratch,{-1000,-1000,0},out,e,[&](uint64_t){return ++calls!=deny;},NorthlightShadowTerrain::Radius,{},nullptr);if(!ok){++failed;assert(out.source==old);}else{++passed;assert(out.pagingIndexCount()==reference.pagingIndexCount()&&out.pagingVertexCount()==reference.pagingVertexCount());for(size_t i=0;i<out.pagingIndexCount();++i)assert(out.pagingIndex(i)==reference.pagingIndex(i));for(size_t i=0;i<out.pagingVertexCount();++i)bits(out.pagingVertex(i),reference.pagingVertex(i));}}
  assert(failed&&passed);printf("CONSUMING_MULTIBLOCK failures=%u successes=%u lateReference=1\n",failed,passed);
 }
 unsigned preparationFaultSuccesses=0,preparationFaultFailures=0;
 for(unsigned nth=0;nth<500;++nth){prep.reset();NorthlightWorldMesh::WorldMeshUploadPlan out;assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e));failAllocation=nth;bool ok=false;try{ok=NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e,{},NorthlightShadowTerrain::Radius,{},false,nullptr,&prep);}catch(...){failAllocation=-1;assert(false&&"preparation failure escaped");}failAllocation=-1;planEqual(oracle,out);if(ok)++preparationFaultSuccesses;else ++preparationFaultFailures;assert(prep.stats().bytes<=8*1024*1024);assert(NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e,{},NorthlightShadowTerrain::Radius,{},false,nullptr,&prep));planEqual(oracle,out);}
 assert(preparationFaultSuccesses&&preparationFaultFailures);
 for(unsigned deny=1;deny<50;++deny){unsigned calls=0;NorthlightWorldMesh::WorldMeshUploadPlan out;bool ok=NorthlightShadowTerrain::build(local,terrain,{-1000,-1000,0},out,e,[&](uint64_t){return ++calls!=deny;},NorthlightShadowTerrain::Radius,{},false,nullptr,&prep);if(ok)planEqual(oracle,out);}
 unsigned real=0;if(argc>1){const std::string base=argv[1];for(unsigned step=0;step<3;++step){Vec3 c{5248+float(step)*32,10,508};int tx=int(std::floor((17066.666666666666-c.y)/533.3333333333333)),ty=int(std::floor((17066.666666666666-c.x)/533.3333333333333));std::vector<std::string> paths;for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){auto p=base+"/Northrend/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(fs::exists(p))paths.push_back(p);}for(unsigned distant=0;distant<2;++distant){float radius=distant?928.f:288.f;WorldScene a;FrozenGI::WorldScene b;Vec3 lo=c-Vec3(radius,radius,radius),hi=c+Vec3(radius,radius,radius);assert(loadInstancedScenes(paths,base+"/models",lo,hi,a,e));assert(FrozenGI::loadInstancedScenes(paths,base+"/models",{lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z},b,e));scene(a,b);if(distant){SceneSourceProvider rp=[&](const std::string& p,std::string& error,const AllocationAdmission& admit)->std::shared_ptr<const WorldScene>{auto decoded=std::make_shared<WorldScene>();if(!loadScene(p.c_str(),*decoded,error,admit))return {};decoded->sourceIdentity=std::hash<std::string>{}(p)|1;return decoded;};WorldScene proven;assert(loadInstancedScenes(paths,base+"/models",lo,hi,proven,e,1,{},{},{},nullptr,rp,true));WorldScene raw;assert(loadInstancedScenes(paths,base+"/models",lo,hi,raw,e,1));scene(raw,proven,false);provenance(proven,true);real+=1;}++real;}}}
 fs::remove_all(root);printf("PASS frozenAssembly=%u realRegions=%u loaderAllocationSites=500 exactSuccesses=%u preservedFailures=%u preparationAllocationSites=500 preparationSuccesses=%u preparationFailures=%u metadataDenials=149 chunkPartialInterleavedUnknown=1 exactMaterialUVAlpha=1 retainedAccounting=1\n",assemblyChecks,real,exactFallbacks,failures,preparationFaultSuccesses,preparationFaultFailures);
}
