#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Lossless terrain cache retention under rotate-away/back pressure; no game or GPU."""
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
parser.add_argument('--baseline-ref')
parser.add_argument('--pressure', action='store_true', help='Replay 9,800 contracts exceeding the unchanged 32 MiB cap')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
out = args.output or fp.out()/'terrain-rotation-cache'
out.mkdir(parents=True, exist_ok=True)
fixture = ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
strings = {node.targets[0].id: ast.literal_eval(node.value) for node in fixture.body
           if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
           and node.targets[0].id in ('stub', 'harness')}
stub=strings['stub']
if sys.platform == 'win32':
    stub = stub[:stub.index('using UINT')] + '#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n' + stub[stub.index('constexpr unsigned D3DUSAGE'):]
(out/'d3d9.h').write_text(stub)
source = r'''
#include "terrain_capture_bounds.h"
#include <chrono>
#include <thread>
'''+strings['harness'].split('int main(){')[0]+r'''
static uint64_t identities[2]={11,12},revisions[2]={21,22};
static uint64_t identity(void*,bool index){return identities[index];}
static uint64_t version(void*,bool index){return revisions[index];}
template<class Snapshot> uint64_t digest(const Snapshot& s){
 uint64_t hash=1469598103934665603ull;auto word=[&](uint32_t v){hash^=v;hash*=1099511628211ull;};
 for(const auto& p:s.positions)for(float v:{p.x,p.y,p.z}){uint32_t bits;std::memcpy(&bits,&v,4);word(bits);}
 for(auto i:s.indices)word(i);for(const auto& c:s.bounds.chunks){word(c.x);word(c.y);}return hash;
}
template<class Cache,class Snapshot> void scenario(const char* label,Device& d){
 Cache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
 const auto start=std::chrono::steady_clock::now();uint64_t readBytes=0,uploadBytes=0,hash=0;size_t misses=0,hits=0,evictions=0;
 std::vector<std::weak_ptr<const Snapshot>> previous(2800);std::shared_ptr<const Snapshot> held;
 for(unsigned frame=0;frame<12;++frame){cache.clearFrame();
  // Stable camera translation, four yaw sectors. The final return revisits the
  // exact same world geometry; order reverses on alternate rotations.
  const unsigned sector=frame%4;
  for(unsigned n=0;n<700;++n){const unsigned draw=sector*700+((frame/4)&1?699-n:n);std::shared_ptr<const Snapshot> snapshot;
   assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,d.view,snapshot));
   const auto old=previous[draw].lock();if(frame>=4){hits+=old==snapshot;misses+=old!=snapshot;if(old!=snapshot)uploadBytes+=snapshot->positions.size()*32;}
   previous[draw]=snapshot;if(draw==0&&!held)held=snapshot;
   assert(snapshot->positions.size()==290&&snapshot->indices.size()==1536);hash^=digest(*snapshot)+(uint64_t(draw)<<32);
  }
  if(frame>=4){readBytes+=cache.bytesRead();evictions+=cache.maintenance().evictions;}
  assert(cache.persistentBytes()<=32u*1024u*1024u&&cache.persistentEntries()<=8192);
 }
#ifdef INDEX_PLANS_CURRENT
 const auto& plans=cache.maintenance();
 std::printf("INDEX_PLANS_LAST_FRAME hits=%u vertices=%llu indices=%llu\n",plans.indexPlanHits,(unsigned long long)plans.indexPlanVertices,(unsigned long long)plans.indexPlanIndices);
#endif
 const auto retainedEntries=cache.persistentEntries();const auto retainedBytes=cache.persistentBytes();
 const auto saved=digest(*held);cache.clearPersistent();cache.clearFrame();assert(digest(*held)==saved);
 // A reset/map transition cannot keep an old source identity. A changed vertex
 // must rebuild without changing the consumer-held immutable geometry.
 ++identities[0];++revisions[0];auto p=held->positions[0];p.z+=37;d.put(0,{p.x,p.y,p.z});std::shared_ptr<const Snapshot> changed;
 assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,290,0,512,d.view,changed));assert(changed->positions[0].z==p.z&&digest(*held)==saved);
 revisions[0]=0;cache.clearFrame();assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,290,0,512,d.view,changed));
 std::printf("TERRAIN_ROTATION label=%s hits=%zu misses=%zu readBytes=%llu replacementVertexBytes=%llu evictions=%zu retainedEntries=%zu retainedBytes=%llu geometryDigest=%016llx elapsedMs=%.6f\n",label,hits,misses,(unsigned long long)readBytes,(unsigned long long)uploadBytes,evictions,retainedEntries,(unsigned long long)retainedBytes,(unsigned long long)hash,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
int main(){
 Device d;d.vb.bytes.resize(16+2800*290*24);std::vector<uint16_t> indices;int rows[17];unsigned vertex=0;
 for(unsigned row=0;row<17;++row){rows[row]=int(vertex);vertex+=(row&1)?8:9;}assert(vertex==145);
 for(unsigned chunk=0;chunk<2;++chunk)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned a=chunk*145+unsigned(rows[y*2]+x),b=a+1,c=chunk*145+unsigned(rows[y*2+2]+x),e=c+1,m=chunk*145+unsigned(rows[y*2+1]+x);for(unsigned i:{a,b,m,b,e,m,e,c,m,c,a,m})indices.push_back(uint16_t(i));}
 assert(indices.size()==1536);d.ib.bytes.resize(indices.size()*2);std::memcpy(d.ib.bytes.data(),indices.data(),d.ib.bytes.size());
 auto fill=[&]{for(unsigned draw=0;draw<2800;++draw)for(unsigned chunk=0;chunk<2;++chunk){const double origin=17066.666666666666,step=100./3.,x0=origin-(102+2*(draw%400)+chunk)*step,y0=origin-(101+draw/400)*step;
  unsigned v=0;for(unsigned row=0;row<17;++row)for(unsigned col=0;col<((row&1)?8u:9u);++col)d.put(draw*290+chunk*145+v++,{float(x0+(col+.5*(row&1))*step/8),float(y0+row*step/16),float(draw%17)+float(row)*.1f});}};
 fill();
 fill();identities[0]=11;revisions[0]=21;scenario<FrameCache,MeshSnapshot>("current",d);
 assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
}
'''
if args.pressure:
    source = source.replace('2800', '9800').replace('frame<12', 'frame<42').replace('frame%4', 'frame%14').replace('frame/4', 'frame/14').replace('frame>=4', 'frame>=14')
