#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Run the production expiry and end-frame release blocks against counted owners.

The Dalaran log kept one expired published BVH and the active managed page table
while admission needed 768 MiB and trimIdle freed zero. Model the same ownership
graph and reserve; no game, GPU, real address-space allocation or lower limits.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import json
import subprocess
import tempfile

source = fp.src('world_renderer.h').read_text()
expiry = source.split('// BEGIN EXPIRED PUBLICATION\n', 1)[1].split('// END EXPIRED PUBLICATION', 1)[0]
start = source.index('    void releaseExpiredMesh(){')
release = source[start:source.index('\n    }', start) + 6]
assert 'retirementBacklog.retireOrFree(reaper,expiredPublication,snapshotRetireBytes(*expiredPublication));' in source
assert 'expiredMeshMapChanged|=heldDrawable&&active->map!=r.map;' in source and 'active.reset();expiredMeshPending|=heldDrawable;' in source
end_frame = source[source.index('    void endFrame(bool retainPool=true,unsigned rendererFrame=0){'):source.index('    bool ready()const')]
assert end_frame.index('replays.clear();') < end_frame.index('releaseExpiredMesh();')
assert 'if(workerMemoryTrim.exchange(false,std::memory_order_relaxed))resetPreparation();' in source
reset_preparation = source.split('auto resetPreparation=[&]{', 1)[1].split('};', 1)[0]
assert all(owner + '.reset();' in reset_preparation for owner in ('meshPreparation', 'sceneSources', 'localGeometry'))
assert 'entryLift' not in reset_preparation  # a trim keeps the entry lift

