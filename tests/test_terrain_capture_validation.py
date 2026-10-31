#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig timing
"""Frozen full-capture oracle for reusing validated terrain remap bounds; no game."""
import argparse
import ast
import json
from pathlib import Path
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()
out = fp.output_dir()/'terrain-capture-validation'
out.mkdir(parents=True, exist_ok=True)
def literals(name):
    return {n.targets[0].id: ast.literal_eval(n.value) for n in ast.parse((fp.TESTS/name).read_text()).body
            if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and isinstance(n.value,ast.Constant) and isinstance(n.value.value,str)}
fixture = literals('test_terrain_snapshot.py')
helpers = literals('test_terrain_retention_budget.py')['body']
fill = helpers[helpers.index('static void fill('):helpers.index('template<class Cache,class Snapshot> static void scenario')]
(out/'d3d9.h').write_text(fixture['stub'])
baseline = subprocess.check_output(['git','show','cc69996:src/world/terrain_capture_bounds.h'],cwd=fp.REPO)
(out/'baseline.h').write_bytes(baseline)
prefix = '''#include "terrain_capture_bounds.h"
#define NorthlightTerrainCapture FrozenTerrainCapture
#include "baseline.h"
#undef NorthlightTerrainCapture
#include <chrono>
#include <cstdio>
'''
body = r'''
template<class A,class B> static void equal(const A& a,const B& b){
 assert(a.positions.size()==b.positions.size()&&a.indices.size()==b.indices.size());
 for(size_t i=0;i<a.positions.size();++i)assert(!std::memcmp(&a.positions[i],&b.positions[i],12));
 for(size_t i=0;i<a.indices.size();++i)assert(a.indices[i]==b.indices[i]);
 assert(!std::memcmp(a.bounds.low,b.bounds.low,12)&&!std::memcmp(a.bounds.high,b.bounds.high,12));
 assert(a.bounds.chunks.size()==b.bounds.chunks.size());
 for(size_t i=0;i<a.bounds.chunks.size();++i){const auto& x=a.bounds.chunks[i];const auto& y=b.bounds.chunks[i];
  assert(x.x==y.x&&x.y==y.y&&x.minX==y.minX&&x.minY==y.minY&&x.maxX==y.maxX&&x.maxY==y.maxY);}
}
static void controls(){
 Device d;fill(d,4,2);const auto vb=d.vb.bytes,ib=d.ib.bytes;
 unsigned passed=0,refused=0;
 for(unsigned variation=0;variation<96;++variation){
  d.vb.bytes=vb;d.ib.bytes=ib;
  if(variation%8==1){float v=std::numeric_limits<float>::quiet_NaN();std::memcpy(d.vb.bytes.data()+20+(variation%145)*24,&v,4);}
  if(variation%8==2){float v=std::numeric_limits<float>::infinity();std::memcpy(d.vb.bytes.data()+24+(variation%145)*24,&v,4);}
  if(variation%8==3){uint16_t v=600;std::memcpy(d.ib.bytes.data(),&v,2);}
  if(variation%8==4)for(unsigned i=0;i<12;++i){uint16_t v=0;std::memcpy(d.ib.bytes.data()+i*2,&v,2);}
  if(variation%8==5){float v=0;std::memcpy(d.vb.bytes.data()+20+(variation%145)*24,&v,4);}
  if(variation%8==6){float v=-0.f;std::memcpy(d.vb.bytes.data()+28+(variation%145)*24,&v,4);}
  FrameCache current;FrozenTerrainCapture::FrameCache old;
  const auto topology=variation&1?D3DPT_TRIANGLESTRIP:D3DPT_TRIANGLELIST;
  const UINT primitives=topology==D3DPT_TRIANGLELIST?512:1534;
  std::shared_ptr<const MeshSnapshot> a;std::shared_ptr<const FrozenTerrainCapture::MeshSnapshot> b;
  Diagnostics da;FrozenTerrainCapture::Diagnostics db;
  const bool ca=current.readMeshShared(&d,topology,0,0,290,0,primitives,d.view,a,&da);
  const bool cb=old.readMeshShared(&d,topology,0,0,290,0,primitives,d.view,b,&db);
  assert(ca==cb&&int(da.reason)==int(db.reason)&&da.hr==db.hr);
  if(ca){equal(*a,*b);++passed;}else ++refused;
  current.clearFrame();old.clearFrame();MeshSnapshot upA;FrozenTerrainCapture::MeshSnapshot upB;
  const bool ua=current.readMeshUP(&d,topology,0,290,primitives,d.ib.bytes.data(),D3DFMT_INDEX16,d.vb.bytes.data()+16,24,d.view,upA,&da);
  const bool ub=old.readMeshUP(&d,topology,0,290,primitives,d.ib.bytes.data(),D3DFMT_INDEX16,d.vb.bytes.data()+16,24,d.view,upB,&db);
  assert(ua==ub&&int(da.reason)==int(db.reason)&&da.hr==db.hr);if(ua)equal(upA,upB);
 }
 assert(passed&&refused&&d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
 std::printf("PASS terrain full-capture controls accepted=%u refused=%u UP/DIP exact positions/indices/bounds/errors\n",passed,refused);
}
static uint64_t identity(void*,bool index){return index?12:11;}
static uint64_t revision(void*,bool index){return index?22:21;}
static uint64_t wideIdentity(void*,bool index){return index?0xfedcba9876543210ull:0x89abcdef01234567ull;}
static uint64_t wideRevision(void*,bool index){return index?0x76543210fedcba98ull:0x0123456789abcdefull;}
static void identityControls(){
 for(bool wide:{false,true}){
  Device d;if(wide)d.setIndices32();
  d.put(4,{-10200,-1180,3});d.put(5,{-10198,-1180,4});d.put(6,{-10200,-1178,5});
  FrameCache current;FrozenTerrainCapture::FrameCache old;
  current.setIdentityProvider(wideIdentity);old.setIdentityProvider(wideIdentity);
  current.setVersionProvider(wideRevision);old.setVersionProvider(wideRevision);
  std::shared_ptr<const MeshSnapshot> a;std::shared_ptr<const FrozenTerrainCapture::MeshSnapshot> b;
  assert(current.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,a));
  assert(old.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,b));equal(*a,*b);
  const auto& packed=a->sourceIdentity->words;const auto& original=b->sourceIdentity->words;
  assert(packed.size()==15&&original.size()==22&&b->sourceIdentity->bytes()-a->sourceIdentity->bytes()==56);
  for(unsigned i=0;i<8;++i)assert(packed[i]==original[i]);
  for(unsigned i=8;i<22;++i)assert(uint32_t(packed[8+(i-8)/2]>>((i-8)%2*32))==original[i]);
  current.clearPersistent();current.clearFrame();std::shared_ptr<const MeshSnapshot> reconstructed;
  assert(current.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,reconstructed));
  assert(a!=reconstructed&&NorthlightSourceIdentity::same(a->sourceIdentity,reconstructed->sourceIdentity));
 }
 std::puts("PASS exact 22-word source contract recovered from 15 words, full-width tokens, signed base, both index formats, reconstruction");
}
template<class Cache,class Snapshot> static void batch(const char* label,Device& d,unsigned draws){
 Cache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(revision);
 uint64_t checksum=0,readBytes=0;unsigned captures=0;
 const auto start=std::chrono::steady_clock::now();
 for(unsigned repeat=0;repeat<20;++repeat){cache.clearPersistent();cache.clearFrame();
  for(unsigned draw=0;draw<draws;++draw){std::shared_ptr<const Snapshot> s;
   assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,d.view,s));
   assert(s->positions.size()==290&&s->indices.size()==1536);++captures;
   for(const auto& p:s->positions){uint32_t v;std::memcpy(&v,&p.z,4);checksum+=v;}
   for(auto i:s->indices)checksum+=i;
  }readBytes+=cache.bytesRead();
 }
 const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 std::printf("TERRAIN_CAPTURE label=%s captures=%u readBytes=%llu checksum=%llu ms=%.6f\n",label,captures,(unsigned long long)readBytes,(unsigned long long)checksum,ms);
}
int main(int argc,char** argv){
 if(argc==1){controls();identityControls();return 0;}
 Device d;fill(d,64,2);
 if(argv[1][0]=='b')batch<FrozenTerrainCapture::FrameCache,FrozenTerrainCapture::MeshSnapshot>("baseline",d,64);
 else batch<FrameCache,MeshSnapshot>("current",d,64);
}
'''
source = prefix+fixture['harness'].split('int main(){')[0]+fill+body
(out/'capture.cpp').write_bytes(source.encode())
checks=[]
for mode,flags in [('O2',['-O2']),('debug',['-O0','-g'])]:
    exe=out/(mode+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,
                    '-Wall','-Wextra','-Werror','-I',str(out),*fp.test_include_flags(),str(out/'capture.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.check_output([str(exe)],text=True)
    print(mode,result,end='');checks.append({'mode':mode,'pass':True,'output':result})
timing=[]
if args.benchmark:
    import re
    for round_index in range(9):
        for label in (('baseline','current') if round_index%2==0 else ('current','baseline')):
            line=subprocess.check_output([str(out/'O2.exe'),label],text=True).strip()
            print(line);row=dict(re.findall(r'(\w+)=([^ ]+)',line));row['round']=round_index;timing.append(row)
    assert len({(r['checksum'],r['captures'],r['readBytes']) for r in timing})==1
result={'baseline':'cc69996','game_launched':False,'checks':checks,'timing':timing}
if timing:
    result['median_ms']={label:statistics.median(float(r['ms']) for r in timing if r['label']==label) for label in ('baseline','current')}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('TERRAIN_CAPTURE_VALIDATION_PASS')
