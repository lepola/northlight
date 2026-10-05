#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the production geometry handoff/error publication against camera retargets."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib, json, subprocess, tempfile, os
from pathlib import Path
HERE = Path(__file__).resolve().parent
source = fp.src('world_renderer.h').read_text()
handoff = source.split('// BEGIN GEOMETRY HANDOFF:', 1)[1]
handoff = handoff[handoff.index('{std::lock_guard'):].split('// END GEOMETRY HANDOFF', 1)[0]
snapshot = source[source.index('    struct Snapshot {'):source.index('\n    struct Request ')]
error = source[source.index('            auto publishError='):source.index('            // Geometry validity is spatial')]
fixture = r'''
#include "world_streaming.h"
#include "world_probe_cache.h"
#include "diagnostics_switch.h"
#include <cassert>
#include <limits>
#include <mutex>
#include <cstdio>
using V=NorthlightGI::Vec3;using DWORD=uint32_t;
namespace NorthlightRegionalFog {struct Field {};}
namespace NorthlightLocalLights {struct Light {};}
template<class... T> void logf(const char*,T...){}
struct Fixture {
 struct PreparedCommit {int tag=0;};
SNAPSHOT
 struct Request {std::string map="Azeroth";V camera,geometryCenter;uint64_t id=1;DWORD queuedAt=1;int actorJob=1;};
 Request request,original,consumed;
 std::mutex mutex;bool stopping=false,pending=false;
 unsigned superseded=0,retargeted=0,enteredGI=0;
 unsigned concurrentSolves=0;DWORD stallMs=0;uint64_t sceneBuild=0; /* 0.3.153 swap diagnostics, reset at publication */
 std::string sceneMap="Azeroth";V sceneCenter;
 std::shared_ptr<NorthlightGI::BVH> bvh=std::make_shared<NorthlightGI::BVH>();
 std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> scenePlan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
 std::shared_ptr<const NorthlightRegionalFog::Field> sceneFog=std::make_shared<NorthlightRegionalFog::Field>();
 std::shared_ptr<const PreparedCommit> scenePrepared=std::make_shared<PreparedCommit>();
 std::vector<NorthlightLocalLights::Light> rawSceneLights;
 std::shared_ptr<Snapshot> published,previousLighting,result=std::make_shared<Snapshot>();
 void run(){for(unsigned iteration=0;iteration<1;++iteration){Request r=original;
HANDOFF
   ++enteredGI;consumed=r;
 }}
 bool failure(){Request r=request;auto stallEnd=[](const char*){}; /* 0.3.190 memory stall episode log: not under test here */
ERROR
  return publishError();
 }
};
int main(){
 using namespace NorthlightWorldStreaming;
 assert(!needsGeometry("A",{},"A",{32,0,0}));
 assert(needsGeometry("A",{},"A",{32.01f,0,0}));
 assert(needsGeometry("A",{},"B",{}));
 assert(applicable("A",{},"A",{96,0,0}));
 assert(!applicable("A",{},"A",{96.01f,0,0}));
 assert(!applicable("A",{},"B",{}));
 assert(!within({}, {std::numeric_limits<float>::quiet_NaN(),0,0},96));
 // The build was requested at zero. Camera moved before it completed.
 for(float distance:{0.f,32.f,64.f,64.01f,96.f}){
  Fixture f;f.request.id=9;f.request.queuedAt=200;f.request.camera={distance,0,0};f.request.actorJob=19;f.pending=true;
  auto old=std::make_shared<Fixture::Snapshot>();old->map="Azeroth";old->serial=17;old->atlas.resize(2);old->bvh=std::make_shared<NorthlightGI::BVH>();
  f.request.geometryCenter=f.request.camera;f.concurrentSolves=5;f.stallMs=40;f.published=old;f.run();
  assert(f.published!=old&&f.published->bvh==f.bvh&&f.published->meshPlan==f.scenePlan);
  assert(!f.concurrentSolves&&!f.stallMs);
  assert(f.published->center.x==0&&f.published->map=="Azeroth"&&f.published->serial==17&&f.published->atlas.size()==2);
  assert(old->bvh!=f.bvh&&old->center.x==0&&old->serial==17); // immutable fallback
  assert(f.published->fogField==f.sceneFog&&f.published->prepared==f.scenePrepared); // paired with meshPlan
  if(distance<=64){assert(f.enteredGI==1&&f.retargeted==1&&!f.pending&&f.consumed.actorJob==19&&f.consumed.id==9);
   assert(f.result->center.x==0&&f.result->requestedAt==200&&f.result->requestId==9);}
  else assert(!f.enteredGI&&f.pending&&f.retargeted==0); // geometry useful, GI needs a newer build
 }
 for(unsigned kind=0;kind<4;++kind){
  Fixture f;auto old=std::make_shared<Fixture::Snapshot>();f.published=old;
  if(kind==0)f.request.camera={96.01f,0,0};
  if(kind==1)f.request.map="Kalimdor";
  if(kind==2)f.request.camera={std::numeric_limits<float>::infinity(),0,0};
  if(kind==3)f.stopping=true;
  f.run();assert(f.published==old&&!f.enteredGI);if(kind<3)assert(f.pending&&f.superseded==1);
 }
 // 0.3.169 lead: a region built ahead that the eye has not reached (>64, lead point within the
 // 32-unit refresh) is published and waits for the next request: no pending re-run (no spin).
 for(float lead:{0.f,10.f,40.f}){Fixture f;f.request.id=9;f.request.camera={-70,0,0};f.request.geometryCenter={-70+lead,0,0};f.pending=true;f.run();
  assert(f.published&&f.published->bvh==f.bvh&&!f.enteredGI);
  assert(f.pending==(lead<38)); /* lead point 70-lead from the centre: >32 wants a new region */}
 Fixture cross;cross.published=std::make_shared<Fixture::Snapshot>();cross.published->map="Kalimdor";cross.published->atlas.resize(4);cross.published->serial=99;
 cross.run();assert(cross.published->atlas.empty()&&cross.published->serial==0&&cross.enteredGI==1);
 Fixture previous;previous.previousLighting=std::make_shared<Fixture::Snapshot>();previous.previousLighting->map="Azeroth";previous.previousLighting->serial=5;
 previous.run();assert(previous.published->serial==5&&!previous.previousLighting);
 Fixture failed;failed.published=std::make_shared<Fixture::Snapshot>();failed.published->map="Azeroth";failed.published->bvh=failed.bvh;
 auto kept=failed.published;assert(!failed.failure()&&failed.published==kept);
 failed.request.camera={97,0,0};assert(!failed.failure()&&failed.published==kept); /* 0.3.169: retained (160), not applicable (96) */
 failed.request.camera={160.01f,0,0};assert(failed.failure()&&failed.published==failed.result);
 std::puts("PASS actual geometry handoff: publish before GI; latest camera/actor request; fixed geometry center; 32/64/96 boundaries; same-map immutable GI fallback; cross-map/distant/nonfinite/stop rejection; failed replacement retains usable geometry up to the 160 hold limit; lead region waits without re-running; swap diagnostics reset at publication");
}
'''.replace('SNAPSHOT', snapshot).replace('HANDOFF', handoff).replace('ERROR', error)
report = {'game_launched': False, 'production_sha256': hashlib.sha256(source.encode()).hexdigest(), 'runs': []}
with tempfile.TemporaryDirectory(prefix='geometry-handoff-') as temp:
    root = Path(temp); (root / 'test.cpp').write_text(fixture)
    for mode, flags in [('O2', ['-O2']), ('asan-ubsan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
        subprocess.run(['clang++', '-std=c++17', *flags, *fp.test_include_flags(), str(root / 'test.cpp'), str(fp.src('world_gi.cpp')), str(fp.src('world_mesh_plan.cpp')), '-o', str(root / 'test')], check=True)
        output = subprocess.check_output([str(root / 'test')], text=True)
        print(mode, output.strip()); report['runs'].append({'mode': mode, 'stdout': output})
(fp.output_dir() / 'geometry-handoff.json').write_text(json.dumps(report, indent=2) + '\n')