fixture = r'''
#include "world_streaming.h"
#include "geometry_memory.h"
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
using NorthlightGI::Vec3;
using namespace NorthlightGeometryMemory;
static uint64_t resident=0;
struct Counted {uint64_t bytes;explicit Counted(uint64_t n):bytes(n){resident+=n;}~Counted(){resident-=bytes;}};
struct Snapshot {std::shared_ptr<Counted> bvh,plan;std::string map="Northrend";Vec3 center;};
struct Request {std::string map="Northrend";Vec3 camera;};
template<class... T> void logf(const char*,T...){}
struct MeshStore {uint64_t bytes=0;uint64_t poolBytes()const{return bytes;}};
struct Harness {
 std::shared_ptr<Snapshot> published,active,expiredPublication;Request r;
 MeshStore meshStore;uint64_t uploadedTextureBytes=0,staticOwnerGeneration=0;
 std::shared_ptr<int> uploadedStaticOwners,fixedTerrain,fixedTerrainBits;
 std::string uploadedMap="Northrend",uploadedTerrain="resident";
 unsigned liveTerrainIndexCount=7,liveDirectionalIndexCount=8,liveTerrainGeneration=9;
 bool expiredMeshPending=false,expiredMeshMapChanged=false,memoryPressure=false,temporalValid=true,fogHistoryValid=true;
 std::atomic<bool> workerMemoryTrim{false};std::atomic<uint64_t> geometryMemoryDemand{0};
 unsigned meshClears=0,poolReleases=0,invalidates=0;
 void clearMesh(){++meshClears;resident-=uploadedTextureBytes;uploadedTextureBytes=0;}
 void releaseMeshPool(){++poolReleases;resident-=meshStore.bytes;meshStore.bytes=0;}
 void invalidateShadowCache(){++invalidates;}
 void expire(){ EXPIRY }
 RELEASE
 void install(){published=active=std::make_shared<Snapshot>();
  active->bvh=std::make_shared<Counted>(142*MiB);active->plan=std::make_shared<Counted>(10*MiB);
  meshStore.bytes=67*MiB;uploadedTextureBytes=9*MiB;resident+=76*MiB;}
 void range(){expire();if(active&&!NorthlightWorldStreaming::retained(active->map,active->center,r.map,r.camera)){
  expiredMeshMapChanged|=bool(active->bvh)&&active->map!=r.map;expiredMeshPending|=bool(active->bvh);active.reset();}}
 void endFrame(){expiredPublication.reset();releaseExpiredMesh();}
 ~Harness(){expiredPublication.reset();published.reset();active.reset();resident-=meshStore.bytes+uploadedTextureBytes;}
};
int main(){
 // Preserve both ordinary coverage and the full 96..160-unit hold.
 for(float distance:{0.f,96.f,120.f,160.f}){Harness h;h.install();auto old=h.active;h.r.camera=Vec3(distance,0,0);
  h.range();h.endFrame();assert(h.active==old&&h.published==old&&!h.expiredPublication);
  assert(h.poolReleases==0&&h.temporalValid&&resident==228*MiB);}
 assert(resident==0);
 // Actual log failure: publication is a second owner. Cleanup before endFrame
 // must not free pages still used by replay; after frame retirement it recovers
 // enough modeled VA to meet the *unchanged* 768 MiB aggregate reserve.
 {Harness h;h.install();std::weak_ptr<Counted> bvh=h.active->bvh,plan=h.active->plan;
  h.r.camera=Vec3(160.01f,0,0);h.geometryMemoryDemand=768*MiB;const uint64_t totalFreeAndReclaimable=615*MiB+resident;
  assert(!admits({totalFreeAndReclaimable-resident,115*MiB,true},buildBudget(64*MiB)));
  h.range();assert(!h.active&&!h.published&&h.expiredPublication&&!bvh.expired());
  assert(h.meshStore.bytes==67*MiB&&h.poolReleases==0);h.endFrame();
  assert(bvh.expired()&&plan.expired()&&resident==0&&h.poolReleases==1&&h.meshClears==1);
  assert(admits({totalFreeAndReclaimable-resident,115*MiB,true},buildBudget(64*MiB)));
  assert(h.uploadedMap.empty()&&h.uploadedTerrain.empty()&&h.staticOwnerGeneration==UINT64_MAX);
  assert(h.liveTerrainIndexCount==0&&h.liveDirectionalIndexCount==0&&h.liveTerrainGeneration==0);
  assert(h.invalidates==1&&!h.temporalValid&&!h.fogHistoryValid&&h.workerMemoryTrim.load());h.endFrame();assert(h.poolReleases==1);}
 assert(resident==0);
 // Preserve legitimate worker ownership; expiry drops no reference behind its
 // back and the weak generation expires only after the worker is finished.
 {Harness h;h.install();auto worker=h.active->bvh;std::weak_ptr<Counted> observed=worker;
  h.r.camera=Vec3(200,0,0);h.memoryPressure=true;h.range();h.endFrame();assert(!observed.expired()&&resident==142*MiB);
  worker.reset();assert(observed.expired()&&resident==0);}
 // A same-frame replacement cancels GPU cleanup; it must never lose its table.
 {Harness h;h.install();h.r.camera=Vec3(200,0,0);h.range();
  h.active=std::make_shared<Snapshot>();h.active->center=h.r.camera;h.active->bvh=std::make_shared<Counted>(1*MiB);
  h.endFrame();assert(h.poolReleases==0&&!h.expiredMeshPending&&h.meshStore.bytes==67*MiB);}
 assert(resident==0);
 // An error snapshot adopted and retired in another context callback must not
 // cancel a pending release of the previous drawable generation.
 {Harness h;h.install();h.r.camera=Vec3(200,0,0);h.geometryMemoryDemand=768*MiB;h.range();
  h.active=std::make_shared<Snapshot>();h.range();h.endFrame();assert(h.poolReleases==1&&resident==0);}
 // Same map without demand or pressure: the pages stay for the next build's elision (residentPages); the release
 // waits, and a demand or pressure that comes later still frees them at the next frame end.
 {Harness h;h.install();h.r.camera=Vec3(200,0,0);h.range();h.endFrame();
  assert(!h.active&&h.poolReleases==0&&h.meshClears==0&&h.meshStore.bytes==67*MiB&&h.expiredMeshPending&&!h.workerMemoryTrim.load());
  h.endFrame();assert(h.poolReleases==0);h.memoryPressure=true;h.endFrame();
  assert(h.poolReleases==1&&h.meshStore.bytes==0&&!h.expiredMeshPending&&h.workerMemoryTrim.load());}
 assert(resident==0);
 {Harness h;h.install();h.r.map="Azeroth";h.range();h.endFrame();assert(!h.active&&!h.published&&resident==0);}
 // Keep the memoized error publication on uncached maps, so no error/build loop.
 {Harness h;h.published=std::make_shared<Snapshot>();h.r.camera=Vec3(1000,0,0);h.expire();assert(h.published&&!h.expiredPublication);}
 // An abandoned region's peak estimate is cleared (abandonGeometry; a trim keeps it), while
 // both entry/interior aggregate and contiguous floors stay unchanged.
 {EntryLift lift;lift.refused(853*MiB,640*MiB,636*MiB,1000,53*MiB);
  assert(lift.apply(buildBudget(64*MiB),1001).available==857*MiB);lift.clear();
  assert(lift.apply(buildBudget(64*MiB),1002).available==768*MiB);
  assert(!admits({767*MiB,115*MiB,true},lift.apply(buildBudget(64*MiB),1002)));
  assert(!admits({639*MiB,115*MiB,true},inBuildBudget()));}
 std::puts("PASS expired geometry: hold bounds, publication owners, end-frame pages under demand/pressure, same-map pages kept for elision, same-frame replacement, map change, worker holder, unchanged memory reserves");
}
'''.replace('EXPIRY', expiry).replace('RELEASE', release)

runs = []
with tempfile.TemporaryDirectory(prefix='northlight-expired-world-') as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(fixture)
    for mode, flags in [('optimized', ['-O2']), ('debug', ['-O0', '-g'])]:
        binary = root / mode
        subprocess.run(['clang++', '-std=c++17', '-UNDEBUG', *flags, *fp.test_include_flags(), str(root / 'test.cpp'), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
        print(mode, result.stdout.strip())
        runs.append({'mode': mode, 'stdout': result.stdout})
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
(out / 'expired-world-geometry.json').write_text(json.dumps({'status': 'pass', 'game_launched': False, 'runs': runs}, indent=2) + '\n')
