// Offline proof for 0.3.137 plan preparation: the fast lookups/LRU cache and the
// exact 0.3.136 path receive the same randomized operation stream and must
// prepare field-identical plans (commands, instances, slices, signature) and
// issue identical draw records, including prebuilt (early prepared) plans.
#define STATIC_SHADOW_GPU_TEST
#include "test_static_shadow_fake_d3d.h"
#include "static_shadow_gpu.h"
#include "world_math.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace StaticShadow;
// Verbatim 0.3.136 bounds tests: the *Affine split must return the same answers.
namespace Old {
inline bool clipReject(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                       const float* matrix,float epsilon=1e-4f,bool retainUpstream=false) {
    if(!matrix||!std::isfinite(epsilon)||epsilon<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    if(matrix[3]!=0||matrix[7]!=0||matrix[11]!=0||matrix[15]!=1)return false;
    double minimum[3],maximum[3];
    for(unsigned axis=0;axis<3;++axis) {
        double lower=matrix[12+axis],upper=lower,magnitude=std::fabs(lower);
        for(unsigned row=0;row<3;++row){
            const double a=double(lo[row])*matrix[row*4+axis],b=double(hi[row])*matrix[row*4+axis];
            lower+=std::min(a,b);upper+=std::max(a,b);
            magnitude+=std::max(std::fabs(a),std::fabs(b));
        }
        // Interval extrema replace eight corner projections. The maximum
        // magnitude bounds EVERY corner's old roundoff expansion (including
        // world-coordinate cancellation), so this can only retain more casters.
        const double margin=double(epsilon)+magnitude*(8.0*std::numeric_limits<float>::epsilon());
        minimum[axis]=lower-margin;maximum[axis]=upper+margin;
    }
    return maximum[0]<-1||minimum[0]>1||maximum[1]<-1||minimum[1]>1||(!retainUpstream&&maximum[2]<0)||minimum[2]>1;
}
inline bool depthFullyInside(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                             const float* matrix,float epsilon=1e-4f){
    if(!matrix||!std::isfinite(epsilon)||epsilon<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    if(matrix[3]!=0||matrix[7]!=0||matrix[11]!=0||matrix[15]!=1)return false;
    double minimum=matrix[14],maximum=minimum,magnitude=std::fabs(minimum);
    for(unsigned axis=0;axis<3;++axis){
        const double coefficient=matrix[axis*4+2],a=double(lo[axis])*coefficient,b=double(hi[axis])*coefficient;
        minimum+=std::min(a,b);maximum+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));
    }
    // 32 eps also covers the preceding local->world float transform and
    // its AABB center/extent computation, including large-coordinate
    // cancellation. Conservative rejection costs speed, never coverage.
    const double margin=double(epsilon)+magnitude*(32.0*std::numeric_limits<float>::epsilon());
    if(!(minimum-margin>0&&maximum+margin<1))return false;
    return true;
}
}
static uint32_t rng=0x5eed137;
static uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float random(float lo,float hi){return lo+(hi-lo)*float(next()>>8)/16777216.f;}
static unsigned pick(unsigned n){return n?next()%n:0;}
static std::shared_ptr<Model> model(const std::string& key,uint64_t revision){
 auto m=std::make_shared<Model>();m->key=key;m->contentRevision=revision;m->index16=true;
 NorthlightGI::WorldMaterial mat;mat.alphaCutoff=pick(3)?.37f:0;mat.width=mat.height=1;mat.rgba={255,255,255,127};m->materials={mat};m->materialKeys={1};
 const unsigned batches=1+pick(6);const float s=random(.5f,25);
 for(unsigned b=0;b<batches;++b){m->vertices.insert(m->vertices.end(),{{0,0,.5f,0,0},{.2f,0,.5f,1,0},{0,.2f,.5f,0,1}});m->indices.insert(m->indices.end(),{3*b,3*b+1,3*b+2});
  Batch batch{3*b,3,0,{random(-s,0),random(-s,0),random(-s,0)},{random(0,s),random(0,s),random(0,3*s)}};
  if(!pick(97))batch.low.x=NAN;else if(!pick(97))std::swap(batch.low.y,batch.high.y);
  m->batches.push_back(batch);}
 return m;
}
static Placement placement(const std::string& key,uint64_t uid,Vec3 center){
 Placement p;p.uid=uid;p.category=1+pick(3);p.modelKey=key;p.translation=center+Vec3(random(-300,300),random(-300,300),random(-60,60));
 if(pick(4)){const float a=random(0,6.283f),k=random(.5f,2);const float m[9]={std::cos(a)*k,-std::sin(a)*k,0,std::sin(a)*k,std::cos(a)*k,0,0,0,k};std::memcpy(p.matrix,m,sizeof m);}
 else for(float& c:p.matrix)c=random(-2,2);
 return p;
}
static std::shared_ptr<Snapshot> finish(std::shared_ptr<Snapshot> s,bool prepared,uint64_t geometry){
 s->placementGroups.clear();s->preparedIdentity=nullptr;s->geometryRevision=0;
 if(prepared){for(size_t i=0;i<s->placements.size();++i)s->placementGroups[s->placements[i].modelKey].push_back(i);s->preparedIdentity=s.get();s->geometryRevision=geometry;}
 return s;
}
struct Pair {IDirect3DDevice9 fd,rd;GpuCache fast,reference;Pair(GpuCache::Limits l,bool strict):fast(l),reference(l){reference.referencePlans();if(strict)fast.roundRobinPlans();else fast.selfCheckPlans();fd.captureTransforms=rd.captureTransforms=true;}};
int main(){
 size_t bounds=0;
 for(unsigned trial=0;trial<2000000;++trial){float m[16];const float e=random(1,89)*.01745329252f,z=random(-3.14f,3.14f);const Vec3 c{random(-17000,17000),random(-17000,17000),random(-3000,3000)};
  NorthlightWorldMath::shadowMatrix(c,{std::cos(e)*std::cos(z),std::cos(e)*std::sin(z),std::sin(e)},random(10,300),m);
  const unsigned bad=pick(64);if(bad==0)m[pick(16)]=NAN;else if(bad==1)m[pick(16)]=INFINITY;else if(bad==2)m[3+4*pick(3)]=1e-7f;else if(bad==3)m[15]=.999f;
  Vec3 lo=c+Vec3(random(-900,900),random(-900,900),random(-900,900)),hi=lo+Vec3(random(-1,300),random(-1,300),random(-1,600));
  if(!pick(97))lo.y=NAN;const float eps=pick(50)?1e-4f:(pick(2)?-1.f:NAN);const bool up=pick(2);const float* mm=pick(500)?m:nullptr;
  assert(Old::clipReject(lo,hi,mm,eps,up)==NorthlightShadowBounds::clipReject(lo,hi,mm,eps,up));
  assert(Old::depthFullyInside(lo,hi,mm,eps)==NorthlightShadowBounds::depthFullyInside(lo,hi,mm,eps));
  if(NorthlightShadowBounds::affineLightMatrix(mm)){assert(Old::clipReject(lo,hi,mm,1e-4f,true)==NorthlightShadowBounds::directionalClipRejectAffine(lo,hi,mm));assert(Old::depthFullyInside(lo,hi,mm)==NorthlightShadowBounds::depthFullyInsideAffine(lo,hi,mm));const auto both=NorthlightShadowBounds::classifyAffine(lo,hi,mm,1e-4f,true);assert(both.rejected==Old::clipReject(lo,hi,mm,1e-4f,true));if(!both.rejected)assert(both.depthInside==Old::depthFullyInside(lo,hi,mm));}
  ++bounds;}
 uint64_t interiorHits=0,selfChecks=0;size_t compared=0,planBytes=0,drawRecords=0,scenes=0;uint64_t fastBuilds=0,referenceBuilds=0;double fastMs=0,referenceMs=0;
 for(unsigned trial=0;trial<120;++trial){
  GpuCache::Limits l;l.uploadMs=1e9;l.uploadBytes=pick(2)?(1u<<30):unsigned(200+pick(4000));l.retainFrames=1+pick(40);if(!pick(6))l.planMetadataBytes=pick(2)?0:size_t(600+pick(4000));
  const bool strict=trial%2==0;Pair pair(l,strict);const Vec3 center{random(-17000,17000),random(-17000,17000),random(-200,400)};
  std::map<std::string,std::shared_ptr<Model>> models;const unsigned modelCount=4+pick(40);
  for(unsigned m=0;m<modelCount;++m){std::string key="world/generic/"+std::string(pick(2)?"doodads/":"passive/")+std::to_string(m*7919%1000)+".m2";models[key]=model(key,1);}
  auto s=std::make_shared<Snapshot>();s->map="t";s->mapGeneration=1;
  const unsigned placements=20+pick(400);uint64_t uid=1,geometry=1;
  for(unsigned i=0;i<placements;++i){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));s->placements.push_back(placement(it->first,uid++,center));s->models[it->first]=it->second;}
  s=finish(s,pick(2),geometry++);
  std::vector<std::array<float,16>> matrices;
  auto addMatrix=[&](){std::array<float,16> m;const float e=random(2,85)*.01745329252f,z=random(-3.14f,3.14f);const Vec3 dir=NorthlightWorldMath::quantizeDirection({std::cos(e)*std::cos(z),std::cos(e)*std::sin(z),std::sin(e)});
   NorthlightWorldMath::shadowMatrixFrom(NorthlightWorldMath::shadowFrame(center+Vec3(random(-80,80),random(-80,80),0),dir,pick(2)?48.f:192.f),(pick(2)?48.f:192.f)*1280.f/1024.f,m.data());
   if(!pick(40))m[pick(16)]=NAN;else if(!pick(40))m[3]=.25f;matrices.push_back(m);};
  for(unsigned i=0;i<4;++i)addMatrix();
  std::vector<Placement> covered;uint64_t frame=1;
  for(unsigned op=0;op<70;++op){
   const unsigned kind=pick(10);
   if(kind<=2){auto n=std::make_shared<Snapshot>(*s);
    for(unsigned edits=1+pick(4);edits--;){const unsigned what=pick(6);
     if(what==0&&!n->placements.empty())n->placements[pick(unsigned(n->placements.size()))].translation.x+=random(-40,40);
     else if(what==1){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));n->placements.push_back(placement(it->first,uid++,center));n->models[it->first]=it->second;}
     else if(what==2&&n->placements.size()>1)n->placements.erase(n->placements.begin()+pick(unsigned(n->placements.size())));
     else if(what==3){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));it->second=model(it->first,it->second->contentRevision+1);if(n->models.count(it->first))n->models[it->first]=it->second;}
     else if(what==4&&!n->placements.empty()){const auto key=n->placements[pick(unsigned(n->placements.size()))].modelKey;n->placements.erase(std::remove_if(n->placements.begin(),n->placements.end(),[&](const Placement& p){return p.modelKey==key;}),n->placements.end());n->models.erase(key);}
     else if(!n->placements.empty())std::swap(n->placements[pick(unsigned(n->placements.size()))],n->placements[pick(unsigned(n->placements.size()))]);
    }
    s=finish(n,pick(3)!=0,geometry++);
   }else if(kind==3){covered.clear();for(const auto& p:s->placements)if(!pick(3)){covered.push_back(p);if(!pick(8))covered.back().translation.z+=1;if(!pick(8))covered.push_back(p);}
    pair.fast.setCoveredPlacements(covered);pair.reference.setCoveredPlacements(covered);
   }else if(kind==4){frame+=1+pick(60);}
   else if(kind==7&&s->preparedIdentity==s.get()){s=finish(std::make_shared<Snapshot>(*s),true,s->geometryRevision);} /* same geometry, new publication */
   else if(kind==5&&!pick(4)){pair.fast.reset();pair.reference.reset();}
   else if(kind==6){addMatrix();if(matrices.size()>14)matrices.erase(matrices.begin()+pick(unsigned(matrices.size())));}
   const bool uploads=pick(4)!=0;++frame;
   pair.fast.update(&pair.fd,s,frame,uploads);
   pair.reference.update(&pair.rd,s,frame,uploads);
   assert(pair.fast.stats().readyModels==pair.reference.stats().readyModels);
   // Early (prebuild-style) preparation of a later matrix, then any order.
   std::vector<size_t> order(matrices.size());for(size_t i=0;i<order.size();++i)order[i]=i;for(size_t i=order.size();i>1;--i)std::swap(order[i-1],order[pick(unsigned(i))]);
   for(size_t i:order){const float* m=matrices[i].data();
    auto a=std::chrono::steady_clock::now();auto builds=pair.fast.stats().planBuilds;const auto x=pair.fast.planDigest(m,strict);fastMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();fastBuilds+=pair.fast.stats().planBuilds-builds;
    a=std::chrono::steady_clock::now();builds=pair.reference.stats().planBuilds;const auto y=pair.reference.planDigest(m,strict);referenceMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();referenceBuilds+=pair.reference.stats().planBuilds-builds;
    if(x!=y){size_t k=0;while(k<x.size()&&k<y.size()&&x[k]==y[k])++k;std::cerr<<"DBG trial="<<trial<<" strict="<<strict<<" op="<<op<<" byte="<<k<<" sizes="<<x.size()<<"/"<<y.size()<<"\n";}
    assert(x==y);assert(pair.fast.planReady(m));assert(pair.fast.signature(m)==pair.reference.signature(m));++compared;planBytes+=x.size();}
   if(!pick(3)){const float* m=matrices[pick(unsigned(matrices.size()))].data();pair.fd.drawnRecords.clear();pair.rd.drawnRecords.clear();
    assert(pair.fast.draw(&pair.fd,m)==pair.reference.draw(&pair.rd,m));assert(pair.fd.drawnRecords.size()==pair.rd.drawnRecords.size());
    for(size_t i=0;i<pair.fd.drawnRecords.size();++i){const auto& a=pair.fd.drawnRecords[i];const auto& b=pair.rd.drawnRecords[i];assert(a.transform==b.transform&&a.firstIndex==b.firstIndex&&a.pixel==b.pixel&&a.primitives==b.primitives);}
    drawRecords+=pair.fd.drawnRecords.size();
    GpuCache::ContentRecord ra,rb;pair.fast.record(m,ra);pair.reference.record(m,rb);assert(ra.valid==rb.valid&&ra.slices.size()==rb.slices.size());
    for(size_t i=0;i<ra.slices.size();++i)assert(ra.slices[i].key==rb.slices[i].key&&ra.slices[i].signature==rb.slices[i].signature&&ra.slices[i].revision==rb.slices[i].revision);}
  }
  assert(pair.fast.stats().planSelfCheckMismatches==0);selfChecks+=pair.fast.stats().planSelfChecks;
  interiorHits+=pair.fast.stats().interiorBatchHits;
  ++scenes;pair.fast.reset();pair.reference.reset();
 }
 assert(FakeResource::alive==0);
 assert(interiorHits>0);
 std::cout<<"interior batch classifications reused: "<<interiorHits<<"\n";
 std::cout<<"plan equality: "<<bounds<<" bounds tests equal to 0.3.136, "<<scenes<<" randomized scenes, "<<compared<<" field-identical plans ("<<planBytes/1024<<" KiB), "<<drawRecords<<" identical draw records, "<<selfChecks<<" in-cache self-checks without mismatch; builds fast="<<fastBuilds<<" reference="<<referenceBuilds<<" ms fast="<<fastMs<<" reference="<<referenceMs<<"\n";
}
