// Reconstructed tracked snapshots retain only immutable envelopes. This uses
// the production job schedule, current pose/camera and production cube masks.
#include <chrono>
struct EnvelopeTestClock {using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<EnvelopeTestClock>;static constexpr bool is_steady=true;static time_point now(){static long long t=0;return time_point(duration(++t));}};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK EnvelopeTestClock
#include "replay_bounds_job.h"
#include "point_light_shadow.h"
#include "water_shader_patch.h"
#include "actor_client_programs.h"
#include <cassert>
#include <cstdio>
#include <fstream>
namespace B=NorthlightReplayBounds;namespace J=NorthlightReplayBoundsJob;
using Mesh=NorthlightDrawSnapshot::Mesh;using Owner=std::shared_ptr<const Mesh>;using Program=NorthlightActorDeformation::Program;
static const D3DVERTEXELEMENT9 declaration[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,28,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
static Program rigid(){using namespace NorthlightWaterShaderPatch;Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0}};p.definitions.push_back({0,{1,0,0,0}});
 for(unsigned axis=0;axis<3;++axis)p.operations.push_back({9,dst(0,0,1u<<axis),{{src(1,0),0},{src(2,31+axis),0},{}}});
 p.operations.push_back({1,dst(0,0,8),{{src(2,0,0),0},{},{}}});NorthlightActorDeformation::buildKernel(p);return p;}
static Program skin(){Program p;assert(NorthlightActorDeformation::compile(ClientShaders::FourBoneVs3,sizeof(ClientShaders::FourBoneVs3)/4,p));return p;}
static NorthlightSourceIdentity::Handle sourceKey(unsigned id,unsigned revision=1,unsigned tag=0){std::vector<std::uint64_t> words(64);words[0]=id+1;words[1]=revision;for(unsigned k=2;k<64;++k)words[k]=k*1099511628211ull;words[63]^=tag;return NorthlightSourceIdentity::make(std::move(words));}
static Owner source(unsigned id,unsigned revision=1,bool tracked=true){auto m=std::make_shared<Mesh>();m->vertexCount=66;m->primitiveCount=22;m->streams[0].stride=32;m->streams[0].bytes.resize(66*32);if(tracked)m->sourceIdentity=sourceKey(id,revision);
 for(unsigned v=0;v<66;++v){float data[]={30.f+float(id%40)*.1f+float(v%3)*.2f,float(int(id%13)-6)*.15f+float(v%2)*.15f,4.f+float(v%5)*.1f,1,0,0,0};auto* at=m->streams[0].bytes.data()+v*32;std::memcpy(at,data,28);at[28]=v%4;at[29]=at[30]=at[31]=0;}return m;}
static void pose(float* c,float* inverse,unsigned frame){std::fill_n(c,1024,0.f);std::fill_n(inverse,16,0.f);for(unsigned k=0;k<4;++k)inverse[k*5]=1;const float a=float(frame)*.073f;inverse[0]=inverse[5]=std::cos(a);inverse[1]=std::sin(a);inverse[4]=-inverse[1];inverse[12]=100+std::sin(a)*2;inverse[13]=-50;
 for(unsigned bone=0;bone<75;++bone){auto* r=c+4*(31+bone*3);r[0]=r[5]=r[10]=1;r[3]=std::sin(a)*.7f+bone*.01f;r[7]=std::cos(a)*.4f;r[11]=float(frame)*.025f;}}
