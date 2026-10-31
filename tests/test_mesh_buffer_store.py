#!/usr/bin/env python3
# northlight-test: requires=cxx
"""World mesh buffer store (mesh_buffer_store.h, 0.3.216 issue #66): content-key lookup over active and idle buffers,
claims that never touch the active table, atomic commit, cancel with invalidated keys, shared duplicate pages, idle-only
trim order, and a randomised invariant run. Native clang++ with ASan/UBSan. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-buffer-store-') as temp:
    binary = Path(temp) / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *fp.test_include_flags(), str(HERE / 'test_mesh_buffer_store.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()

# Source wiring (world_renderer.h): the store replaces meshPool[2]; an active buffer is never written; claims are cancelled on every
# path that drops a pending staging; only the missed pages are uploaded and admitted.
w = fp.src('world_renderer.h').read_text()
for gone in ('meshPool[', 'activeMesh', 'stagingSlot', 'vbCapacity=0,ibCapacity=0;};'):
    assert gone not in w, gone
assert '#include "mesh_buffer_store.h"' in w and 'NorthlightMeshBufferStore::Store& meshStore=gpuScene->meshStore;' in w
assert 'NorthlightMeshBufferStore::Store meshStore;' in fp.src('gpu_scene.h').read_text()
acquire = w[w.index('bool acquireMeshPage(uint32_t id'):w.index('void releaseMeshPool()')]
assert 'if(!meshStore.writable(id)||id>=meshBuffers.size())return false;' in acquire and acquire.index('writable(id)') < acquire.index('Create')
bind = w[w.index('bool bindMeshPage('):w.index('std::vector<Batch> batches;')]
assert 'meshStore.bufferOf(batch.page)' in bind and 'meshBuffers[id]' in bind
upload = w[w.index('bool upload(NorthlightStreaming::Budget& streamBudget){'):w.index('if(!vertices||!indices||uploadedMap!=active->map)return false;')]
perform = upload[upload.index('auto perform=['):upload.index('while(pageBudget.available()')]
assert 'if(!meshStore.markWriting(id)||id>=meshBuffers.size())return false;' in perform and perform.index('markWriting') < perform.index('payload.pages->copy(') < perform.index('Unlock')  # writable + key invalidated before the first write
assert 'staging.upload[cursor]' in perform and 'staging.uploadBuffer[cursor]' in perform  # the cursor runs over the miss list
assert upload.count('meshStore.beginStaging(pageKeys,') == 1 and upload.index('meshStore.beginStaging(pageKeys,') < upload.index('NorthlightGeometryMemory::admits(memory,budget)')
assert 'uploadBudget(missVertexBytes,missIndexBytes,plan.textureBytes,largestTexture)' in upload and 'for(uint32_t pageIndex:staged.upload)' in upload
assert 'meshStore.stagingGrowth(' in upload and 'vbPool==(payload->pages->temporary?D3DPOOL_DEFAULT:D3DPOOL_MANAGED)' in upload and 'ibPool==(payload->pages->temporary?D3DPOOL_DEFAULT:D3DPOOL_MANAGED)' in upload
assert 'missVertexBytes||missIndexBytes?NorthlightWorldMeshPages::PageVertexByteLimit:0' in upload
commit = upload[upload.index('Keep the previous complete geometry drawable'):]
assert commit.count('meshStore.commit()') == 1 and commit.index('retiredMaterials.enqueue(materials,uploadedTextureBytes)') < commit.index('meshStore.commit()') < commit.index('plan.payload.discard()')
assert 'meshStore.bufferOf(0)' in commit and commit.index('meshStore.commit()') > commit.index('clearMesh();')
for field in ('pages=%zu', 'reusedPages=%zu', 'uploadedPages=%zu', 'reusedMiB=%.2f', 'uploadedMiB=%.2f'):
    assert field in commit, field
assert 'stagingMs=%lu releasedPayloadMiB=%.2f pages=%zu' in commit  # existing fields keep their order, new ones append
# Every path that drops a pending staging cancels its claims (partial content: idle, key invalid).
orphan = w[w.index('void releaseOrphanedPending('):w.index('void requestPayloadRebuild()')]
failure = w[w.index('bool streamingFailure('):w.index('bool uploadRegionalFog(')]
for body in (orphan, failure):
    assert 'meshStore.cancel();' in body and body.index('meshStore.cancel();') < body.index('pendingMesh.reset();')
release = w[w.index('void releaseGPU(bool keepMesh=false){'):]
release = release[:release.index('\n')]
assert 'if(!keepMesh){pendingMesh.reset();clearMesh();}' in release and 'if(!keepMesh){releaseMeshPool();uploadedMap.clear();}' in release  # releaseMeshPool resets the store, claims included
assert 'meshStore.reset();' in w[w.index('void releaseMeshPool()'):w.index('size_t trimMeshPools(')]
assert 'meshIdleMiB=%.1f' in w and 'meshStore.poolBytes()' in w
# 0.3.216 page elision: keys published under the worker mutex per map, worker elides before seal, an elided miss falls back.
assert 'std::shared_ptr<const NorthlightMeshPageKey::KeySet> residentPages;std::string residentMap;' in w
publish = w[w.index('void publishResidentPages('):w.index('bool acquireMeshPage(')]
assert 'std::lock_guard<std::mutex> lock(mutex);residentPages=std::move(keys);' in publish and 'meshStore.activeKeys(' in publish and 'vbPool==D3DPOOL_MANAGED' in publish and 'ibPool==D3DPOOL_MANAGED' in publish and 'residentMap=residentPages?map:std::string()' in publish
assert 'publishResidentPages(std::string());' in w[w.index('void releaseMeshPool()'):w.index('size_t trimMeshPools(')]  # pool released: nothing resident
assert commit.count('publishResidentPages(uploadedMap);') == 1 and commit.index('meshStore.commit()') < commit.index('publishResidentPages(uploadedMap);')
worker = w[w.index('std::shared_ptr<const NorthlightMeshPageKey::KeySet> resident;'):w.index('WORLD mesh pages=%zu')]
assert 'residentPages&&residentMap==r.map' in worker and 'pagingOptions.resident=coldRecovery?nullptr:resident.get();' in worker  # same map only
assert 'NorthlightWorldMeshPages::build(*plan,*pages,error,terrainAdmission,pagingOptions)' in worker and 'elide(*pages' not in worker  # elided inside build(): the worker never holds resident pages
assert worker.index('pagingOptions') < worker.index('NorthlightWorldMeshPages::seal(*plan,pages')  # the payload accounts the freed memory
assert 'elidedPages=%zu elidedMiB=%.2f' in w and 'elidedPages=%zu"' in commit
begin = upload[upload.index('std::vector<uint8_t> elidedPages;'):upload.index('syncMeshBuffers();')]
assert 'meshStore.elidedMisses()' in begin and 'residentPages.reset();residentMap.clear();' in begin and 'active->meshPlan->payload.discard();' in begin
assert begin.index('residentPages.reset()') < begin.index('payload.discard()')
assert 'if(cpu.elided)return false;' in perform
print('wiring ok')

# File-backed pressure generations retain replay bytes. Normal snapshots may
# only elide/reuse resources whose lifetime survives Reset. Idle DEFAULT pages
# must not invalidate an active MANAGED generation.
assert 'meshStore.beginStaging(pageKeys,anyElided?elidedPages:std::vector<uint8_t>(),compatiblePage)' in begin
compatible = begin[begin.index('const auto compatiblePage='):begin.index('if(!meshStore.beginStaging(')]
assert 'payload->pages->temporary||' in compatible and 'vbPool==D3DPOOL_MANAGED' in compatible and 'ibPool==D3DPOOL_MANAGED' in compatible
reset_mesh = w[w.index('void releaseDefaultMesh(){'):w.index('void reset(){')]
assert 'meshStore.table()' in reset_mesh and 'D3DPOOL_DEFAULT' in reset_mesh
assert 'if(activeLost){' in reset_mesh and 'keepPressureProbes=true;resetReplayPending=true;clearMesh(resetReplayScoped);releaseMeshPool();uploadedMap.clear();' in reset_mesh and 'else trimMeshPools();' in reset_mesh
assert 'resetReplayScoped=active&&active->bvh==uploaded.lock()&&active->meshPlan==uploadedMeshPlan.lock();' in reset_mesh and 'resetReplayPlan=active->meshPlan;' in reset_mesh
assert reset_mesh.index('meshStore.cancel();') < reset_mesh.index('if(activeLost)')

# Pressure Reset waits for retirement with a separate, nonblocking query. The
# shared live-ring frame fence must never be ended by this recovery gate.
reset = w[w.index('void reset(){'):w.index('// 0.3.176 (U0/S0)')]
assert reset.index('drop(resetReplayQuery);resetReplayFence.drop();') < reset.index('releaseDefaultMesh();')
fence = w[w.index('bool pressureReplayReady(){'):w.index('bool upload(NorthlightStreaming::Budget& streamBudget){')]
assert 'auto& queries=resetReplayFence;' in fence and 'resetReplayQuery->GetData(nullptr,0,0)' in fence
assert 'D3DGETDATA_FLUSH' not in fence and 'Sleep(' not in fence and 'queries=fence()' not in fence
assert '!meshRetry.ready(GetTickCount())||!pressureReplayReady()' in upload
assert 'drop(resetReplayQuery);resetReplayFence.drop();' in release
print('pressure pool compatibility and nonblocking Reset wiring ok')

# A previous page balance must never admit the next real driver creation.
assert 'GlobalMemoryStatusEx(&current)' in acquire and 'debitGrowth(memory,capacity)' not in acquire
assert 'NorthlightGeometryMemory::ResetReplayReserve' in acquire and 'pressureReplayAllocation()' in acquire
scope = w[w.index('bool pressureReplayAllocation(){'):w.index('bool pressureReplayReady(){')]
assert 'active->bvh==resetReplaySource.lock()&&active->meshPlan==resetReplayPlan.lock()' in scope
assert 'drop(resetReplayQuery);resetReplayFence.drop();resetReplayPending=false;' in scope
assert 'pageMemory' not in perform and 'if(!sampled)' not in perform
print('fresh page admission and exact Reset recovery source wiring ok')

assert upload.index("reusableMaterial(m,") < upload.index('admitsGrowth("pressure-material"') < upload.index("HRESULT hr=d->CreateTexture(m.width,m.height")

# Reset during an unfinished replay must preserve managed probe ownership and
# cancel staging even if no DEFAULT page has been allocated yet.
assert 'const bool continuing=pressureReplayAllocation();' in reset_mesh
assert 'if(continuing){keepPressureProbes=true;resetReplayPending=true;}' in reset_mesh
assert 'if(!anyDefault&&!continuing)return;' in reset_mesh
assert 'uploaded=next.bvh;uploadedMeshPlan=next.plan;' in commit
assert 'uploaded.reset();uploadedMeshPlan.reset();' in w
