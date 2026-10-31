#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile the actual world bind lambda; verify canonical scene wiring, ownership and material offsets."""
import subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import replay_gpu_fixture
out=fp.output_dir()/'shared-gpu-wiring';out.mkdir(parents=True,exist_ok=True)
stub=replay_gpu_fixture.stub;a=stub.index('using UINT');b=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:a]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[b:]
(out/'d3d9.h').write_text(stub);(out/'replay_fixture.h').write_text(replay_gpu_fixture.fixture)
world=fp.src('world_renderer.h').read_text();bump=fp.src('auto_bump_renderer.h').read_text();device=fp.src('renderer.cpp').read_text()
first=world.index('        auto bindReplay=[&](Replay& p){');last=world.index('        replayTimeDeferredIndices.clear();',first)
binding=world[first:last]
assert 'world=std::make_unique<WorldRenderer>(ext,gpuScene);' in device and 'autoBump=std::make_unique<NorthlightAutoBump>(ext,gpuScene);' in device
assert 'world.reset();gpuScene.reset();releaseResources();' in device and 'gpuScene->setFrame(frame);' in device
assert 'sharedReplayGeometry' not in world and 'sharedGeometry.borrow' not in bump
assert 'NativeClient& deferredGPU=gpuScene->materials;' in bump and 'NativeClient& replayGpuCache=gpuScene->shadows;' in world
assert 'deferredGPU.commitPending();' in bump and bump.index('deferredGPU.commitPending();')<bump.index('bool success=true;const Deferred* previous=nullptr;')
assert 'DrawIndexedPrimitive(mesh.topology,INT(placement.vertexBase),0,mesh.vertexCount,placement.indexStart,mesh.primitiveCount)' in bump
assert 'DrawPrimitive(mesh.topology,placement.vertexBase,mesh.primitiveCount)' in bump
scene=fp.src('gpu_scene.h').read_text()
for field in ['staticCasters','meshStore','meshBuffers','meshPoolTrim','terrain','terrainSources','decodedSources','workerPreparation','normalMaps','displacement','materialTargets','captureSources']:assert field in scene,field
assert 'gpuScene->attach(replaySnapshots)' in world and 'gpuScene->attach(deferredSnapshots)' in bump
assert 'gpuScene->attach(snapshots)' in fp.src('auto_displacement_renderer.h').read_text()
source='''#include "replay_fixture.h"
#include "scene_native_store.h"
using Mesh=NorthlightDrawSnapshot::Mesh;
struct Replay {std::shared_ptr<const Mesh> shared;bool gpuCached=false,indexed=true;IDirect3DVertexBuffer9* stream[4]={};IDirect3DIndexBuffer9* index=nullptr;UINT offset[4]={},stride[4]={},start=0;INT base=0;
 const Mesh& mesh()const{return *shared;}~Replay(){for(auto p:stream)if(p)p->Release();if(index)index->Release();}};
int main(){
 Device driver;auto* d=&driver;NorthlightGpuScene::NativeStore scene;NorthlightGpuScene::NativeClient replayGpuCache(scene,NorthlightGpuScene::Consumer::Shadow),materials(scene,NorthlightGpuScene::Consumer::Material);
 unsigned growthCalls=0;auto growthAdmission=[&](size_t){++growthCalls;return false;};bool allowCache=true;
'''+binding+'''
 auto m=std::const_pointer_cast<Mesh>(mesh());m->sourceIdentity=NorthlightSourceIdentity::make({1,2,3});{Replay p;p.shared=m;NorthlightReplayGPU::Placement at;
 scene.setFrame(1);assert(!materials.bind(d,m,p.stream,p.index,[](size_t){return true;},at));
 scene.setFrame(2);const bool ready=materials.bind(d,m,p.stream,p.index,[](size_t){return true;},at);assert(ready||materials.commitPending());
 const auto calls=driver.calls;bindReplay(p);assert(p.gpuCached&&driver.calls==calls&&!growthCalls&&replayGpuCache.population().entries==0);
 assert(!p.base&&!p.start&&!p.offset[0]&&p.stride[0]==m->streams[0].stride);
 allowCache=false;bindReplay(p);assert(!p.gpuCached&&!growthCalls&&driver.calls==calls);
 allowCache=true;auto rebuilt=std::make_shared<Mesh>(*m);p.shared=rebuilt;m.reset();bindReplay(p);assert(p.gpuCached&&!growthCalls&&driver.calls==calls);
 materials.clear();assert(!materials.bytes()&&replayGpuCache.bytes());bindReplay(p);assert(p.gpuCached&&driver.calls==calls);
 }
 materials.clear();replayGpuCache.clear();assert(!alive);puts("SHARED_GPU_WIRING_PASS actual_world_lambda no_growth_on_reuse admission_gate offsets reconstructed_source ownership_transfer device_root");
}
'''
(out/'wiring.cpp').write_text(source)
for flags in (['-O2'],['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']):
 exe=out/('sanitized.exe' if len(flags)>1 else 'optimized.exe')
 command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,'-I',str(out),*fp.test_include_flags(),str(out/'wiring.cpp'),'-o',str(exe)]
 subprocess.run(command,env=fp.zig_env(),check=True);subprocess.run([str(exe)],check=True)