(out/'rotation.cpp').write_text(source)
versions=[]
if args.baseline_ref:
    relative = fp.src('terrain_capture_bounds.h').relative_to(fp.REPO).as_posix()
    versions.append(('baseline', subprocess.check_output(['git', 'show', args.baseline_ref+':'+relative], cwd=fp.REPO)))
versions.append(('current', fp.src('terrain_capture_bounds.h').read_bytes()))
rows=[]
for label, header in versions:
    directory=out/label;directory.mkdir(exist_ok=True)
    (directory/'terrain_capture_bounds.h').write_bytes(header)
    exe=directory/'rotation.exe'
    # Separate binaries preserve each FrameCache's private Indices friendship.
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                    '-UNDEBUG', *(['-DINDEX_PLANS_CURRENT'] if label=='current' else []),
                    '-Wall', '-Wextra', '-Werror', '-I', str(directory), '-I', str(out), *fp.test_include_flags(),
                    str(out/'rotation.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    text=subprocess.check_output([str(exe)], text=True);print(label, text, end='')
    row=next(dict(re.findall(r'(\w+)=([^\s]+)', line)) for line in text.splitlines() if line.startswith('TERRAIN_ROTATION'))
    row['label']=label;rows.append(row)
    if args.pressure and label=='current':
        plans=next(dict(re.findall(r'(\w+)=(\d+)', line)) for line in text.splitlines() if line.startswith('INDEX_PLANS_LAST_FRAME'))
        assert plans==dict(hits='700',vertices='203000',indices='1075200'), plans
if args.baseline_ref:
    assert len(rows)==2 and rows[0]['geometryDigest']==rows[1]['geometryDigest']
    assert int(rows[0]['misses'])>0
if args.pressure:
    assert rows[-1]['geometryDigest']=='ff50261aab2ddbfa' and int(rows[-1]['misses'])==19600
    assert int(rows[-1]['readBytes'])==196392000 and int(rows[-1]['replacementVertexBytes'])==181888000 and int(rows[-1]['evictions'])==19600
else:
    assert rows[-1]['geometryDigest']=='e6a64536c883ee30' and int(rows[-1]['hits'])==5600
    assert all(int(rows[-1][key])==0 for key in ('misses', 'readBytes', 'replacementVertexBytes', 'evictions'))
(out/'validation.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