static void enclosed(const Program& p,const Owner& mesh,const float* c,const float* inverse,const B::Bounds& b){assert(b.valid);std::vector<NorthlightActorDeformation::Position> positions;assert(NorthlightActorDeformation::worldPositions(p,*mesh,declaration,4,c,inverse,positions));for(const auto& v:positions){const float xyz[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(xyz[k]>=b.low[k]&&xyz[k]<=b.high[k]);}}
static B::Status ready(B::EnvelopeCache& cache,const B::EnvelopeCache::Prepared& p,const Owner& owner,const float* c,const float* inverse,B::Bounds& b){cache.beginFrame(false);B::Budget budget;return cache.calculatePrepared(p,*owner,owner,c,inverse,budget,b,B::EnvelopeCache::Stage::Ready);}
#ifndef NORTHLIGHT_SOURCE_IDENTITY_FIXTURE_ONLY
int main(int argc,char** argv){assert(argc==2);const Program programs[]={rigid(),skin()};assert(B::SkinEnvelope::supports(programs[1]));std::shared_ptr<const B::Prepared> prepared[2];for(unsigned k=0;k<2;++k){prepared[k]=B::EnvelopeCache::prepareProgram(programs[k],declaration,4);assert(prepared[k]);}
 float c[1024],inverse[16];pose(c,inverse,0);B::Cache cache;constexpr unsigned Sources=1900;std::vector<Owner> initial;initial.reserve(Sources);
 // Initial warmup retains all owners, exactly as the old cache expects. The
 // rotation then drops every Mesh and recreates each source's exact identity.
 for(unsigned id=0;id<Sources;++id){auto owner=source(id);initial.push_back(owner);B::Bounds bound;B::Budget budget;cache.beginFrame(false,Sources);assert(cache.calculatePreparedPhase(*prepared[id%50==0],*owner,owner,c,inverse,budget,bound,B::EnvelopeCache::Stage::All)==B::Status::Valid);enclosed(programs[id%50==0],owner,c,inverse,bound);}
 double warmMs=0,rotationMs=0;std::vector<J::Item> stable(1494);for(unsigned i=0;i<stable.size();++i){stable[i].owner=initial[i];stable[i].prepared=prepared[i%50==0];stable[i].constants=c;}
 for(unsigned frame=0;frame<64;++frame){pose(c,inverse,frame+100);for(auto& item:stable){item.bounds={};item.work={};}cache.beginFrame(false,stable.size());const auto start=std::chrono::steady_clock::now();const auto result=J::schedule(cache,stable,inverse);if(frame>=16)warmMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();assert(result.valid==stable.size()&&!result.boundVertices);}
 stable.clear();std::weak_ptr<const Mesh> gone=initial[0];initial.clear();assert(gone.expired());
 float matrices[6][16];const float lamp[]={100,-50,0};for(unsigned face=0;face<6;++face)assert(NorthlightPointShadow::faceMatrix(lamp,.1f,80,face,matrices[face],64));
 std::size_t valid=0,total=0,fanout=0,built=0,identityHits=0,reconstructed=0,enclosures=0,peakBytes=0;std::ofstream rows(argv[1]);
 for(unsigned frame=0;frame<48;++frame){pose(c,inverse,frame+1);const unsigned count=frame%3?1494:820;std::vector<J::Item> items(count);for(unsigned i=0;i<count;++i){const unsigned id=(frame*733+i)%Sources;items[i].owner=source(id,1+(frame%2)); // Current revision may change, but byte-exact replay proof keeps the canonical source.
   auto copy=std::make_shared<Mesh>(*items[i].owner);copy->replayIdentity=sourceKey(id);items[i].owner=copy;items[i].constants=c;items[i].prepared=prepared[id%50==0];}
  cache.beginFrame(false,count);const auto start=std::chrono::steady_clock::now();const auto result=J::schedule(cache,items,inverse);rotationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();valid+=result.valid;total+=count;built+=result.boundVertices;peakBytes=std::max(peakBytes,cache.envelopeBytes());
#if NORTHLIGHT_EXPECT_SOURCE_REUSE
  identityHits+=cache.envelopeStats().sourceIdentityHits;reconstructed+=cache.envelopeStats().sourceReconstructedHits;assert(result.valid==count&&result.boundVertices==0);
#else
  assert(result.valid==0);
#endif
  for(unsigned i=0;i<count;++i){const auto& item=items[i];const unsigned mask=NorthlightPointShadow::faceMask(item.bounds.low,item.bounds.high,item.bounds.valid,matrices);fanout+=__builtin_popcount(mask);if(item.bounds.valid&&i%17==0){const unsigned id=(frame*733+i)%Sources;enclosed(programs[id%50==0],item.owner,c,inverse,item.bounds);++enclosures;}}
  rows<<frame<<'\t'<<count<<'\t'<<result.valid<<'\t'<<result.boundVertices<<'\t'<<cache.envelopeEntries()<<'\t'<<cache.envelopeBytes()<<'\n';
 }
#if NORTHLIGHT_EXPECT_SOURCE_REUSE
 assert(reconstructed==total&&enclosures>3000&&fanout<total*3&&cache.envelopeEntries()==Sources);
 // Exact key negatives: no identity, changed revision/content/layout/program,
 // hash collision and current-pose/camera/budget failures never publish stale bounds.
 B::EnvelopeCache e;auto original=source(7);B::Bounds bounds;B::Budget budget;e.beginFrame(false);assert(e.calculatePrepared(*prepared[0],*original,original,c,inverse,budget,bounds)==B::Status::Valid);
 auto equal=source(7);assert(ready(e,*prepared[0],equal,c,inverse,bounds)==B::Status::Valid);enclosed(programs[0],equal,c,inverse,bounds);
 assert(ready(e,*prepared[1],equal,c,inverse,bounds)==B::Status::Budget&&!bounds.valid);
 for(const auto& bad:{source(7,2),source(7,1,false),source(8)})assert(ready(e,*prepared[0],bad,c,inverse,bounds)==B::Status::Budget&&!bounds.valid);
 auto collision=std::make_shared<Mesh>(*original);const std::uint64_t prime=1099511628211ull,seed=1469598103934665603ull;auto words=original->sourceIdentity->words;words[0]=9;words[1]=((seed^8)*prime)^((seed^9)*prime)^1;collision->sourceIdentity=NorthlightSourceIdentity::make(std::move(words));assert(collision->sourceIdentity->hash==original->sourceIdentity->hash&&!NorthlightSourceIdentity::same(collision->sourceIdentity,original->sourceIdentity));assert(ready(e,*prepared[0],collision,c,inverse,bounds)==B::Status::Budget&&!bounds.valid);
 auto changed=programs[0];changed.definitions.push_back({20,{1,2,3,4}});auto cp=B::EnvelopeCache::prepareProgram(changed,declaration,4);assert(cp&&ready(e,*cp,equal,c,inverse,bounds)==B::Status::Budget&&!bounds.valid);
 D3DVERTEXELEMENT9 different[4];std::memcpy(different,declaration,sizeof declaration);different[0].Offset=4;cp=B::EnvelopeCache::prepareProgram(programs[0],different,4);assert(cp&&ready(e,*cp,equal,c,inverse,bounds)==B::Status::Budget&&!bounds.valid);
 pose(c,inverse,77);assert(ready(e,*prepared[0],equal,c,inverse,bounds)==B::Status::Valid);enclosed(programs[0],equal,c,inverse,bounds);budget={};budget.maxOperations=0;e.beginFrame(false);assert(e.calculatePrepared(*prepared[0],*equal,equal,c,inverse,budget,bounds)==B::Status::Budget&&!bounds.valid);
 inverse[15]=2;assert(ready(e,*prepared[0],equal,c,inverse,bounds)==B::Status::Invalid&&!bounds.valid);
 // Interrupted source construction can resume on the exact reconstructed
 // source; no captured pose is retained in the partial immutable envelope.
 pose(c,inverse,0);B::EnvelopeCache partial;auto first=source(99);budget={};budget.maxVertices=1;partial.beginFrame(false);assert(partial.calculatePrepared(*prepared[0],*first,first,c,inverse,budget,bounds)==B::Status::Budget&&!bounds.valid&&budget.vertices==1);first.reset();auto second=source(99);budget={};partial.beginFrame(false);assert(partial.calculatePrepared(*prepared[0],*second,second,c,inverse,budget,bounds)==B::Status::Valid&&budget.vertices==65);enclosed(programs[0],second,c,inverse,bounds);
#endif
 assert(peakBytes<=8u*1024u*1024u);std::printf("REPLAY_SOURCE_IDENTITY_PASS reuse=%u sources=%u frames=48 packets=%zu valid=%zu cubeFaceDraws=%zu buildVertices=%zu identityHits=%zu reconstructed=%zu enclosures=%zu peakBytes=%zu entryCap=2048 byteCap=8388608 poseCamera=current geometryRetained=0 warmCpuMs=%.6f rotationCpuMs=%.6f\n",unsigned(NORTHLIGHT_EXPECT_SOURCE_REUSE),Sources,total,valid,fanout,built,identityHits,reconstructed,enclosures,peakBytes,warmMs/48,rotationMs/48);
}
#endif
