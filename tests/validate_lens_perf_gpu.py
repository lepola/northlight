#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Guarded 1080p DXVK lens timing, including copy and state handling."""
import argparse,json,os,statistics,struct,subprocess,sys
from pathlib import Path
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',type=Path,required=True);p.add_argument('--preview-source',type=Path,required=True);p.add_argument('--build-only',action='store_true');a=p.parse_args()
if os.name!='nt':raise SystemExit('Windows required')
out=fp.out()/'lens-perf';out.mkdir(parents=True,exist_ok=True);exe=out/'lens-perf.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'lens_perf_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
if a.build_only:print('Lens timing compiled; not run');sys.exit(0)
img=Image.open(a.preview_source).convert('RGBA').resize((1920,1080),Image.Resampling.LANCZOS)
(out/'scene.fgr').write_bytes(struct.pack('<5I',0x31524746,1920,1080,21,4)+img.tobytes('raw','BGRA'))
text=''
for samples in (0,4):
    guard=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command','if (Get-Process -Name wow,WowClient64 -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }'],creationflags=subprocess.CREATE_NO_WINDOW)
    if guard.returncode:raise SystemExit('Game process detected; GPU test deferred')
    text+=subprocess.check_output([str(exe),str(a.backend.resolve()),str(samples),str(out/'scene.fgr')],cwd=out,text=True,timeout=180)
(out/'timing.txt').write_text(text,encoding='utf-8');rows=[dict(item.split('=',1) for item in line.split()[1:]) for line in text.splitlines() if line.startswith('TIMING ')]
summary={}
for samples in (0,4):
 for mode,name in enumerate(('off','copy','rain','snow')):
    selected=[r for r in rows if int(r['samples'])==samples and int(r['mode'])==mode]
    if len(selected)!=8:raise SystemExit('Incomplete timing samples')
    summary[f'{samples}/{name}']={k:statistics.median(float(r[k]) for r in selected) for k in ('gpu_ms','cpu_us')}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(summary,indent=2))
