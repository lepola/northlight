#!/usr/bin/env python3
# northlight-test: requires=cxx slow
"""Execute the production worker publication/solve tail using deterministic requests.
Native CPU only: no Windows, Wine, game, D3D, geometry loading or actor capture.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
source=fp.src('world_renderer.h').read_text()
start=source.index('            r.light.points=sceneLights;r.light.movingGeometry=nullptr;')
end=source.index('\n        }\n      }catch(const std::bad_alloc&)',start)
body=source[start:end]
start=source.index('    struct Snapshot {');end=source.index('\n    struct Request ',start)
snapshot=source[start:end]
fixture=r'''
#include "world_probe_progress.h"
#include "world_dynamic_probes.h"
#include "gi_solve_pool.h"
#include "quality_settings.h"
#include <cassert>
#include <functional>
#include <cstring>
#include <memory>
#include <mutex>
#include <cstdio>
using V=NorthlightGI::Vec3;using DWORD=uint32_t;
namespace NorthlightWorldMesh {struct WorldMeshUploadPlan {size_t cpuBytes()const{return 16;}};}
namespace NorthlightRegionalFog {struct Field {};}
namespace NorthlightLocalLights {struct Light {};}
namespace NorthlightLightAttachments {struct Records {};}
static DWORD now=0;static unsigned solveCalls=0;static std::function<void()> afterSolve,onTick;
static DWORD GetTickCount(){if(onTick)onTick();return now;}
namespace NorthlightGI {
static Probe fixtureSolve(const BVH& bvh,Vec3 p,const PreparedLighting& light,unsigned rays,uint32_t seed,const BVH* moving=nullptr){
 assert(rays==64&&light.values().maxBounces==3);auto result=solveProbePrepared(bvh,p,light,rays,seed,moving);++solveCalls;++now;if(afterSolve)afterSolve();return result;}
static Probe fixtureMoving(const BVH& bvh,Vec3 p,const PreparedLighting& light,unsigned rays,uint32_t seed,const BVH& moving,StaticPathRecord& record,uint64_t generation,MovingSolveStats* stats){
 assert(rays==64&&light.values().maxBounces==3&&generation&&stats);auto result=solveProbeMoving(bvh,p,light,rays,seed,moving,record,generation,stats);
 const auto direct=solveProbePrepared(bvh,p,light,rays,seed,&moving);assert(!std::memcmp(result.sh,direct.sh,sizeof direct.sh)&&!std::memcmp(result.moments,direct.moments,sizeof direct.moments)&&result.valid==direct.valid);
 ++solveCalls;++now;if(afterSolve)afterSolve();return result;}
}
#define solveProbePrepared fixtureSolve
#define solveProbeMoving fixtureMoving
struct Fixture {
 struct PreparedCommit {int tag=0;};
SNAPSHOT
 struct Request {std::shared_ptr<const int> actorJob;V camera,probeCenter;NorthlightGI::Lighting light;std::string map="Azeroth";uint64_t id=1,baseId=1;DWORD queuedAt=0;unsigned reason=1;bool enabled=true,giEnabled=true;};
 // The production relight cache answers repeated keys without solving; this tail test counts every solve and advances its clock
 // per solve, so it uses a pass-through with the same interface (the cache itself: test_probe_blend_equivalence / world_probe_cache).
 struct PassThroughRelight {
  void reset(){} void configure(const NorthlightGI::PreparedLighting&,unsigned){}
  NorthlightGI::Probe solve(const NorthlightGI::BVH& b,NorthlightGI::ProbeGridKey,V q,const NorthlightGI::PreparedLighting& l,unsigned rays,uint32_t seed){return NorthlightGI::solveProbePrepared(b,q,l,rays,seed);}
  NorthlightGI::ProbeRelightCache::Stats stats()const{return {};}
 } relightCache;
 Request request;
 std::shared_ptr<NorthlightGI::BVH> bvh=std::make_shared<NorthlightGI::BVH>(),probeCacheGeometry,actors;
 std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> scenePlan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
 std::shared_ptr<const NorthlightRegionalFog::Field> sceneFog;
 std::shared_ptr<const NorthlightLightAttachments::Records> sceneLightAttachments=std::make_shared<NorthlightLightAttachments::Records>();
 std::shared_ptr<const PreparedCommit> scenePrepared=std::make_shared<PreparedCommit>();
 std::vector<NorthlightLocalLights::Light> rawSceneLights;
 std::vector<NorthlightGI::PointLight> sceneLights;
 std::vector<NorthlightGI::ProbeAtlasEntry> displayFallback;
 std::shared_ptr<Snapshot> published,previousLighting;
 V sceneCenter;std::string sceneMap="Azeroth";
 NorthlightGI::ProbeCache probeCache;NorthlightGI::DynamicProbeLayer dynamicProbes;
 NorthlightQuality::Settings quality;std::unique_ptr<NorthlightGI::SolvePool> solvePool; /* 0.3.138 knobs at defaults: serial loop, 64 rays, 3 bounces */
 NorthlightGI::Lighting probeCacheLight;NorthlightGI::PreparedLighting preparedLight;
 NorthlightGI::ProbePublicationCadence publicationCadence;
 NorthlightGI::MovingSolveStats movingStats;unsigned superseded=0,retargeted=0;uint64_t serial=0,lightingGeneration=0,solvedActorHash=7,completedActorHash=0,completedBaseId=0;
 bool stopping=false,pending=true;std::mutex mutex;
 std::unique_ptr<int> builtGeometry;bool concurrent=false;unsigned concurrentSolves=0; /* 0.3.153 builder handoff */
 struct Observed {std::shared_ptr<const Snapshot> value;DWORD at;uint64_t hash;};std::vector<Observed> observed;
 static uint64_t hash(const Snapshot& s){uint64_t h=1469598103934665603ull;for(const auto& e:s.atlas){auto add=[&](uint32_t v){h^=v;h*=1099511628211ull;};add(e.occupied);if(e.occupied){add(e.key.x);add(e.key.y);add(e.key.z);add(e.probe.valid);add(e.probe.samples);for(const auto& v:e.probe.sh){uint32_t bits[3];std::memcpy(bits,&v,12);for(auto x:bits)add(x);}}}return h;}
 Fixture(){std::string error;assert(bvh->build({},error));}
 void observe(){if(published&&published->serial&&(observed.empty()||observed.back().value!=published))observed.push_back({published,now,hash(*published)});}
 void next(V camera,unsigned reason=2){request.camera=request.probeCenter=camera;++request.id;request.baseId=request.id;request.reason=reason;request.queuedAt=now;pending=true;}
 void run(unsigned maxRequests=16){
  for(unsigned iteration=0;iteration<maxRequests&&pending;++iteration){
   Request r=request;pending=false;auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.camera;result->requestId=r.id;result->requestedAt=r.queuedAt;
   BODY
  }
  observe();
 }
 void immutable()const{for(const auto& o:observed)assert(hash(*o.value)==o.hash);}
};
#undef solveProbePrepared
#undef solveProbeMoving
static void reset(){now=0;solveCalls=0;afterSolve={};onTick={};}
static unsigned occupied(const std::vector<NorthlightGI::ProbeAtlasEntry>& a){unsigned n=0;for(const auto& e:a)n+=e.occupied;return n;}
int main(){
 const unsigned W=NorthlightGI::probeLayout().count(); /* 2744 at the default layout */
 reset();Fixture cold;onTick=[&]{cold.observe();};cold.run();assert(!cold.pending&&cold.published&&!cold.published->partial&&cold.published->processedProbes==W&&cold.completedBaseId==1);assert(cold.observed.front().value->partial&&cold.observed.front().value->processedProbes==8&&cold.observed.front().at==8);const unsigned interval=NorthlightGI::ProbePublicationCadence::IntervalMs;assert(cold.observed.size()>5&&cold.observed.size()<=W/interval+3);
 DWORD lastTick=0;for(const auto& o:cold.observed)if(o.value->partial){if(lastTick)assert(o.at-lastTick>=interval);lastTick=o.at;assert(o.value->lightingGeneration==1);}cold.immutable();
 assert(cold.published->lightAttachments==cold.sceneLightAttachments);
 // Disabling renderer/GI work during a solve must fence partial and final publication.
 for(unsigned mode=0;mode<2;++mode){reset();Fixture disabled;onTick=[&]{disabled.observe();};afterSolve=[&]{if(solveCalls==100){if(mode)disabled.request.giEnabled=false;else disabled.request.enabled=false;}};disabled.run(1);
  assert(disabled.completedBaseId==0&&disabled.superseded==1&&disabled.published&&disabled.published->partial);
  for(const auto& o:disabled.observed)assert(o.at<=100);disabled.immutable();}
 // Repeated camera changes inside one BVH region retain all solved world
 // points, refresh the nearest-first window, and do not defer the pub clock.
 reset();Fixture moving;onTick=[&]{moving.observe();};unsigned changes=0;afterSolve=[&]{if(changes<8&&solveCalls>=unsigned(100*(changes+1))){++changes;moving.next({float(changes*4),0,0});}};moving.run();assert(changes==8&&moving.retargeted==8&&moving.superseded==0&&!moving.pending&&!moving.published->partial&&moving.completedBaseId==moving.request.baseId);assert(moving.published->lightingGeneration==1&&moving.published->reusedProbes>0);assert(solveCalls<W+8*400);moving.immutable();
 // A new static lighting generation keeps already displayed GI until each
 // replacement is solved. Old samples never enter the new solve cache.
 afterSolve={};onTick={};auto old=cold.published;cold.next({},16);cold.request.light.skyRadiance={.7f,.8f,.9f};cold.observed.clear();unsigned before=solveCalls;onTick=[&]{cold.observe();};cold.run();assert(cold.published->lightingGeneration==2&&cold.published->solvedProbes==W&&cold.probeCache.size()==W&&cold.displayFallback.empty());bool checked=false;for(const auto& o:cold.observed)if(o.value->lightingGeneration==2&&o.value->partial){assert(occupied(o.value->atlas)==W);assert(o.value->solvedProbes==8);unsigned unchanged=0;for(size_t i=0;i<o.value->atlas.size();++i)if(o.value->atlas[i].occupied&&o.value->atlas[i].probe.sh[0].x==old->atlas[i].probe.sh[0].x)++unchanged;assert(unchanged>=W-8);checked=true;break;}assert(checked&&solveCalls-before==W);cold.immutable();
 // Latest reason may be camera-only after overwriting a light-change request.
 // No superseded generation may publish against those latest light values.
 reset();Fixture lighting;onTick=[&]{lighting.observe();};afterSolve=[&]{if(solveCalls==100){lighting.next({8,0,0});lighting.request.light.skyRadiance={2,3,4};}};lighting.run();assert(lighting.superseded==1&&lighting.retargeted==0&&lighting.lightingGeneration==2&&!lighting.published->partial);for(const auto& o:lighting.observed)if(o.value->lightingGeneration==1)assert(o.at<=100);lighting.immutable();
 // Map and distant-region requests reject in-flight publication entirely.
 for(unsigned mode=0;mode<2;++mode){reset();Fixture changed;onTick=[&]{changed.observe();};afterSolve=[&]{if(solveCalls==256){changed.next(mode?V{80,0,0}:V{});if(!mode)changed.request.map="Kalimdor";}};changed.run(1);assert(changed.pending&&changed.completedBaseId==0);assert(changed.superseded==1);for(const auto& o:changed.observed)assert(o.at<=256);changed.immutable();}
 // A camera request on the last solve cannot mark an incomplete request complete. World-keyed transport is no
 // longer cancelled by a camera retarget (compatibleTransport), so the pass finishes and the queued request still
 // produces a full final from the solved world points.
 reset();Fixture last;onTick=[&]{last.observe();};afterSolve=[&]{if(solveCalls==W)last.next({8,0,0});};last.run();assert(last.retargeted==0&&last.completedBaseId==2&&!last.published->partial&&last.published->processedProbes==W);last.immutable();
 // Cancellation during a dynamic overlay publishes only an independent
 // cached-only export; current atlas is never mutated after publication.
 reset();Fixture dyn;NorthlightGI::WorldScene actor;actor.materials.push_back({});for(V p:{V{-4,-4,0},V{4,-4,0},V{0,4,0}})actor.vertices.push_back({p,{0,0,1}});actor.triangles.push_back({0,1,2,0});dyn.actors=std::make_shared<NorthlightGI::BVH>();std::string error;assert(dyn.actors->build(std::move(actor),error));onTick=[&]{dyn.observe();};afterSolve=[&]{if(solveCalls==W+6)dyn.next({8,0,0});};dyn.run();assert(dyn.completedBaseId==2&&!dyn.published->partial&&dyn.published->dynamicSolved<=128);dyn.immutable();
 // Stop may happen during a batch; no final publication follows it.
 reset();Fixture stopped;onTick=[&]{stopped.observe();};afterSolve=[&]{if(solveCalls==9)stopped.stopping=true;};stopped.run();assert(stopped.completedBaseId==0&&stopped.published->partial);stopped.immutable();
 // 0.3.153: a finished geometry build stops a pass on the previous generation
 // without counting a cancellation; completed passes during a build are counted.
 reset();Fixture swap;onTick=[&]{swap.observe();};afterSolve=[&]{if(solveCalls==300)swap.builtGeometry=std::make_unique<int>(1);};swap.run(1);assert(swap.completedBaseId==0&&swap.superseded==0&&swap.retargeted==0&&swap.published->partial&&swap.published->processedProbes<W&&!swap.concurrentSolves);swap.immutable();
 reset();Fixture during;during.concurrent=true;during.run();assert(during.completedBaseId==1&&during.concurrentSolves==1&&!during.published->partial);
 // 0.3.153 GIProbeAhead: only the probe window (origin, solve order) follows probeCenter;
 // the eye keeps the region/publication checks. The window is solved exactly as if centred there.
 {reset();Fixture ahead;ahead.request.camera={3,5,4};ahead.request.probeCenter={23,5,4};ahead.run();const auto& p=*ahead.published;
  assert(!p.partial&&p.processedProbes==W&&p.origin.x==NorthlightGI::probeWindowOrigin({23,5,4}).x&&p.origin.x!=NorthlightGI::probeWindowOrigin({3,5,4}).x);
  for(unsigned i=0;i<W;++i){NorthlightGI::ProbeGridKey k;assert(NorthlightGI::probeGridKey(NorthlightGI::probeWindowPosition(p.origin,i),k));const auto& e=p.atlas[NorthlightGI::probeAtlasIndex(k)];assert(e.occupied&&e.key==k);}
  ahead.next({4,5,4});ahead.request.probeCenter={22,5,4};const unsigned before=solveCalls;ahead.run();assert(solveCalls==before&&ahead.published->reusedProbes==W);}
 // 0.3.153 GIDistance=68: the same production tail solves an 18x18x14 window in a 20^3 atlas.
 {assert(NorthlightGI::configureProbeLayout(NorthlightGI::probeLayoutFor(18)));const unsigned W18=NorthlightGI::probeLayout().count();assert(W18==4536);
  reset();Fixture wide;wide.run();assert(!wide.published->partial&&wide.published->processedProbes==W18&&wide.published->solvedProbes==W18&&solveCalls==W18);
  assert(wide.published->atlas.size()==8000&&occupied(wide.published->atlas)==W18&&wide.probeCache.capacity()==8000);
  auto cameraMove=[&]{wide.next({8,0,0});};cameraMove();const unsigned before=solveCalls;wide.run();assert(!wide.published->partial&&solveCalls-before==18*14);
  assert(NorthlightGI::configureProbeLayout(NorthlightGI::ProbeLayout{}));}
 afterSolve={};onTick={};std::puts("PASS extracted production worker: first8 before2744 completion; bounded cadence;8 camera retargets; old display coverage retained across lighting reset; no cross-map/light/distant publication; last-static and mid-dynamic cancellation; completion flags; shutdown; immutable exports; build handoff stops the old pass; concurrent solves counted; GIProbeAhead window; GIDistance=68 layout");
}
'''.replace('SNAPSHOT',snapshot).replace('BODY',body)
report={'status':'pass','game_launched':False,'render_device_created':False,'scope':'actual production solve/publication tail; deterministic request/clock fixture; geometry and actor loaders not executed','runs':[],'sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in ['world_renderer.h','world_probe_progress.h','world_dynamic_probes.h','world_probe_cache.h','world_gi.h','world_gi.cpp','test_progressive_gi_worker.py','test_world_probe_progress.cpp']}}
with tempfile.TemporaryDirectory(prefix='northlight-progressive-gi-') as tmp:
 path=Path(tmp);(path/'worker.cpp').write_text(fixture)
 for target in ['worker','helpers']:
  cpp=path/'worker.cpp' if target=='worker' else HERE/'test_world_probe_progress.cpp'
  for mode,flags in [('O2',['-O2']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
   command=['clang++','-std=c++17',*flags,*fp.test_include_flags(),str(cpp),str(fp.src('world_gi.cpp')),'-o',str(path/'test')]
   subprocess.run(command,check=True)
   result=subprocess.run([str(path/'test')],check=True,capture_output=True,text=True,cwd=tmp)
   print(target,mode,result.stdout.strip());report['runs'].append({'target':target,'mode':mode,'stdout':result.stdout,'command':command})
(fp.output_dir()/'progressive-gi-validation.json').write_text(json.dumps(report,indent=2)+'\n')
