#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Compile the production terrain publisher and DIP routing with real CPU snapshots."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path)
args = p.parse_args()
world = fp.src('world_renderer.h').read_text()
start = world.index('    void capture(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertexTotal,')
publisher = world[start:world.index('    void releaseReplayGPU()', start)]
device = fp.src('renderer.cpp').read_text()
start = device.index('            NorthlightCaptureMetadata::PositionBounds positions;')
route = device[start:device.index('},draw);}', start)]
fixture = ast.parse((fp.TESTS / 'test_terrain_snapshot.py').read_text())
values = {n.targets[0].id: ast.literal_eval(n.value) for n in fixture.body
          if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
          and n.targets[0].id in ('stub', 'harness')}
source = values['harness'].split('int main(){')[0] + r'''
#include <string>
struct IDirect3DVertexShader9 {};
static void logf(const char*,...){}
struct CpuScope {explicit CpuScope(long long*){}};
namespace NorthlightRenderThreadProbe {static bool sampleFrame(unsigned){return true;}}
namespace NorthlightWaterRenderer {static constexpr unsigned NoUserPointer=0;}
struct CaptureDevice:Device {
 bool projectionBad=false;
 HRESULT GetVertexShaderConstantF(UINT first,float* out,UINT count)override{
  assert((first==0||first==4)&&count==4);std::memcpy(out,view,64);if(first==4&&projectionBad)out[0]=2;return 0;
 }
};
struct World {
 using TerrainSnapshot=std::shared_ptr<const MeshSnapshot>;
 CaptureDevice* d;FrameCache terrainBoundsCache;std::vector<int> replays;
 bool readyValue=true,terrain=true,constantSelfCheck=false,captureSampled=false;
 unsigned calls=0,models=0,terrainCaptureCalls=0,terrainAttempts=0,terrainFailures=0;
 long long terrainCaptureTicks=0;float projection[3]={1,1,0};struct {float view[16];} context;
 TerrainSnapshot accepted;std::vector<std::string>* events=nullptr;
 explicit World(CaptureDevice& device):d(&device){std::memcpy(context.view,d->view,64);}
 bool ready()const{return readyValue;}
 struct Shader {bool terrain;};Shader lookupShader(IDirect3DVertexShader9*)const{return {terrain};}
 bool terrainShader(IDirect3DVertexShader9* shader){return lookupShader(shader).terrain;}
 template<class... T>void captureModel(T...){++models;}
 void appendTerrain(TerrainSnapshot snapshot){accepted=std::move(snapshot);}
 PUBLISHER
};
struct Geometry {struct {D3DPRIMITIVETYPE topology;INT base;UINT minimum,vertices,start,primitives;bool indexed;} draw;};
struct Bump {
 std::vector<std::string>* events;unsigned calls=0;bool hint=false;
 template<class Draw>void capture(IDirect3DVertexShader9* vs,Draw draw,bool up,const Geometry* geometry,const NorthlightCaptureMetadata::PositionBounds* bounds){
  events->push_back("bump");++calls;assert(!up&&bounds&&bounds->owner==geometry&&bounds->shader==vs&&"SHARED_BOUND_OWNER_MISMATCH");hint=bounds->known;assert(draw()==42);
 }
};
struct Native {
 std::vector<std::string>* events;unsigned calls=0;
 int DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count){
  assert(t==D3DPT_TRIANGLELIST&&base==-3&&min==7&&vertices==3&&start==1&&count==1);events->push_back("native");++calls;return 42;
 }
};
struct Route {
 World* world;Bump* autoBump;Native* ext;std::vector<std::string>* events;
 bool enabled=true,applied=false,failed=false;unsigned frame=127;
 template<class T>void captureWater(IDirect3DVertexShader9*,unsigned,T){events->push_back("water");}
 void run(IDirect3DVertexShader9* vs){
  D3DPRIMITIVETYPE t=D3DPT_TRIANGLELIST;INT base=-3;UINT min=7,vertices=3,start=1,count=1;
  Geometry geometry{{t,base,min,vertices,start,count,true}};auto draw=[]{};
  ROUTE
 }
};
static void fill(CaptureDevice& d){d.put(4,{-10200,-1180,3});d.put(5,{-10198,-1180,4});d.put(6,{-10200,-1178,5});}
int main(){
 IDirect3DVertexShader9 vs;
 {CaptureDevice d;fill(d);World w(d);NorthlightCaptureMetadata::PositionBounds out;out.known=true;
  const auto capture=[&]{w.capture(D3DPT_TRIANGLELIST,-3,7,3,1,1,true,&vs,false,true,&out);};
  capture();assert(out.known&&out.low[2]==3&&out.high[2]==5&&w.accepted);
  d.put(4,{-10200,-1180,30});capture();assert(out.known&&out.low[2]==4&&out.high[2]==30&&"STALE_SHARED_BOUND");
  for(unsigned fail=0;fail<5;++fail){out.known=true;w.readyValue=fail!=0;d.projectionBad=fail==1;d.vb.failLock=fail==2;d.view[12]=fail==3?1.f:0.f;w.terrain=fail!=4;capture();assert(!out.known&&"FAILED_CAPTURE_PUBLISHED_BOUND");}
 }
 unsigned cases=0;
 for(unsigned bits=0;bits<64;++bits){CaptureDevice d;fill(d);World w(d);std::vector<std::string> events;w.events=&events;
  Bump bump{&events};Native native{&events};Route r{&w,(bits&1)?&bump:nullptr,&native,&events};
  r.enabled=bits&2;r.applied=bits&4;r.failed=bits&8;w.terrain=bits&16;w.readyValue=bits&32;
  const bool active=r.autoBump&&r.enabled&&!r.applied&&!r.failed,shared=active&&w.terrain;
  r.run(&vs);assert(w.calls==1&&bump.calls==unsigned(active)&&native.calls==unsigned(active)&&"SHARED_CAPTURE_COUNT_MISMATCH");
  assert(bump.hint==(shared&&w.readyValue));
  std::vector<std::string> expected;if(shared)expected.push_back("world");if(active){expected.push_back("bump");expected.push_back("native");}if(!shared)expected.push_back("world");expected.push_back("water");
  assert(events==expected&&"SHARED_CAPTURE_ORDER_MISMATCH");assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);++cases;
 }
 std::printf("PASS shared terrain publisher: current XYZ/failure invalidation; %u production DIP routes, exact native arguments, one world capture, original model/disabled order and balanced references\n",cases);
}
'''
publisher = publisher.replace('        if(positions)positions->known=false;',
                              '        ++calls;if(events)events->push_back("world");\n        if(positions)positions->known=false;')
