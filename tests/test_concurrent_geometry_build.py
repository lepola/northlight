#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.153: run the production GI worker and geometry builder threads (the whole
extracted work()) against a render-thread simulator. Only the region-build block
is replaced by a synthetic scene whose roof height depends on the build centre,
so a probe solved against an older generation cannot pass the exact check.
Checks: the worker solves camera moves on the previous generation while a build
runs; the settled atlas equals an independent sequential solve bit-for-bit;
deferral with a still camera; stop/shutdown mid-build; builder and worker faults.
Native CPU only: no Windows, Wine, game, D3D or world cache.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
source=fp.src('world_renderer.h').read_text()
work=source[source.index('    void work() {'):source.index('\n    bool check(HRESULT h,const char* s)')]
begin=work.index('            // BEGIN REGION BUILD');end=work.index('            // END REGION BUILD')
work=work[:begin]+'''            // FIXTURE REGION BUILD
            {
                if(!generations.canAdmit()){{std::lock_guard<std::mutex> lock(mutex);generationStall.deferred(GetTickCount());}deferBuild("generation-deferred",NorthlightGeometryMemory::Sample{},20);continue;}
                {std::lock_guard<std::mutex> lock(mutex);generationStall.admitted();} /* mirrors production (test_pending_release_wiring.py) */
                if(fixtureDefers>0){--fixtureDefers;deferBuild("build-deferred",NorthlightGeometryMemory::Sample{},25);continue;}
                auto replacement=std::make_shared<NorthlightGI::BVH>();
                if(!generations.track(replacement))throw std::runtime_error("Geometry generation admission invariant");
                {std::lock_guard<std::mutex> lock(mutex);building=true;++buildsStarted;}
                std::this_thread::sleep_for(std::chrono::milliseconds(fixtureBuildMs.load()));
                {std::lock_guard<std::mutex> lock(mutex);building=false;}
                if(fixtureThrow.exchange(false))throw std::bad_alloc();
                if(fixtureFail.exchange(false)){result->message="fixture build failure";publishError();continue;}
                if(!currentGeometry()){++superseded;continue;}
                std::string error;if(!replacement->build(fixtureScene(r.geometryCenter),error))throw std::runtime_error(error);
                built->plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();built->prepared=std::make_shared<PreparedCommit>();
                built->bvh=replacement;built->map=r.map;built->center=r.geometryCenter;built->fog=std::make_shared<NorthlightRegionalFog::Field>();
                {std::lock_guard<std::mutex> lock(mutex);centers[replacement.get()]=r.geometryCenter;history.push_back(r.geometryCenter);lastBuilt=replacement;++buildsDone;}
            }
'''+work[end:]
assert work.count('std::thread([&]{')==1 and 'FIXTURE REGION BUILD' in work
snapshot=source[source.index('    struct Snapshot {'):source.index('\n    struct Request ')]
request=source[source.index('    struct Request {'):]
request=request[:request.index('\n')]
palette=source[source.index('    struct PaletteRegion {'):]
palette=palette[:palette.index('\n')]
orphaned=source[source.index('    template<class Pending,class Active> static bool pendingOrphaned('):]
orphaned=orphaned[:orphaned.index('\n')] # 0.3.156 production release rule
fixture=r'''
#include "world_probe_progress.h"
#include "world_dynamic_probes.h"
#include "world_streaming.h"
#include "gi_solve_pool.h"
#include "quality_settings.h"
#include "stream_hooks.h"
#include "geometry_memory.h"
#include "terrain_reach_fallback.h"
#include "memory_admission_probe.h"
#include "worker_actor_memo.h"
#include "diagnostics_switch.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
using V=NorthlightGI::Vec3;using DWORD=uint32_t;
static DWORD GetTickCount(){return DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static int GetCurrentThread(){return 0;}
static bool SetThreadPriority(int,int){return true;}
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
static std::mutex logMutex;static std::vector<std::string> logLines;
template<class... T> static void logf(const char* format,T... values){char line[2048];std::snprintf(line,sizeof line,format,values...);std::lock_guard<std::mutex> lock(logMutex);logLines.emplace_back(line);}
namespace NorthlightRegionalFog {struct Field {};struct Region {};constexpr double WorldZero=17066.6666667,TileSize=533.3333333;
 inline Region loadRegion(const std::string&,const std::string&,float,float){return {};}}
namespace NorthlightLocalLights {struct Light {};struct Cache {explicit Cache(const std::string&){}};}
namespace NorthlightActorGeometry {struct Result {std::shared_ptr<const NorthlightGI::WorldScene> scene;uint64_t hash=0;unsigned texturesDecoded=0;size_t textureEncodedBytes=0;};
 struct ActorJob {Result resolve()const{throw std::runtime_error("fixture actor failure");}};}
static NorthlightGI::WorldScene fixtureScene(V c){
 // Ground under the whole build box and a roof over the build centre whose
 // height changes every 32 units: generations differ near every window.
 NorthlightGI::WorldScene s;s.materials.push_back({});
 const float roof=24+8*float(((int(std::floor(c.x/32))%3)+3)%3);
 auto quad=[&](float x0,float y0,float x1,float y1,float z,V n){uint32_t b=uint32_t(s.vertices.size());
  for(V p:{V{x0,y0,z},V{x1,y0,z},V{x1,y1,z},V{x0,y1,z}})s.vertices.push_back({p,n});s.triangles.push_back({b,b+1,b+2,0});s.triangles.push_back({b,b+2,b+3,0});};
 quad(c.x-288,c.y-288,c.x+288,c.y+288,0,{0,0,1});quad(c.x-40,c.y-40,c.x+40,c.y+40,roof,{0,0,-1});return s;}
static NorthlightGI::Lighting fixtureLight(){NorthlightGI::Lighting l;l.sunDirection=V(.3f,.2f,.93f);l.sunIrradiance=V(3,2.9f,2.7f);l.skyRadiance=V(.4f,.5f,.7f);l.maxBounces=3;
 l.additionalDirections.push_back({V(0,0,1),V()});return l;}
struct Fixture {
 struct PreparedCommit {int tag=0;};
SNAPSHOT
REQUEST
PALETTE
ORPHANED
 NorthlightGeometryMemory::StallWatch generationStall;
 NorthlightQuality::Settings quality;std::string root="fixture/";
 std::mutex mutex;std::condition_variable wake;Request request;bool stopping=false,pending=false;
 std::atomic<bool> workerBusy{false},workerMemoryTrim{false};std::atomic<unsigned> workerFaultCode{0},geometryBuildEstimateMs{1000};
 std::shared_ptr<Snapshot> published;std::shared_ptr<const PaletteRegion> publishedPaletteRegion;
 // Fixture build controls and observations (guarded by mutex unless atomic).
 std::atomic<unsigned> fixtureBuildMs{60},fixtureDefers{0};std::atomic<bool> fixtureThrow{false},fixtureFail{false};
 bool building=false;unsigned buildsStarted=0,buildsDone=0;std::map<const NorthlightGI::BVH*,V> centers;std::vector<V> history;std::weak_ptr<NorthlightGI::BVH> lastBuilt;
 static NorthlightGeometryMemory::Sample geometryAdmissionFor(NorthlightMemoryAdmission::Probe&,NorthlightGeometryMemory::Budget,bool* exact=nullptr){
  if(exact)*exact=true;NorthlightGeometryMemory::Sample s;s.available=s.largest=uint64_t(3)<<30;s.valid=true;return s;}
 static void logGeometryMemory(const char*,NorthlightGeometryMemory::Sample,size_t=0,bool=true){}
WORK
};
struct Harness {
 Fixture f;std::thread thread;std::map<uint64_t,V> cameras;
 Harness(unsigned threads,unsigned buildMs,unsigned defers){f.quality.giThreads=threads;f.fixtureBuildMs=buildMs;f.fixtureDefers=defers;thread=std::thread([this]{f.work();});}
 ~Harness(){if(thread.joinable())stop();}
 // lead: the 0.3.169 geometry lead (region build centre ahead of the eye); 0 = centred on the eye.
 void issue(V camera,std::shared_ptr<const NorthlightActorGeometry::ActorJob> job=nullptr,const char* map="Azeroth",float lead=0){
  {std::lock_guard<std::mutex> lock(f.mutex);auto r=f.request;r.map=map;r.camera=r.probeCenter=camera;r.geometryCenter=V(camera.x+lead,camera.y,camera.z);r.light=fixtureLight();r.actorJob=job;
   r.reason=f.request.id?2u:1u;r.id=f.request.id+1;r.baseId=r.id;r.queuedAt=GetTickCount();f.request=r;f.pending=true;cameras[r.id]=camera;}
  f.wake.notify_one();}
 // The WorldRenderer destructor sequence.
 double stop(){const auto t=std::chrono::steady_clock::now();{std::lock_guard<std::mutex> lock(f.mutex);f.stopping=true;}f.wake.notify_one();thread.join();
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
 template<class P> bool waitFor(P predicate,unsigned ms){for(unsigned t=0;t<ms;t+=5){{std::lock_guard<std::mutex> lock(f.mutex);if(predicate())return true;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;}
 // The worker must also be idle: a geometry publication at adoption copies the previous
 // final GI snapshot (display fallback) and already names the new BVH.
 bool settled(unsigned ms=180000){return waitFor([&]{auto last=f.lastBuilt.lock();auto p=f.published;
  return p&&p->serial&&!p->partial&&p->requestId==f.request.id&&last&&p->bvh==last&&!f.building&&!f.pending&&!f.workerBusy;},ms);}
};
static bool same(const NorthlightGI::Probe& a,const NorthlightGI::Probe& b){
 return !std::memcmp(a.sh,b.sh,sizeof a.sh)&&!std::memcmp(a.moments,b.moments,sizeof a.moments)&&a.samples==b.samples&&a.backFaceSamples==b.backFaceSamples&&a.valid==b.valid&&a.maxDistance==b.maxDistance;}
// Every occupied slot equals an independent sequential solve of its key against the
// final generation; the final window is complete. Returns window probes that differ
// from the previous generation (proves an old-generation probe would be detected).
static unsigned verifySettled(Harness& h,V previousCenter){
 std::shared_ptr<Fixture::Snapshot> p;V camera;{std::lock_guard<std::mutex> lock(h.f.mutex);p=h.f.published;camera=h.f.request.camera;}
 auto light=fixtureLight();light.points.clear();light.movingGeometry=nullptr;const auto prepared=NorthlightGI::prepareLighting(light);
 assert(p->atlas.size()==NorthlightGI::probeLayout().atlasSize());unsigned occupied=0;
 for(size_t i=0;i<p->atlas.size();++i){const auto& e=p->atlas[i];if(!e.occupied)continue;++occupied;assert(NorthlightGI::probeAtlasIndex(e.key)==i);
  const V q(float(e.key.x)*8,float(e.key.y)*8,float(e.key.z)*8);
  assert(same(e.probe,NorthlightGI::solveProbePrepared(*p->bvh,q,prepared,h.f.quality.giRays,NorthlightGI::probeSeed(e.key))));}
 NorthlightGI::BVH previous;std::string error;assert(previous.build(fixtureScene(previousCenter),error));unsigned differs=0;
 const V origin=NorthlightGI::probeWindowOrigin(camera);
 for(unsigned i=0;i<NorthlightGI::probeLayout().count();++i){const V q=NorthlightGI::probeWindowPosition(origin,i);NorthlightGI::ProbeGridKey k;assert(NorthlightGI::probeGridKey(q,k));
  const auto& e=p->atlas[NorthlightGI::probeAtlasIndex(k)];assert(e.occupied&&e.key==k);
  if(!same(e.probe,NorthlightGI::solveProbePrepared(previous,q,prepared,h.f.quality.giRays,NorthlightGI::probeSeed(k))))++differs;}
 assert(occupied>=NorthlightGI::probeLayout().count());return differs;}
static unsigned logged(const char* field){unsigned total=0;std::lock_guard<std::mutex> lock(logMutex);
 for(const auto& l:logLines){if(l.rfind("WORLD geometry published",0))continue;auto at=l.find(field);assert(at!=std::string::npos);total+=unsigned(std::strtoul(l.c_str()+at+std::strlen(field),nullptr,10));}return total;}
int main(){
 for(auto run:{std::pair<unsigned,unsigned>{1,14},{2,14},{2,18}}){
  // 0.3.153 GIDistance: the 14-cell window (GIDistance=52) and GIDistance=68 (18 cells).
  const unsigned threads=run.first;assert(NorthlightGI::configureProbeLayout(NorthlightGI::probeLayoutFor(run.second)));
  logLines.clear();
  // Moving camera: 4 x 9 units then a short hold, so each hold starts a build
  // (>32 units from the region centre) while the worker keeps receiving moves.
  Harness h(threads,100,2);V camera(3,5,4);h.issue(camera);
  assert(h.settled()); // two deferrals with a still camera still complete
  unsigned servedDuringBuild=0,observed=0;uint64_t lastSerial=0;
  // Worker GI publications only (a geometry publication keeps its source serial).
  auto observe=[&]{std::lock_guard<std::mutex> lock(h.f.mutex);auto p=h.f.published;if(!p||!p->serial||p->serial==lastSerial)return;lastSerial=p->serial;++observed;
   const V c=h.f.centers.at(p->bvh.get()),q=h.cameras.at(p->requestId),d=q-c;if(NorthlightGI::dot(d,d)>32.f*32.f)++servedDuringBuild;};
  for(unsigned leg=0;leg<8;++leg){
   for(unsigned step=0;step<4;++step){camera.x+=9;h.issue(camera);for(unsigned t=0;t<5;++t){observe();std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
   for(unsigned t=0;t<40;++t){observe();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  }
  {std::lock_guard<std::mutex> lock(h.f.mutex);auto p=h.f.published;assert(p&&p->bvh);}
  assert(h.settled());
  V previousCenter;{std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.history.size()>=2);previousCenter=h.f.history[h.f.history.size()-2];}
  const unsigned differs=verifySettled(h,previousCenter);assert(differs>100);
  const unsigned solves=logged(" concurrentSolves="),stall=logged(" stallMs=");unsigned builds;{std::lock_guard<std::mutex> lock(h.f.mutex);builds=h.f.buildsDone;}
  assert(builds>=8&&servedDuringBuild>0&&solves>0);
  const double stopMs=h.stop();
  assert(NorthlightGI::configureProbeLayout(NorthlightGI::ProbeLayout{}));
  std::printf("moving grid=%u GIThreads=%u builds=%u publications=%u servedDuringBuild=%u concurrentSolves=%u stallMsTotal=%u windowProbesDifferingFromPreviousGeneration=%u stopMs=%.1f\n",run.second,threads,builds,observed,servedDuringBuild,solves,stall,differs,stopMs);
 }
 {// Stop mid-build: the destructor sequence joins both threads; nothing is delivered after stop.
  Harness h(1,400,0);h.issue({3,5,4});assert(h.waitFor([&]{return h.f.building;},10000));
  const double ms=h.stop();assert(ms<2000);assert(!h.f.published||!h.f.published->bvh);std::printf("stop during first build: joined in %.1f ms\n",ms);}
 {// Stop while the worker serves the previous generation and the builder runs.
  Harness h(2,400,0);h.issue({3,5,4});assert(h.settled());h.issue({43,5,4});
  assert(h.waitFor([&]{return h.f.building&&h.f.published&&h.f.published->serial&&h.f.published->requestId==h.f.request.id&&!h.f.published->partial;},30000));
  const double ms=h.stop();assert(ms<2000);std::printf("stop during concurrent solve and build: joined in %.1f ms\n",ms);}
 {// Builder fault: reported through workerFaultCode; shutdown still joins.
  Harness h(1,50,0);h.f.fixtureThrow=true;h.issue({3,5,4});assert(h.waitFor([&]{return h.f.workerFaultCode.load()==1;},10000));
  const double ms=h.stop();std::printf("builder fault: code=1 joined in %.1f ms\n",ms);}
 {// Worker fault while the builder runs: the worker's exit joins the builder, which delivers nothing.
  Harness h(1,300,0);h.issue({3,5,4});assert(h.settled());h.issue({43,5,4});assert(h.waitFor([&]{return h.f.building;},10000));
  h.issue({44,5,4},std::make_shared<NorthlightActorGeometry::ActorJob>());
  assert(h.waitFor([&]{return h.f.workerFaultCode.load()==2;},10000));h.thread.join();
  assert(h.f.buildsDone==2&&h.f.published->bvh&&h.f.published->bvh!=h.f.lastBuilt.lock()&&!h.f.lastBuilt.lock());std::puts("worker fault during build: builder joined, no delivery");}
 {// (a) Map change while a build runs: the builder supersedes it, the worker drops its region
  // and waits; only the new map's region is delivered and settles exactly.
  Harness h(1,300,0);h.issue({3,5,4});assert(h.settled());h.issue({43,5,4});assert(h.waitFor([&]{return h.f.building;},10000));
  h.issue({43,5,4},nullptr,"Kalimdor");assert(h.waitFor([&]{return h.f.buildsDone==2;},30000)&&h.settled());
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.published->map=="Kalimdor"&&h.f.buildsDone==2&&h.f.buildsStarted==3&&h.f.published->superseded>=1);}
  const unsigned differs=verifySettled(h,{3,5,4});std::printf("map change during build: superseded, Kalimdor settled exactly (window probes differing from previous=%u)\n",differs);}
 {// (b) Build failure while serving the previous region: the region is retained and keeps
  // serving GI; the next request retries the build.
  Harness h(1,150,0);h.issue({3,5,4});assert(h.settled());std::shared_ptr<NorthlightGI::BVH> old;{std::lock_guard<std::mutex> lock(h.f.mutex);old=h.f.published->bvh;}
  h.f.fixtureFail=true;h.issue({43,5,4});
  assert(h.waitFor([&]{return !h.f.fixtureFail&&!h.f.building&&h.f.published->bvh==old&&!h.f.published->partial&&h.f.published->requestId==h.f.request.id;},30000));
  // The flag clears before publishError() logs; wait for the line (lock order mutex -> logMutex, as in production).
  assert(h.waitFor([&]{std::lock_guard<std::mutex> lock(logMutex);for(const auto& l:logLines)if(l.rfind("WORLD replacement deferred",0)==0)return true;return false;},30000));
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.buildsDone==1&&h.f.published->message.empty());}
  old.reset();h.issue({44,5,4});assert(h.waitFor([&]{return h.f.buildsDone==2;},30000)&&h.settled());{std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.buildsDone==2&&h.f.history.back().x==44);}
  verifySettled(h,{3,5,4});std::puts("build failure while serving: previous region retained and served, retry on next request settles exactly");}
 {// (c) Stale adoption: the camera is >64 from the delivered centre (still <96): geometry is
  // published, GI is skipped, the worker drops it and a rebuild follows without spinning.
  logLines.clear();Harness h(1,250,0);h.issue({3,5,4});assert(h.settled());h.issue({43,5,4});assert(h.waitFor([&]{return h.f.building;},10000));
  h.issue({113,5,4});assert(h.waitFor([&]{return h.f.buildsDone==3;},30000)&&h.settled());
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.buildsDone==3&&h.f.history[1].x==43&&h.f.history[2].x==113);}
  unsigned published=0;bool stalled=false;{std::lock_guard<std::mutex> lock(logMutex);for(const auto& l:logLines)if(!l.rfind("WORLD geometry published",0)){++published;stalled|=l.find(" stallMs=0")==std::string::npos&&published==3;}}
  assert(published==3&&stalled);verifySettled(h,{43,5,4});std::puts("stale adoption >64: published for shadows, dropped, rebuilt once, settled exactly");}
 {// (d) 0.3.156 orphaned staged upload (0.3.155 game log): the render thread adopts region A and
  // stages its upload; fast flight retires `active` (>96); build B is published already out of
  // range, so B and the staged A fill Generations<BVH,2> and the builder defers for ever. The
  // production release rule (pendingOrphaned) frees A; the next build is admitted and adopted.
  {NorthlightGeometryMemory::StallWatch w;assert(!w.due(100,10));w.deferred(0);assert(w.armed&&w.since==0&&!w.due(9,10)&&w.due(10,10)&&!w.due(99,10));w.deferred(50);assert(w.since==0);w.admitted();assert(!w.armed&&!w.due(1000,10));
   // 0.3.157: a deferral stamped at the same tick the render thread checks never fires (was now|1 -> wrap).
   NorthlightGeometryMemory::StallWatch v;v.deferred(1000);assert(!v.due(1000,10)&&!v.due(1009,10)&&v.due(1010,10));}
  Harness h(1,100,0);h.issue({3,5,4});assert(h.settled());
  struct Pending {std::shared_ptr<NorthlightGI::BVH> bvh;std::string map;};std::unique_ptr<Pending> pending;std::shared_ptr<Fixture::Snapshot> active;
  // (d) with the 0.3.169 hold the jumps are beyond its 160 limit: 200 units each.
  // One render frame: adopt a publication, retire an active beyond the 0.3.169 hard limit (updateWorldContext),
  // then the 0.3.156 rule at the same site; without the rule the staged upload is never released.
  auto frame=[&](bool rule){std::lock_guard<std::mutex> lock(h.f.mutex);const auto& q=h.f.request;auto p=h.f.published;
   if(p&&p!=active&&NorthlightWorldStreaming::adopts(p->map,p->center,bool(p->bvh),active&&active->bvh,q.map,q.camera))active=p;
   if(active&&!NorthlightWorldStreaming::retained(active->map,active->center,q.map,q.camera))active.reset();
   if(rule&&Fixture::pendingOrphaned(pending.get(),active.get()))pending.reset();};
  frame(true);assert(active&&active->bvh);pending.reset(new Pending{active->bvh,active->map});std::weak_ptr<NorthlightGI::BVH> a=active->bvh;
  h.issue({203,5,4});frame(false);assert(!active&&pending); /* active retired; the staged upload stays (0.3.155) */
  assert(h.waitFor([&]{return h.f.buildsDone==2&&h.f.published->bvh==h.f.lastBuilt.lock();},30000)); /* B published */
  h.issue({403,5,4});frame(false);assert(!active); /* B is out of range before the render thread adopts it */
  uint64_t still;{std::lock_guard<std::mutex> lock(h.f.mutex);still=h.f.request.id;} /* the camera stays still from here */
  assert(h.waitFor([&]{return h.f.generationStall.armed;},30000));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));frame(false);
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.buildsStarted==2&&h.f.buildsDone==2&&h.f.generationStall.armed&&!a.expired());} /* deadlocked without the rule */
  frame(true);assert(!pending);
  assert(h.waitFor([&]{return h.f.buildsDone==3;},30000)&&h.settled());
  for(unsigned i=0;i<20&&!active;++i){frame(true);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(a.expired()&&active&&active->bvh==h.f.lastBuilt.lock()&&h.f.history.back().x==403&&!h.f.generationStall.armed&&h.f.request.id==still);}
  verifySettled(h,{203,5,4});std::puts("orphaned staged upload: builder deferred while held; released by the production rule; generation freed, next build adopted, settled exactly");}
 // (e) 0.3.156 same-map teleport, then a still camera (no request after the jump): region A is
 // staged when the camera jumps 500 or 3000 units. Variants at the jump: 0 = only A exists
 // (published == staged, one generation); 1 = build B published but not yet adopted (A + B:
 // deadlock without the rule); 2 = build B still running (superseded by the builder itself).
 // Recovery must finish with the camera still: the builder's deferral retry re-runs admission.
 for(float jump:{500.f,3000.f})for(int variant=0;variant<3;++variant){
  Harness h(1,150,0);h.issue({3,5,4});assert(h.settled());
  struct Pending {std::shared_ptr<NorthlightGI::BVH> bvh;std::string map;};std::unique_ptr<Pending> pending;std::shared_ptr<Fixture::Snapshot> active;
  auto frame=[&](bool rule){std::lock_guard<std::mutex> lock(h.f.mutex);const auto& q=h.f.request;auto p=h.f.published;
   if(p&&p!=active&&NorthlightWorldStreaming::adopts(p->map,p->center,bool(p->bvh),active&&active->bvh,q.map,q.camera))active=p;
   if(active&&!NorthlightWorldStreaming::retained(active->map,active->center,q.map,q.camera))active.reset();
   if(rule&&Fixture::pendingOrphaned(pending.get(),active.get()))pending.reset();};
  frame(true);assert(active);pending.reset(new Pending{active->bvh,active->map});std::weak_ptr<NorthlightGI::BVH> a=active->bvh;
  if(variant==1){h.issue({43,5,4});assert(h.waitFor([&]{return h.f.buildsDone==2&&h.f.published->bvh==h.f.lastBuilt.lock()&&!h.f.building;},30000));}
  if(variant==2){h.issue({43,5,4});assert(h.waitFor([&]{return h.f.building;},30000));}
  const V target(3+jump,5,4);h.issue(target);uint64_t still;{std::lock_guard<std::mutex> lock(h.f.mutex);still=h.f.request.id;}
  frame(false);assert(!active&&pending);
  unsigned started=0;
  if(variant==1){ /* A (staged) + B (published, out of range): the builder defers until the rule runs */
   assert(h.waitFor([&]{return h.f.generationStall.armed;},30000));std::this_thread::sleep_for(std::chrono::milliseconds(400));frame(false);
   {std::lock_guard<std::mutex> lock(h.f.mutex);started=h.f.buildsStarted;assert(started==2&&h.f.buildsDone==2&&!a.expired());}}
  for(unsigned t=0;t<6000;++t){frame(true);{std::lock_guard<std::mutex> lock(h.f.mutex);if(active&&h.f.published==active&&!h.f.published->partial&&h.f.published->requestId==still&&!h.f.workerBusy&&!h.f.pending)break;}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  {std::lock_guard<std::mutex> lock(h.f.mutex);
   assert(!pending&&a.expired()&&active&&active->bvh==h.f.lastBuilt.lock()&&h.f.history.back().x==target.x&&h.f.request.id==still&&!h.f.generationStall.armed);
   if(variant==1)assert(h.f.buildsStarted==started+1);}
  assert(h.settled());verifySettled(h,{3,5,4});
  std::printf("teleport %.0f variant=%d: still camera recovered (request id unchanged, staged generation freed, jump region adopted)\n",jump,variant);}
 // (f) 0.3.169 coverage hold: region A is active and staged when the camera jumps 120 or 150 units
 // on the same map and stays still. A is beyond the 96 coverage but held (no vanilla frame) while
 // B builds; B is adopted within the same hard limit, A's staged upload is released by the
 // production rule and its generation freed, without a generation stall.
 for(float jump:{120.f,150.f}){
  Harness h(1,150,0);h.issue({3,5,4});assert(h.settled());
  struct Pending {std::shared_ptr<NorthlightGI::BVH> bvh;std::string map;};std::unique_ptr<Pending> pending;std::shared_ptr<Fixture::Snapshot> active;
  unsigned heldFrames=0,emptyFrames=0;
  auto frame=[&]{std::lock_guard<std::mutex> lock(h.f.mutex);const auto& q=h.f.request;auto p=h.f.published;
   if(p&&p!=active&&NorthlightWorldStreaming::adopts(p->map,p->center,bool(p->bvh),active&&active->bvh,q.map,q.camera))active=p;
   if(active&&!NorthlightWorldStreaming::retained(active->map,active->center,q.map,q.camera))active.reset();
   if(Fixture::pendingOrphaned(pending.get(),active.get()))pending.reset();
   if(!active)++emptyFrames;else if(!NorthlightWorldStreaming::applicable(active->map,active->center,q.map,q.camera))++heldFrames;};
  frame();assert(active&&active->bvh);pending.reset(new Pending{active->bvh,active->map});std::weak_ptr<NorthlightGI::BVH> a=active->bvh;
  const V target(3+jump,5,4);h.issue(target);
  frame();{std::lock_guard<std::mutex> lock(h.f.mutex);assert(active&&active->bvh==a.lock()&&pending&&!NorthlightWorldStreaming::applicable(active->map,active->center,h.f.request.map,h.f.request.camera));}
  for(unsigned t=0;t<6000;++t){frame();{std::lock_guard<std::mutex> lock(h.f.mutex);if(active&&active->bvh==h.f.lastBuilt.lock()&&h.f.history.back().x==target.x)break;}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  {std::lock_guard<std::mutex> lock(h.f.mutex);
   assert(active&&active->bvh==h.f.lastBuilt.lock()&&h.f.history.back().x==target.x&&!pending&&a.expired()&&!emptyFrames&&heldFrames>0);
   assert(h.f.buildsStarted==2&&h.f.buildsDone==2&&!h.f.generationStall.armed);}
  for(unsigned t=0;t<40;++t){frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  assert(h.settled());verifySettled(h,{3,5,4});
  std::printf("hold %.0f: A held for %u frames (no empty frame), B adopted, staged A released, no stall\n",jump,heldFrames);}
 {// (g) 0.3.169 geometry lead: a region built 60 ahead of the eye. When the eye falls more than 64
  // behind it while the lead point stays within the 32-unit refresh, the worker publishes it and
  // waits for the next request: no pending re-run (spin), no extra build. It solves once the eye arrives.
  Harness h(1,100,0);h.issue({3,5,4});assert(h.settled());
  h.issue({23,5,4},nullptr,"Azeroth",60);assert(h.waitFor([&]{return h.f.buildsDone==2;},30000));
  {std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.history.back().x==83);}
  h.issue({13,5,4},nullptr,"Azeroth",60); /* eye 70 behind the region, lead point 10 from it */
  assert(h.waitFor([&]{return h.f.published&&h.f.published->bvh==h.f.lastBuilt.lock()&&!h.f.pending&&!h.f.workerBusy;},30000));
  for(unsigned t=0;t<40;++t){std::this_thread::sleep_for(std::chrono::milliseconds(5));std::lock_guard<std::mutex> lock(h.f.mutex);assert(!h.f.pending&&!h.f.building&&h.f.buildsStarted==2);}
  h.issue({33,5,4},nullptr,"Azeroth",60); /* eye 50 from the region: GI solves on it */
  assert(h.settled());{std::lock_guard<std::mutex> lock(h.f.mutex);assert(h.f.buildsStarted==2&&h.f.buildsDone==2);}
  verifySettled(h,{3,5,4});std::puts("lead: region built ahead, worker idles (no spin, no extra build) until the eye is within 64, then settles exactly");}
 std::puts("PASS concurrent geometry build: moves solved on the previous generation during builds; settled atlas bit-identical to a sequential final-generation solve; still-camera deferral; stop mid-build; builder and worker faults join; map change, build failure and stale adoption during builds; orphaned staged upload released (0.3.156); same-map teleports recover with a still camera; 0.3.169 120/150 hold adopts without a stall; lead region idles without spinning");
}
'''.replace('SNAPSHOT',snapshot).replace('REQUEST',request).replace('PALETTE',palette).replace('ORPHANED',orphaned).replace('WORK',work)
report={'status':'pass','game_launched':False,'runs':[],'sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in ['world_renderer.h','world_probe_progress.h','world_probe_cache.h','gi_solve_pool.h','world_gi.cpp','test_concurrent_geometry_build.py']}}
modes=[('O2',['-O2']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']),('tsan',['-O1','-g','-fsanitize=thread'])]
with tempfile.TemporaryDirectory(prefix='northlight-concurrent-geometry-') as tmp:
    path=Path(tmp);(path/'test.cpp').write_text(fixture)
    for mode,flags in modes:
        command=['clang++','-std=c++17','-pthread',*flags,'-UNDEBUG',*fp.test_include_flags(),str(path/'test.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(path/mode)]
        subprocess.run(command,check=True)
        result=subprocess.run([str(path/mode)],capture_output=True,text=True,timeout=1800)
        print(mode,result.stdout.strip(),result.stderr.strip());result.check_returncode()
        assert 'WARNING: ThreadSanitizer' not in result.stderr
        report['runs'].append({'mode':mode,'stdout':result.stdout,'stderr':result.stderr})
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
(out/'concurrent-geometry-build.json').write_text(json.dumps(report,indent=2)+'\n')
