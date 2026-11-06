#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compare precipitation shader GPU timings on 1080p 1x and 4x offscreen targets."""
import argparse,json,os,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend',type=Path,required=True);p.add_argument('--build-only',action='store_true');p.add_argument('--half-resolution',action='store_true');a=p.parse_args()
if os.name!='nt':raise SystemExit('Windows required')
out=fp.out()/('weather-shader-perf-half' if a.half_resolution else 'weather-shader-perf');out.mkdir(parents=True,exist_ok=True);exe=out/'weather-shader-perf.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra',*fp.test_include_flags(),str(fp.TESTS/'weather_shader_perf_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
if a.build_only:print('GPU benchmark compiled; not run');sys.exit(0)
text=''
for samples in (0,4):
    guard=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command','if (Get-Process -Name wow,WowClient64 -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }'],creationflags=subprocess.CREATE_NO_WINDOW)
    if guard.returncode:raise SystemExit('Game process detected; offscreen GPU test deferred')
    text+=subprocess.check_output([str(exe),str(a.backend.resolve()),str(samples),'1' if a.half_resolution else '0'],cwd=out,text=True,timeout=180)
(out/'timing.txt').write_text(text,encoding='utf-8')
rows=[]
for line in text.splitlines():
    if line.startswith('TIMING '):rows.append(dict(item.split('=',1) for item in line.split()[1:]))
summary={}
for samples in (0,4):
 for entry in (0,1):
  for snow in (0,1):
   for strength in (0,1):
    select=lambda variant:[float(r['ms']) for r in rows if (int(r['samples']),int(r['entry']),int(r['snow']),int(r['strength']),r['variant'])==(samples,entry,snow,strength,variant)]
    b,c=select('baseline'),select('candidate')
    if len(b)!=8 or len(c)!=8:raise SystemExit(f'Incomplete timing results for samples={samples}, entry={entry}, snow={snow}, strength={strength}; see {out / "timing.txt"}')
    summary[f'{samples}/{entry}/{snow}/{strength}']={'baseline_ms':statistics.median(b),'candidate_ms':statistics.median(c),'baseline_samples':b,'candidate_samples':c}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(summary,indent=2))
