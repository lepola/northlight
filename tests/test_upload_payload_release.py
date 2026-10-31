#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.214 (task 42): the heavy CPU upload payload (paged vertex/index copy, cutout textures) is owned
by UploadPayload, held by the plan through UploadPayloadHandle, and discarded by the render thread right
after the mesh commit. Native test of the production handle (seal() moves pages/materials in, cpuBytes
counts the payload only while it exists, copies share it, discard frees it once the last reference
drops, a worker thread reading cpuBytes()/live() while the render thread discards -- also under TSan);
plus a source audit of the render-thread wiring (release after commit, rebuild request instead of
re-upload when the payload is gone, no per-frame spin, reaper-side free)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

cpp=r'''
#include "world_mesh_pages.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
using namespace NorthlightGI;
namespace P=NorthlightWorldMeshPages;
namespace M=NorthlightWorldMesh;
static M::WorldMeshUploadPlan sealed(WorldScene& source,std::shared_ptr<const P::Plan>& pagesOut,std::weak_ptr<M::UploadPayload>* weak=nullptr){
    for(uint32_t i=0;i<3;++i){WorldVertex v{};v.position=Vec3(float(i),0,0);source.vertices.push_back(v);}
    M::WorldMeshUploadPlan input;input.source=&source;input.vertexCount=3;input.triangleCount=1;input.materials.resize(1);
    input.materials[0].bgra={255,255,255,255};input.indices={0,1,2};
    M::Batch b;b.start=0;b.count=1;b.material=0;input.batches.push_back(b);
    input.vertexBytes=96;input.indexBytes=12;
    source.triangles.resize(1);source.materials.resize(1);
    auto pages=std::make_shared<P::Plan>();std::string error;
    assert(P::build(input,*pages,error));pagesOut=pages;
    assert(P::seal(input,pages,error));assert(!P::validateSealed(input));
    if(weak)*weak=input.payload.share();
    return input;
}
int main(){
    WorldScene source;std::shared_ptr<const P::Plan> pages;
    std::weak_ptr<M::UploadPayload> weak;
    auto plan=sealed(source,pages,&weak);
    const uint64_t withPayload=plan.cpuBytes();
    assert(plan.payload.live()&&plan.payload.bytes()>=pages->cpuBytes()&&plan.materials.capacity()==0&&plan.pagedCpuBytes==pages->cpuBytes());
    auto copy=plan;                                   // a plan copy shares the immutable payload
    assert(copy.payload.share()==plan.payload.share()&&copy.cpuBytes()==withPayload);
    auto held=plan.payload.share();                   // the pending packet's reference
    const uint64_t released=plan.payload.discard();
    assert(released==held->bytes&&!plan.payload.live()&&plan.payload.bytes()==0);
    assert(plan.cpuBytes()+released==withPayload);    // accounting drops exactly the payload
    assert(P::validateSealed(plan)&&std::string(P::validateSealed(plan))=="paged payload released");
    assert(!weak.expired());                          // the copy and the pending reference still hold it
    copy.payload.discard();assert(!weak.expired());
    held.reset();assert(weak.expired());              // last reference frees it
    assert(plan.payload.discard()==0&&!plan.payload.take());
    // Moving a plan moves the payload; the source is empty and consistent.
    WorldScene source2;std::shared_ptr<const P::Plan> pages2;auto a=sealed(source2,pages2);
    M::WorldMeshUploadPlan moved=std::move(a);assert(moved.payload.live()&&!a.payload.live()&&!a.payload.share());
    // A worker thread keeps asking for the retirement size while the render thread discards.
    for(int round=0;round<200;++round){
        WorldScene s;std::shared_ptr<const P::Plan> p;auto shared=std::make_shared<M::WorldMeshUploadPlan>(sealed(s,p));
        std::atomic<bool> go{false},stop{false};uint64_t last=0;
        std::thread worker([&]{while(!go){}uint64_t seen=UINT64_MAX;while(!stop){const uint64_t b=shared->cpuBytes();bool live=shared->payload.live();(void)live;assert(b<=seen||seen==UINT64_MAX||b<seen+1);seen=b;last=b;}});
        go=true;auto payload=shared->payload.share();assert(payload);shared->payload.discard();payload.reset();
        stop=true;worker.join();(void)last;
        assert(shared->cpuBytes()<UINT64_MAX&&!shared->payload.live());
    }
    std::puts("PASS upload payload: seal moves pages/materials, accounting, shared copies, last-reference free, concurrent discard");
}
'''
w=fp.src('world_renderer.h').read_text()
def method(signature,text=w):
    start=text.index(signature);end=text.index('\n    }\n',start);return text[start:end]
