#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact rotate-away/back retention with independently binding slot and byte limits; no game/GPU."""
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', default='1404591')
args = parser.parse_args()
out = fp.out()/'terrain-retention-budget'
out.mkdir(parents=True, exist_ok=True)
tree = ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
values = {n.targets[0].id: ast.literal_eval(n.value) for n in tree.body
          if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
          and n.targets[0].id in ('stub', 'harness')}
stub=values['stub']
if sys.platform == 'win32':
    stub = stub[:stub.index('using UINT')] + '#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n' + stub[stub.index('constexpr unsigned D3DUSAGE'):]
(out/'d3d9.h').write_text(stub)
relative = fp.src('terrain_capture_bounds.h').relative_to(fp.REPO).as_posix()
baseline = subprocess.check_output(['git', 'show', args.baseline_ref+':'+relative], cwd=fp.REPO)
(out/'baseline.h').write_bytes(baseline)
(out/'slots-only.h').write_bytes(baseline.replace(b'ContentEntries=4096', b'ContentEntries=8192'))
prefix = r'''
#include "terrain_capture_bounds.h"
#define NorthlightTerrainCapture BaselineTerrainCapture
#include "baseline.h"
#undef NorthlightTerrainCapture
#define NorthlightTerrainCapture SlotsTerrainCapture
#include "slots-only.h"
#undef NorthlightTerrainCapture
#include <chrono>
'''
body = r'''
static uint64_t identities[]={11,12},revisions[]={21,22};
static uint64_t identity(void*,bool index){return identities[index];}
static uint64_t version(void*,bool index){return revisions[index];}
template<class Snapshot> static uint64_t digest(const Snapshot& s){
 uint64_t h=1469598103934665603ull;auto word=[&](uint32_t v){h^=v;h*=1099511628211ull;};
 for(const auto& p:s.positions)for(float v:{p.x,p.y,p.z}){uint32_t bits;std::memcpy(&bits,&v,4);word(bits);}
 for(auto i:s.indices)word(i);for(const auto& c:s.bounds.chunks){word(c.x);word(c.y);}return h;
}
static void fill(Device& d,unsigned draws,unsigned chunks){
 d.vb.bytes.resize(16+size_t(draws)*chunks*145*24);std::vector<uint16_t> indices;int rows[17];unsigned vertex=0;
 for(unsigned row=0;row<17;++row){rows[row]=int(vertex);vertex+=(row&1)?8:9;}assert(vertex==145);
 for(unsigned chunk=0;chunk<chunks;++chunk)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned a=chunk*145+unsigned(rows[y*2]+x),b=a+1,c=chunk*145+unsigned(rows[y*2+2]+x),e=c+1,m=chunk*145+unsigned(rows[y*2+1]+x);for(unsigned i:{a,b,m,b,e,m,e,c,m,c,a,m})indices.push_back(uint16_t(i));}
 d.ib.bytes.resize(indices.size()*2);std::memcpy(d.ib.bytes.data(),indices.data(),d.ib.bytes.size());
 for(unsigned draw=0;draw<draws;++draw)for(unsigned chunk=0;chunk<chunks;++chunk){const double origin=17066.666666666666,step=100./3.,x0=origin-(102+2*(draw%400)+chunk)*step,y0=origin-(101+draw/400)*step;
  unsigned v=0;for(unsigned row=0;row<17;++row)for(unsigned col=0;col<((row&1)?8u:9u);++col)d.put(draw*chunks*145+chunk*145+v++,{float(x0+(col+.5*(row&1))*step/8),float(y0+row*step/16),float(draw%17)+float(row)*.1f});}
}
template<class Cache,class Snapshot> static void scenario(const char* label,Device& d,unsigned draws,unsigned chunks){
 Cache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
 uint64_t readBytes=0,uploadBytes=0,hash=0;size_t misses=0,hits=0,evictions=0,countPressure=0,bytePressure=0;
 std::vector<std::weak_ptr<const Snapshot>> previous(draws);std::shared_ptr<const Snapshot> held;
 const auto start=std::chrono::steady_clock::now();
 for(unsigned frame=0;frame<12;++frame){cache.clearFrame();const unsigned sector=frame%4;
  for(unsigned n=0;n<draws/4;++n){const unsigned draw=sector*(draws/4)+((frame/4)&1?draws/4-1-n:n);std::shared_ptr<const Snapshot> snapshot;
   assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*chunks*145),0,chunks*145,0,chunks*256,d.view,snapshot));
   const auto old=previous[draw].lock();if(frame>=4){hits+=old==snapshot;misses+=old!=snapshot;if(old!=snapshot)uploadBytes+=snapshot->positions.size()*20;}
   previous[draw]=snapshot;if(draw==0&&!held)held=snapshot;
   assert(snapshot->positions.size()==chunks*145&&snapshot->indices.size()==chunks*768);hash^=digest(*snapshot)+(uint64_t(draw)<<32);
  }
  if(frame>=4){readBytes+=cache.bytesRead();const auto m=cache.maintenance();evictions+=m.evictions;countPressure+=m.countPressure;bytePressure+=m.bytePressure;}
  assert(cache.persistentBytes()<=32u*1024u*1024u&&cache.persistentEntries()<=Cache::persistentEntryLimit());
 }
 const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 const auto retained=cache.persistentEntries();const auto bytes=cache.persistentBytes();const auto saved=digest(*held);
 cache.setPersistentByteLimit(1);assert(cache.persistentBytes()<=cache.persistentByteLimit()&&digest(*held)==saved);
 cache.clearPersistent();assert(!cache.persistentEntries()&&digest(*held)==saved);
 std::printf("TERRAIN_BUDGET label=%s draws=%u chunks=%u hits=%zu misses=%zu readBytes=%llu replacementVertexBytes=%llu evictions=%zu countPressure=%zu bytePressure=%zu retainedEntries=%zu retainedBytes=%llu geometryDigest=%016llx elapsedMs=%.6f\n",label,draws,chunks,hits,misses,(unsigned long long)readBytes,(unsigned long long)uploadBytes,evictions,countPressure,bytePressure,retained,(unsigned long long)bytes,(unsigned long long)hash,elapsed);
}
int main(){
 for(unsigned chunks:{1u,2u}){Device d;unsigned draws=chunks==1?4800:4200;fill(d,draws,chunks);
  scenario<BaselineTerrainCapture::FrameCache,BaselineTerrainCapture::MeshSnapshot>("baseline",d,draws,chunks);
  scenario<SlotsTerrainCapture::FrameCache,SlotsTerrainCapture::MeshSnapshot>("slotsOnly",d,draws,chunks);
  scenario<FrameCache,MeshSnapshot>("current",d,draws,chunks);
  assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 }
}
'''
source = prefix+values['harness'].split('int main(){')[0]+body
(out/'retention.cpp').write_bytes(source.encode())
exe = out/'retention.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wall', '-Wextra', '-Werror', '-I', str(out), *fp.test_include_flags(),
                str(out/'retention.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
result = subprocess.check_output([str(exe)], text=True)
print(result, end='')
rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.splitlines() if line.startswith('TERRAIN_BUDGET')]
assert len(rows)==6
for group in (rows[:3], rows[3:]):
    assert len({r['geometryDigest'] for r in group})==1
    assert all(int(r['retainedBytes'])<=32*1024*1024 for r in group)
    assert all(int(group[-1][key])==0 for key in ('misses', 'readBytes', 'replacementVertexBytes', 'evictions'))
    assert int(group[-1]['hits'])==int(group[-1]['draws'])*2
assert int(rows[0]['countPressure'])>0 and int(rows[0]['bytePressure'])==0
assert int(rows[1]['misses'])==0 and int(rows[4]['bytePressure'])>0
assert int(rows[4]['retainedEntries'])<int(rows[5]['retainedEntries'])
(out/'validation.json').write_bytes((json.dumps({'baseline':args.baseline_ref, 'gameLaunched':False,
    'byteLimit':32*1024*1024, 'rows':rows}, indent=2)+'\n').encode())