assert publisher.count('++calls;') == 1
source = source.replace('PUBLISHER', publisher).replace('ROUTE', route)
with tempfile.TemporaryDirectory(prefix='shared-terrain-bounds-') as tmp:
    out = args.output or Path(tmp)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'd3d9.h').write_text(values['stub'])
    (out / 'shared.cpp').write_text(source)
    for label, flags in [('O2', ['-O2']), ('O0', ['-O0']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
        binary = out / label
        subprocess.run(['clang++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(out), *fp.test_include_flags(), str(out / 'shared.cpp'), '-o', str(binary)], check=True)
        result = subprocess.check_output([str(binary)], text=True)
        (out / (label + '.log')).write_text(result)
        print(label, result, end='', flush=True)
    for label, old, new, marker in [
        ('failed-proof', 'if(positions)positions->known=false;', '(void)positions;', 'FAILED_CAPTURE_PUBLISHED_BOUND'),
        ('route', 'if(sharedTerrain)captureWorld(&positions);', 'if(false)captureWorld(&positions);', 'SHARED_CAPTURE_COUNT_MISMATCH'),
    ]:
        assert source.count(old) == 1
        cpp = out / (label + '.cpp')
        cpp.write_text(source.replace(old, new))
        binary = out / label
        subprocess.run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', '-I', str(out), *fp.test_include_flags(), str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode != 0 and marker in result.stderr, result
    print('PASS stale failed proof and missing terrain publisher controls rejected')
    (out / 'validation.json').write_text(json.dumps({'pass': True, 'world_source_sha256': hashlib.sha256(world.encode()).hexdigest(),
        'renderer_source_sha256': hashlib.sha256(device.encode()).hexdigest(), 'game_launched': False}, indent=2) + '\n')
