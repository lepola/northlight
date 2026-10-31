#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe timing
"""Alternating x86 pressure-boundary clear timings with the actual populated terrain cache."""
import argparse,ast,json,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'terrain-retirement-bench');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
module=ast.parse((fp.TESTS/'benchmark_shared_terrain_bounds.py').read_text())
source=next(ast.literal_eval(n.value.right) for n in module.body if isinstance(n,ast.Assign) and any(getattr(t,'id','')=='source' for t in n.targets))
prefix=(fp.TESTS/'position_bounds.cpp').read_text().split('int main(){')[0]
# Reuse the same real-D3D-interface fake device and ADT-shaped geometry fixture.
source=prefix+source[:source.index(' struct Trial {')].replace('700','7000')+r'''
 NorthlightStreaming::CpuRetirement queue;
 for(unsigned pair=0;pair<10;++pair)for(unsigned n=0;n<2;++n){
  const bool async=(pair+n)&1;NorthlightTerrainCapture::FrameCache c;c.setIdentityProvider(identity);c.setVersionProvider(revision);
  for(unsigned draw=0;draw<7000;++draw){if(draw%200==0)c.clearFrame();std::shared_ptr<const NorthlightTerrainCapture::MeshSnapshot> mesh;
   assert(c.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,nullptr,mesh));}
  const auto entries=c.persistentEntries();const auto bytes=c.persistentBytes();assert(entries>5000&&bytes<=(32u<<20));
  assert(queue.waitIdle(10000));const auto start=std::chrono::steady_clock::now();const auto deferred=c.clearPersistent(async?&queue:nullptr);
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();assert(bool(deferred)==async&&!c.persistentEntries());assert(queue.waitIdle(10000));
  assert(d.vb.refs==1&&d.ib.refs==1&&d.decl.refs==1);
  std::printf("TERRAIN_CLEAR_X86 async=%u ms=%.6f entries=%zu bytes=%zu deferred=%zu\n",unsigned(async),ms,entries,size_t(bytes),deferred);
 }
}
'''
cpp=a.output/'benchmark.cpp';cpp.write_text(source);exe=a.output/'benchmark.exe'
with (a.output/'compile.log').open('w') as log:subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-Werror=format','-std=c++17',*fp.test_include_flags(),str(cpp),'-o',str(exe)],env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=180);(a.output/'benchmark.log').write_text(run.stdout+run.stderr)
rows=[dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in run.stdout.splitlines() if s.startswith('TERRAIN_CLEAR_X86')];assert len(rows)==20
assert all(bool(int(r['deferred']))==bool(int(r['async'])) for r in rows)
assert len({(r['entries'],r['bytes']) for r in rows})==1
medians={k:statistics.median(float(r['ms']) for r in rows if r['async']==k) for k in ('0','1')}
report=dict(scope='render-thread terrain CPU clear only; queue drain excluded; not FPS',medians_ms=medians,ratio=medians['1']/medians['0'],rows=rows)
(a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='rows'}))