upload=method('    bool upload(NorthlightStreaming::Budget& streamBudget){');retire=method('    void retirePendingCpu(){');rebuild=method('    void requestPayloadRebuild(){');orphan=method('    void releaseOrphanedPending(const char* site){')
builder=w[w.index('    void work() {'):w.index('\n    bool check(HRESULT h,const char* s)')]
plan=fp.src('world_mesh_plan.h').read_text();pagesH=fp.src('world_mesh_pages.h').read_text()
commit=upload[upload.index('auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Commit);'):]
release=w[w.index('    void releaseGPU(bool keepMesh=false){'):w.index('\n',w.index('    void releaseGPU(bool keepMesh=false){'))]
resetFn=method('    void reset(){');recoverFn=method('    void recover(){');resources=method('    bool createResources(UINT w,UINT h,D3DFORMAT fmt){')
checks={
 'Reset (failed or not) and resolution change keep the uploaded MANAGED mesh':
  resetFn.index('failed=false;') < resetFn.index('releaseGPU(true);') and 'releaseGPU(true);width=w;height=h;' in resources,
 'recover() keeps the MANAGED mesh; only the destructor releases it':'releaseGPU(true);' in recoverFn and w.index('worker.join();',w.index('~WorldRenderer()')) < w.index('releaseGPU();',w.index('~WorldRenderer()')),
 'releaseGPU(keepMesh) guards exactly the mesh state: pool, pending packet, clearMesh, map, fixed set + bits, covered owners':
  'if(!keepMesh){pendingMesh.reset();clearMesh();}' in release and 'if(!keepMesh){releaseMeshPool();uploadedMap.clear();}' in release
  and 'if(!keepMesh){fixedTerrain.reset();fixedTerrainBits.reset();}' in release and 'if(!keepMesh)uploadedStaticOwners.reset();' in release
  and 'staticOwnerGeneration=UINT64_MAX;staticCasters.reset();' in release,
 'DEFAULT-pool state is still released on a keepMesh Reset (targets, live terrain, index ring, queries, caches)':
  all(x in release for x in ('liveTerrainGPU.clear();','drop(liveIndicesGPU);','queries.drop();','stateBlocks.clear();','uploadedTerrain.clear();','drop(color);','invalidateShadowCache();','retiredMaterials.clear();')),
 'normal pages are MANAGED; pressure DEFAULT pages retain replay payload and release before Reset':
  'D3DPOOL pool=D3DPOOL_MANAGED' in w and 'payload.pages->temporary?D3DPOOL_DEFAULT:D3DPOOL_MANAGED' in upload
  and w.count('D3DPOOL_MANAGED,&next.partialTexture')==1
  and resetFn.index('releaseDefaultMesh();')<resetFn.index('releaseGPU(true);')
  and 'payload.pages->temporary?0:plan.payload.discard()' in commit,
 'the rebuild request has one caller: the upload() missing-payload fallback':w.count('requestPayloadRebuild()')==2 and upload.count('requestPayloadRebuild()')==1,
 'the plan holds the payload through the handle; no pages/materials read after commit':
  'UploadPayloadHandle payload;' in plan and 'std::shared_ptr<const NorthlightWorldMeshPages::Plan> pages;\n    // Sealed' not in plan
  and 'plan.pages->' not in w and 'plan->pages->' not in w and 'plan.materials' not in w and 'plan->materials' not in w,
 'cpuBytes counts the payload through one atomic, never its vectors':'uint64_t bytes=payload.bytes()+sizeof(*this)' in plan and 'std::atomic_load' not in plan[plan.index('uint64_t cpuBytes()'):],
 'seal moves pages and materials into the payload before clearing the source':'payload->materials.swap(input.materials)' in pagesH and 'input.payload.set(std::move(payload))' in pagesH,
 'upload() shares the payload once, asks for a rebuild when it is gone':
  'auto payload=plan.payload.share();' in upload and 'if(!payload){requestPayloadRebuild();return uploadedMap==active->map&&vertices&&indices;}' in upload
  and upload.index('requestPayloadRebuild()')<upload.index('validateSealed(plan)') and 'pendingMesh->payload=payload;' in upload,
 'staging reads only the pending packet payload':'const auto& payload=*next.payload;' in upload,
 'released right after the commit, before the pending packet is retired':
  commit.count('plan.payload.discard()')==1 and commit.index('plan.payload.discard()')<commit.index('retirePendingCpu();pendingMesh.reset();meshRetry.clear();')
  and commit.index('plan.payload.discard()')>commit.index('uploaded=next.bvh;') and 'releasedPayloadMiB=%.2f' in commit,
 'the last reference is freed on the reaper; a retried plan only drops its reference':
  'if(pending.payload){if(pending.plan&&!pending.plan->payload.live())retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.payload,size_t(pending.payload->bytes));else pending.payload.reset();}' in retire,
 'a superseded pending plan is released with its packet':'pendingMesh->plan!=active->meshPlan)pendingMesh->plan->payload.discard();' in orphan and orphan.index('payload.discard()')<orphan.index('retirePendingCpu();'),
 'rebuild is requested once per bvh (15 s re-ask), logged, and consumed by the worker under the existing lock':
  'DWORD(now-payloadRebuildTick)<15000u' in rebuild and 'deferLogf("WORLD mesh upload payload was released' in rebuild and 'geometryRebuild=true;pending=true;' in rebuild
  and 'if(geometryRebuild){geometryRebuild=false;geometryWanted=true;}' in builder,
}
bad=[k for k,v in checks.items() if not v]
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
if bad:sys.exit(1)
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp);(d/'test.cpp').write_text(cpp)
    for label,flags in (('O1 ASan/UBSan',['-O1','-g','-fsanitize=address,undefined']),('O1 TSan',['-O1','-g','-fsanitize=thread'])):
        exe=d/'test'
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,*fp.test_include_flags(),str(d/'test.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)],check=True)
        out=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300);print(label+': '+out.stdout+out.stderr,end='');out.check_returncode()
