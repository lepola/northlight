// Offline proof for the 0.3.152 static plan worker (static_plan_job.h,
// GpuCache::kickPlans): a synchronous cache and a worker cache receive the same
// randomized operation stream (publications with moving windows, owner changes,
// readiness/eviction, matrix/travel changes, 2-4 slots per frame). The worker
// cache kicks the later slots' plans each frame with early and late joins,
// steal-back (worker delays), resets mid-job and injected bad_alloc. Every
// frame and slot: plan bytes, signature, record, changedBounds, drawCalls and
// fake-D3D draw records are bit-identical; the worker's after() results equal
// the synchronous calls; planStateDigest differs only in LRU `used` stamps
// (trials where every kicked plan is consumed or dropped by reset).
#define STATIC_SHADOW_GPU_TEST
#include "test_static_shadow_fake_d3d.h"
#include "static_shadow_gpu.h"
#include "world_math.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace StaticShadow;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL %s:%d: %s (trial %u frame %u)\n",__FILE__,__LINE__,#c,trialNo,frameNo);std::abort();}}while(0)
static unsigned trialNo=0,frameNo=0;
static uint32_t rng=0x152f00d;
static uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float random(float lo,float hi){return lo+(hi-lo)*float(next()>>8)/16777216.f;}
static unsigned pick(unsigned n){return n?next()%n:0;}
static std::shared_ptr<Model> model(const std::string& key,uint64_t revision){
 auto m=std::make_shared<Model>();m->key=key;m->contentRevision=revision;m->index16=true;
 NorthlightGI::WorldMaterial mat;mat.alphaCutoff=pick(3)?.37f:0;mat.width=mat.height=1;mat.rgba={255,255,255,127};m->materials={mat};m->materialKeys={1};
 const unsigned batches=1+pick(5);const float s=random(.5f,25);
 for(unsigned b=0;b<batches;++b){m->vertices.insert(m->vertices.end(),{{0,0,.5f,0,0},{.2f,0,.5f,1,0},{0,.2f,.5f,0,1}});m->indices.insert(m->indices.end(),{3*b,3*b+1,3*b+2});
  Batch batch{3*b,3,0,{random(-s,0),random(-s,0),random(-s,0)},{random(0,s),random(0,s),random(0,3*s)}};
  if(!pick(97))batch.low.x=NAN;
  m->batches.push_back(batch);}
 return m;
}
static Placement placement(const std::string& key,uint64_t uid,Vec3 center){
 Placement p;p.uid=uid;p.category=1+pick(3);p.modelKey=key;p.translation=center+Vec3(random(-300,300),random(-300,300),random(-60,60));
 const float a=random(0,6.283f),k=random(.5f,2);const float m[9]={std::cos(a)*k,-std::sin(a)*k,0,std::sin(a)*k,std::cos(a)*k,0,0,0,k};std::memcpy(p.matrix,m,sizeof m);
 return p;
}
static std::shared_ptr<Snapshot> finish(std::shared_ptr<Snapshot> s,bool prepared,uint64_t geometry){
 s->placementGroups.clear();s->preparedIdentity=nullptr;s->geometryRevision=0;
 if(prepared){for(size_t i=0;i<s->placements.size();++i)s->placementGroups[s->placements[i].modelKey].push_back(i);s->preparedIdentity=s.get();s->geometryRevision=geometry;}
 return s;
}
using Rects=std::vector<NorthlightShadowBounds::TexelRect>;
struct Phase {bool ran=false,changed=false;std::vector<CasterBounds> boxes;size_t models=0;std::vector<size_t> calls;uint64_t signature=0;};
static bool sameBoxes(const std::vector<CasterBounds>& a,const std::vector<CasterBounds>& b){
 if(a.size()!=b.size())return false;
 for(size_t i=0;i<a.size();++i)if(std::memcmp(&a[i].low,&b[i].low,sizeof(Vec3))||std::memcmp(&a[i].high,&b[i].high,sizeof(Vec3))||a[i].bounded!=b[i].bounded)return false;
 return true;
}
static bool sameRecords(const GpuCache::ContentRecord& a,const GpuCache::ContentRecord& b){
 if(a.valid!=b.valid||a.instancing!=b.instancing||a.epoch!=b.epoch||std::memcmp(a.matrix,b.matrix,sizeof a.matrix)||a.slices.size()!=b.slices.size())return false;
 for(size_t i=0;i<a.slices.size();++i){const auto& x=a.slices[i];const auto& y=b.slices[i];
  if(x.key!=y.key||x.signature!=y.signature||x.revision!=y.revision||std::memcmp(&x.bounds.low,&y.bounds.low,sizeof(Vec3))||std::memcmp(&x.bounds.high,&y.bounds.high,sizeof(Vec3))||x.bounds.bounded!=y.bounds.bounded)return false;}
 return true;
}
static Rects rects(){Rects out;for(unsigned n=pick(4);n--;){const long x=long(pick(1100)),y=long(pick(1100));out.push_back({x,y,x+1+long(pick(300)),y+1+long(pick(300))});}return out;}
// The later plan must finish while the first build is paused. A serial kick
// fails this handshake; no timing-based speed threshold is needed. Fill all
// slots first so reserving a new near matrix also exercises LRU eviction.
static void overlapAndFailure(){
 for(bool failFirst:{false,true}){
  GpuCache::Limits limits;limits.uploadMs=1e9;limits.uploadBytes=1u<<30;
  IDirect3DDevice9 sd,ad;GpuCache sync(limits),async(limits);sync.testAsync(false);async.testAsync(true);
  auto scene=std::make_shared<Snapshot>();scene->map="overlap";scene->mapGeneration=1;
  scene->models["model"]=model("model",1);
  for(unsigned i=0;i<20;++i)scene->placements.push_back(placement("model",i,{0,0,0}));
  CHECK(sync.update(&sd,scene,1)&&async.update(&ad,scene,1));
  std::array<std::array<float,16>,10> matrices{};
  for(unsigned i=0;i<matrices.size();++i){
   auto& m=matrices[i];m[0]=m[5]=m[10]=.001f;m[15]=1;m[12]=float(i)*.01f;
   if(i<8)CHECK(sync.signature(m.data())==async.signature(m.data()));
  }
  std::atomic<bool> farFinished{false};bool firstEntered=false;
  async.testBeforeSyncBuild([&]{
   firstEntered=true;
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
   while(!farFinished.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
   CHECK(farFinished.load());
   if(failFirst)throw std::bad_alloc();
  });
  const float* later[]={matrices[9].data()};bool threw=false;
  try{CHECK(async.kickPlans(matrices[8].data(),later,1,[&](unsigned,const GpuCache::DetachedView&){farFinished=true;})==1);}
  catch(const std::bad_alloc&){threw=true;}
  CHECK(firstEntered&&threw==failFirst);
  if(failFirst)CHECK(async.asyncPending()==0);
  async.testBeforeSyncBuild({});
  for(unsigned i=8;i<10;++i){
   CHECK(sync.planDigest(matrices[i].data(),true)==async.planDigest(matrices[i].data(),true));
   CHECK(sync.signature(matrices[i].data())==async.signature(matrices[i].data()));
  }
  async.settle();CHECK(async.asyncPending()==0);
  if(!failFirst)CHECK(sync.planStateDigest(false)==async.planStateDigest(false));
  // A completed far plan can remain cached after the near build fails, just
  // like any unconsumed kick. Cache order may differ; every subsequent plan
  // must still match, including evictions through all eight slots.
  for(const auto& matrix:matrices)CHECK(sync.planDigest(matrix.data(),true)==async.planDigest(matrix.data(),true));
 }
 std::cout<<"overlapping plan builds: far completes during near build; first-build failure joins and restores cache\n";
}
int main(int argc,char** argv){
 const unsigned trials=argc>1?unsigned(std::atoi(argv[1])):48,frames=argc>2?unsigned(std::atoi(argv[2])):40;
 uint64_t kicks=0,retracted=0,asyncBuilds=0,stolen=0,failures=0,phases=0,phaseCompared=0,compared=0,drawRecords=0,digests=0,midResets=0,unconsumed=0;
 for(trialNo=0;trialNo<trials;++trialNo){
  GpuCache::Limits l;l.uploadMs=1e9;l.uploadBytes=pick(2)?(1u<<30):unsigned(200+pick(4000));l.retainFrames=1+pick(40);if(!pick(6))l.planMetadataBytes=pick(2)?0:size_t(600+pick(4000));
  // exact: every kicked plan is consumed in loop order or dropped by reset(), so the
  // slot tables must match except `used`. Otherwise some kicked slots are never used
  // (or a mutator runs mid-job) and the worker cache may keep one extra valid plan.
  const bool exact=trialNo%3!=2;
  IDirect3DDevice9 sd,ad;sd.captureTransforms=ad.captureTransforms=true;
  GpuCache sync(l),async(l);sync.testAsync(false);async.testAsync(true);
  const bool roundRobin=trialNo%4==3;if(roundRobin){sync.roundRobinPlans();async.roundRobinPlans();} /* test-only eviction: never kicks */
  const Vec3 center{random(-17000,17000),random(-17000,17000),random(-200,400)};
  std::map<std::string,std::shared_ptr<Model>> models;const unsigned modelCount=4+pick(60);
  for(unsigned m=0;m<modelCount;++m){std::string key="world/generic/"+std::string(pick(2)?"doodads/":"passive/")+std::to_string(m*7919%1000)+".m2";models[key]=model(key,1);}
  auto s=std::make_shared<Snapshot>();s->map="t";s->mapGeneration=1;
  const unsigned placements=40+pick(600);uint64_t uid=1,geometry=1;
  for(unsigned i=0;i<placements;++i){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));s->placements.push_back(placement(it->first,uid++,center));s->models[it->first]=it->second;}
  s=finish(s,pick(4)!=0,geometry++);
  std::vector<std::array<float,16>> pool;Vec3 pivot=center;
  auto addMatrix=[&](){std::array<float,16> m;const float e=random(2,85)*.01745329252f,z=random(-3.14f,3.14f);const Vec3 dir=NorthlightWorldMath::quantizeDirection({std::cos(e)*std::cos(z),std::cos(e)*std::sin(z),std::sin(e)});
   const float radius=pick(2)?48.f:192.f;NorthlightWorldMath::shadowMatrixFrom(NorthlightWorldMath::shadowFrame(pivot+Vec3(random(-60,60),random(-60,60),0),dir,radius),radius*1280.f/1024.f,m.data());
   if(!pick(60))m[pick(16)]=NAN;pool.push_back(m);};
  for(unsigned i=0;i<5;++i)addMatrix();
  std::vector<Placement> covered;uint64_t frame=1;
  GpuCache::ContentRecord recS[4],recA[4];
  bool diverged=false; /* an unconsumed kick kept a plan the synchronous cache never built: strict bookkeeping may differ from here on */
  for(frameNo=0;frameNo<frames;++frameNo){
   // Frame boundary: the same mutations on both caches (the worker cache is settled).
   for(unsigned edits=pick(3);edits--;){const unsigned kind=pick(10);
    if(kind<=2){auto n=std::make_shared<Snapshot>(*s);
     for(unsigned k=1+pick(4);k--;){const unsigned what=pick(6);
      if(what==0&&!n->placements.empty())n->placements[pick(unsigned(n->placements.size()))].translation.x+=random(-40,40);
      else if(what==1){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));n->placements.push_back(placement(it->first,uid++,pivot));n->models[it->first]=it->second;}
      else if(what==2&&n->placements.size()>1)n->placements.erase(n->placements.begin()+pick(unsigned(n->placements.size())));
      else if(what==3){auto it=models.begin();std::advance(it,pick(unsigned(models.size())));it->second=model(it->first,it->second->contentRevision+1);if(n->models.count(it->first))n->models[it->first]=it->second;}
      else if(what==4&&n->placements.size()>8){ /* moving window: drop the front, add at the end of every run */
       for(unsigned d=pick(4);d--;)n->placements.erase(n->placements.begin());auto it=models.begin();std::advance(it,pick(unsigned(models.size())));
       for(unsigned a=1+pick(4);a--;){n->placements.push_back(placement(it->first,uid++,pivot));n->models[it->first]=it->second;}}
      else if(!n->placements.empty())std::swap(n->placements[pick(unsigned(n->placements.size()))],n->placements[pick(unsigned(n->placements.size()))]);
     }
     s=finish(n,pick(4)!=0,geometry++);
    }else if(kind==3){covered.clear();for(const auto& p:s->placements)if(!pick(3)){covered.push_back(p);if(!pick(8))covered.back().translation.z+=1;}
     sync.setCoveredPlacements(covered);async.setCoveredPlacements(covered);
    }else if(kind==4)frame+=1+pick(60);
    else if(kind==5&&!pick(5)){sync.reset();async.reset();}
    else if(kind==6){pivot=pivot+Vec3(random(-30,30),random(-30,30),0);addMatrix();if(pool.size()>10)pool.erase(pool.begin()+pick(unsigned(pool.size())));} /* travel */
    else if(kind==7&&s->preparedIdentity==s.get())s=finish(std::make_shared<Snapshot>(*s),true,s->geometryRevision);
    else if(kind==8){const float* m=pool[pick(unsigned(pool.size()))].data();sync.discardPlan(m);async.discardPlan(m);}
   }
   const bool uploads=pick(4)!=0;++frame;
   CHECK(sync.update(&sd,s,frame,uploads)==async.update(&ad,s,frame,uploads));
   CHECK(sync.stats().readyModels==async.stats().readyModels);
   // This frame's slots (loop order), their frozen phase-2 inputs, and the worker's knobs.
   const unsigned slots=2+pick(3);std::array<float,16> slot[4];
   for(unsigned k=0;k<slots;++k){slot[k]=pool[pick(unsigned(pool.size()))];if(k&&!pick(12))slot[k]=slot[pick(k)];}
   Rects candidateRects[4][2];std::vector<const Rects*> candidates[4];Phase phase[4];
   for(unsigned k=0;k<slots;++k){candidateRects[k][0]=rects();candidateRects[k][1]=rects();candidates[k]={nullptr,&candidateRects[k][0],&candidateRects[k][1]};}
   const unsigned delay=pick(3)?0:unsigned(50+pick(1500)),failMask=pick(6)?0:unsigned(1+pick(15)),dequeueDelay=pick(5)?0:unsigned(200+pick(2000));
   async.testAsync(true,delay,failMask,pick(30),dequeueDelay); /* dequeueDelay: a late-scheduled worker (all stolen: the job is retracted) */
   const float* later[3];for(unsigned k=1;k<slots;++k)later[k-1]=slot[k].data();
   // Worker side of phase 2: the view of item i answers for its matrix (first slot with it).
   const unsigned kicked=async.kickPlans(slot[0].data(),later,slots-1,[&](unsigned,const GpuCache::DetachedView& view){
    unsigned k=0;while(k<slots&&std::memcmp(slot[k].data(),view.matrix(),64))++k;if(k==slots)return;
    auto& ph=phase[k];ph.changed=view.changedBounds(view.matrix(),recA[k],ph.boxes,ph.models);view.drawCalls(view.matrix(),candidates[k],1280,2,ph.calls);ph.signature=view.signature();ph.ran=true;});
   kicks+=kicked!=0;CHECK(!roundRobin||!kicked);
   // The synchronous cache's first slot is prepared at the same point (kickPlans prepares it first).
   if(kicked)CHECK(sync.signature(slot[0].data())==async.signature(slot[0].data()));
   const unsigned resetAt=pick(8)?~0u:pick(slots),skipAt=exact||pick(3)?~0u:1+pick(slots-1);
   for(unsigned k=0;k<slots;++k){const float* m=slot[k].data();
    if(k==resetAt){sync.reset();async.reset();++midResets;for(auto& r:recS)r.valid=false;for(auto& r:recA)r.valid=false;} /* device-failure path: settles (drops) the job mid-flight */
    if(k==skipAt){++unconsumed;diverged=true;if(pick(2)){covered.clear();sync.setCoveredPlacements(covered);async.setCoveredPlacements(covered);}continue;} /* a mutator mid-job */
    if(!pick(3))std::this_thread::sleep_for(std::chrono::microseconds(pick(800))); /* late join */
    // First access in a random form (each joins/installs or steals the kicked plan).
    const unsigned first=pick(5);
    if(first==0)CHECK(sync.signature(m)==async.signature(m));
    else if(first==1)CHECK(sync.planDigest(m,false)==async.planDigest(m,false)); /* strict=false: everything that reaches a draw */
    else if(first==2){const bool a=sync.planReady(m),b=async.planReady(m);CHECK(!a||b);} /* an installed worker build is ready */
    else if(first==3){std::vector<size_t> a,b;sync.drawCalls(m,candidates[k],1280,2,a);async.drawCalls(m,candidates[k],1280,2,b);CHECK(a==b);}
    else{std::vector<CasterBounds> a,b;size_t x=0,y=0;CHECK(sync.changedBounds(m,recS[k],a,x)==async.changedBounds(m,recA[k],b,y));CHECK(sameBoxes(a,b)&&x==y);}
    // Worker phase 2 equals the synchronous calls on the same frozen inputs
    // (read after an explicit join: changedBounds may return before preparing).
    async.joinPlan(m);
    if(phase[k].ran&&async.kickedModeCurrent()&&k!=resetAt&&(resetAt==~0u||k<resetAt)){
     std::vector<CasterBounds> boxes;size_t models=0;std::vector<size_t> calls;
     CHECK(sync.changedBounds(m,recS[k],boxes,models)==phase[k].changed);CHECK(sameBoxes(boxes,phase[k].boxes)&&models==phase[k].models);
     sync.drawCalls(m,candidates[k],1280,2,calls);CHECK(calls==phase[k].calls);CHECK(sync.signature(m)==phase[k].signature);++phaseCompared;}
    phases+=phase[k].ran;
    // Everything else the renderer reads for the slot.
    CHECK(sync.signature(m)==async.signature(m));CHECK(sync.planDigest(m,false)==async.planDigest(m,false));CHECK(diverged||sync.planDigest(m,true)==async.planDigest(m,true));
    {std::vector<size_t> a,b;sync.drawCalls(m,candidates[k],1280,2,a);async.drawCalls(m,candidates[k],1280,2,b);CHECK(a==b);}
    if(pick(2)){sd.drawnRecords.clear();ad.drawnRecords.clear();const Rects* r=pick(3)?nullptr:&candidateRects[k][0];
     CHECK(sync.draw(&sd,m,r,1280,2)==async.draw(&ad,m,r,1280,2));CHECK(sd.drawnRecords.size()==ad.drawnRecords.size());
     for(size_t i=0;i<sd.drawnRecords.size();++i){const auto& a=sd.drawnRecords[i];const auto& b=ad.drawnRecords[i];CHECK(a.transform==b.transform&&a.firstIndex==b.firstIndex&&a.pixel==b.pixel&&a.primitives==b.primitives&&a.scissorTest==b.scissorTest);}
     CHECK(sync.stats().drawCalls==async.stats().drawCalls&&sync.stats().instances==async.stats().instances&&sync.stats().rectSkipped==async.stats().rectSkipped);drawRecords+=sd.drawnRecords.size();}
    if(pick(2)){sync.record(m,recS[k]);async.record(m,recA[k]);CHECK(sameRecords(recS[k],recA[k]));}
    ++compared;
   }
   async.settle(); /* the renderer's RAII join at the end of render() */
   CHECK(async.asyncPending()==0);
   if(exact&&!diverged){CHECK(sync.planStateDigest(false)==async.planStateDigest(false));++digests;}
  }
  const auto& st=async.stats();asyncBuilds+=st.asyncBuilds;retracted+=async.testRetractions();stolen+=st.asyncStolen;failures+=st.asyncFailures;
  CHECK(sync.stats().asyncKicks==0&&sync.stats().asyncBuilds==0);
  sync.reset();async.reset();
 }
 overlapAndFailure();
 CHECK(FakeResource::alive==0);
 CHECK(kicks>0&&retracted>0&&asyncBuilds>0&&stolen>0&&failures>0&&phaseCompared>0&&midResets>0&&unconsumed>0);
 std::cout<<"static plan job: "<<trials<<" trials, "<<kicks<<" kicked frames, "<<asyncBuilds<<" worker builds installed, "<<stolen<<" stolen back ("<<retracted<<" jobs retracted unstarted), "<<failures<<" injected failures rebuilt synchronously, "
   <<midResets<<" resets mid-job, "<<unconsumed<<" unconsumed kicks; "<<compared<<" slot accesses bit-identical ("<<drawRecords<<" draw records), "<<phaseCompared<<"/"<<phases<<" worker phase-2 results equal, "<<digests<<" plan-state digests equal except used\n";
}
