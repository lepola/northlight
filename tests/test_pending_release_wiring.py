#!/usr/bin/env python3
# northlight-test:
"""0.3.156 orphaned staged-upload release and generation-stall watchdog: source wiring.
A staged upload (pendingMesh) whose snapshot is no longer active never commits but keeps
a geometry generation alive; with an out-of-range published build that filled
Generations<BVH,2> and the builder deferred for ever (0.3.155 game log). The release rule
must run where upload() cannot: before render()'s ready() gate and at the out-of-range
active.reset() site, both on the D3D thread. The watchdog only logs. Writes nothing.
The behaviour itself runs in test_concurrent_geometry_build.py case (d).
0.3.169 coverage hold: the out-of-range site retires on the hard limit (retained, 160) or a map
change, never on the 96-unit applicable(); adoption uses the same limit (adopts) so a held
snapshot and an unadoptable build cannot fill both generations, and the builder's error
publication keeps a retained region. Hold/retire and skip-episode diagnostics are wired."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
from pathlib import Path
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();r=fp.src('renderer.cpp').read_text();g=fp.src('geometry_memory.h').read_text()
t=(HERE/'test_concurrent_geometry_build.py').read_text()
def method(signature,text=w):
    start=text.index(signature);end=text.index('\n    }\n',start);return text[start:end]
render=method('    bool render(IDirect3DSurface9* targetSurface');upload=method('    bool upload(NorthlightStreaming::Budget& streamBudget){')
context=method('    void updateWorldContext(const char* map,const float* camera,const NorthlightWmoContext::Lighting* global=nullptr){');helper=method('    void releaseOrphanedPending(const char* site){')
gate='const bool screenResourcesReady=ready()&&resources(w,h,fmt);'
upload_gate='const bool geometryReady=sceneTargetsReady&&(hadTargets||upload(streamBudget));'
reset='active.reset();expiredMeshPending|=heldDrawable;}'
retire='const char* retired=active&&!NorthlightWorldStreaming::retained(active->map,active->center,r.map,r.camera)?(active->map!=r.map?"map":"retired"):nullptr;'
adopt='if(published&&published!=active&&NorthlightWorldStreaming::adopts(published->map,published->center,bool(published->bvh),active&&active->bvh,r.map,r.camera)){'
builder=w[w.index('    void work() {'):w.index('\n    bool check(HRESULT h,const char* s)')]
error=builder[builder.index('auto publishError='):builder.index('// Geometry validity is spatial')]
admission='if(!generations.canAdmit()){++generationDeferrals;'
# Each fault/reset cancellation retires CPU ownership before dropping its staged
# owner. A successful commit publishes the complete table before that retirement.
# Count-free checks permit additional legitimate lifecycle cancellation sites.
cancel_paths=(helper,method('    bool streamingFailure('),method('    void releaseDefaultMesh(){'))
retire_pending='retirePendingCpu();';clear_pending='pendingMesh.reset();'
retirement_order=all(body.index('meshStore.cancel();')<body.index(retire_pending)<body.index(clear_pending) for body in cancel_paths)
commit_order=upload.index('meshStore.commit()')<upload.index(retire_pending)<upload.index(clear_pending)

checks={
 'bounded replacement can retain a live geometry generation under the same hard limit':
  'if(!NorthlightGeometryMemory::admits(memory,entryBudget))' in builder and 'if(!generations.canAdmit())' in builder
  and 'coldRecovery?NorthlightGeometryMemory::coldInBuildBudget(bytes):NorthlightGeometryMemory::inBuildBudget(bytes)' in builder,
 'one release rule: the upload() predicate as a static helper, shared by the fixture':
  w.count('static bool pendingOrphaned(')==1 and 'return pending&&(!active||pending->map!=active->map||pending->bvh!=active->bvh);' in w
  and 'pendingMesh->map!=active->map||pendingMesh->bvh!=active->bvh' not in w and 'Fixture::pendingOrphaned(' in t and "static bool pendingOrphaned('" in t,
 'helper releases exactly as upload() did, with one log line per event':
  'if(!pendingOrphaned(pendingMesh.get(),active.get()))return;' in helper and 'retirePendingCpu();pendingMesh.reset();meshRetry.clear();' in helper
  and helper.count('logf("WORLD pending mesh released orphan=')==1,
 'upload() uses the helper before staging':upload.count('releaseOrphanedPending("upload");')==1 and upload.index('releaseOrphanedPending("upload");')<upload.index('if(uploaded.lock()!=active->bvh&&!pendingMesh'),
 'render(): release before the ready() and upload gates':render.count('releaseOrphanedPending("render");')==1 and render.count(gate)==1 and render.count(upload_gate)==1 and render.index('releaseOrphanedPending("render");')<render.index(gate)<render.index(upload_gate),
 'updateWorldContext(): release right after the out-of-range active.reset()':
  context.count(reset)==1 and context.count('releaseOrphanedPending("range");')==1 and context.index(reset)<context.index('releaseOrphanedPending("range");')<context.index('logf("WORLD camera tick='), # 0.3.200 added an earlier GetTickCount() local; anchor on the camera line
 '0.3.169 retire on the hard limit or a map change, then the release rule (A2, A5)':
  context.count(retire)==1 and context.index(retire)<context.index(reset)<context.index('releaseOrphanedPending("range");')
  and 'if(retired){expiredMeshMapChanged|=heldDrawable&&active->map!=r.map;retirementBacklog.retireOrFree(reaper,active,snapshotRetireBytes(*active));active.reset();expiredMeshPending|=heldDrawable;}' in context
  and 'NorthlightWorldStreaming::applicable(active->map,active->center,r.map,r.camera)){retirementBacklog' not in context,
 '0.3.169 adoption uses the same hard limit and never takes an error snapshot over a drawable one (A1, A3)':
  context.count(adopt)==1 and context.index(adopt)<context.index(retire)
  and 'inline bool adopts(' in fp.src('world_streaming.h').read_text() and 'return (drawable||!activeDrawable)&&retained(map,center,currentMap,camera);' in fp.src('world_streaming.h').read_text(),
 '0.3.169 builder error publication keeps a retained region (A3)':
  'if(published&&published->bvh&&NorthlightWorldStreaming::retained(published->map,published->center,request.map,request.camera)){' in error,
 '0.3.169 handoff and builder coverage checks stay at 96 (A6)':
  'return !stopping&&NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);' in builder
  and 'if(!NorthlightWorldStreaming::applicable(sceneMap,sceneCenter,request.map,request.camera)){' in builder and builder.count('retained(')==1,
 '0.3.169 D1/D2: hold begin/end once per episode, independent of pendingMesh; coverMax on the camera line':
  context.count('logf("WORLD coverage hold begin dist=')==1 and context.count('logf("WORLD coverage hold end ms=')==1
  and 'coverMax=%.1f lead=%.1f' in context and context.count('coverMax=0;')==1 and 'if(distance>coverMax)coverMax=distance;' in context,
 '0.3.169 D3: a reason for every false render() and one skip-episode line per run':
  render.count('skipReason=')>=5 and 'skipReason="fault";if(workerFault())return false;' in render
  and r.count('logf("WORLD skip episode reason=%s last=%s frames=%u ms=%lu"')==1 and 'world->lastSkipReason()' in r,
 '0.3.169 lead: reason 128 only while a lead exists (M2); worker waiting liveness asserted (S1)':
  context.count('(NorthlightWorldStreaming::leadMoved(lastRequest.camera,lastRequest.geometryCenter,r.camera,r.geometryCenter)?128u:0u)')==1
  and 'GeometryLeadMoveStep)?128u' not in context
  and builder.count('static_assert(64-NorthlightWorldStreaming::GeometryRefreshDistance>NorthlightWorldStreaming::GeometryLeadMoveStep,')==1,
 'staged ownership retires after cancellation or complete publication':retirement_order and commit_order,
 'updateWorldContext runs only from terrain, WMO, model and validated sky context readers (D3D thread, like render())':
  w.count('updateWorldContext(map,')==4 and 'traceContext=5' in method('    bool skyContext(') and 'updateWorldContext(map,camera.camera,globalRead?&light:nullptr);' in method('    bool wmoContext(IDirect3DVertexShader9* shader){') and 'updateWorldContext(map,camera,globalRead?&global:nullptr);' in method('    void terrainContext(){')
  and 'world->wmoContext(vs)' in r and 'world->terrainContext();' in r,
 'builder marks each generation deferral and clears on admission, under the mutex':
  w.count(admission)==1 and re.search(r'if\(!generations\.canAdmit\(\)\)\{\+\+generationDeferrals;\{std::lock_guard<std::mutex> lock\(mutex\);generationStall\.deferred\(GetTickCount\(\)\);\}deferBuild\("generation-deferred",memory,100\);continue;\}\n\s*\{std::lock_guard<std::mutex> lock\(mutex\);generationStall\.admitted\(\);\}',w) is not None,
 'fixture region mirrors the production admission marks':'generationStall.deferred(GetTickCount());}deferBuild("generation-deferred"' in t and 'generationStall.admitted();}' in t,
 'watchdog: render thread, inside the request mutex block, once per episode, log only':
  context.count('generationStall.due(GetTickCount(),GenerationStallLogMs)')==1 and context.count('logf("WORLD geometry stalled ')==1
  and context.index('{std::lock_guard<std::mutex> lock(mutex);')<context.index('generationStall.due(')<context.index('{auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Retire);')
  and re.search(r'\bpublished(=[^=]|\.reset\(|\s*=\s*nullptr)',re.sub(r'"[^"]*"','""',context[context.index('generationStall.due('):context.index('{auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Retire);')])) is None
  and 'GenerationStallLogMs=10000' in w,
 'StallWatch is portable and episode-based':'struct StallWatch {' in g and 'void deferred(uint32_t now){if(!armed){armed=true;since=now;}}' in g and 'void admitted(){armed=false;reported=false;}' in g,
 'generation limit unchanged (F3)':'template<class T,size_t Limit=2> class Generations {' in g and 'NorthlightGeometryMemory::Generations<NorthlightGI::BVH> generations;' in w,
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
print('PASS pending release wiring: one rule before the ready() gate, at the out-of-range site and in upload(); log-only stall watchdog; 0.3.169 hold/adopt on the 160 hard limit with diagnostics')
