#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,pillow
"""Guarded production lens image/state test and offscreen PNG previews."""
import argparse, os, struct, subprocess, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',type=Path,required=True);p.add_argument('--preview-source',type=Path);p.add_argument('--build-only',action='store_true');a=p.parse_args()
if os.name!='nt':raise SystemExit('Windows required')
out=fp.out()/'lens-gpu';out.mkdir(parents=True,exist_ok=True);exe=out/'lens-gpu.exe'
world=fp.src('world_renderer.h').read_text(encoding='utf-8');start=world.index('struct NameMaskCompositeBind {');end=world.index('\n};',start)+3
(out/'lens_name_mask.h').write_text('#include "name_stencil.h"\n'+world[start:end]+'\n',encoding='utf-8')
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'lens_effects_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
if a.build_only:print('GPU lens oracle compiled; not run');sys.exit(0)
w,h=960,540
if a.preview_source:
    img=Image.open(a.preview_source).convert('RGBA').resize((w,h),Image.Resampling.LANCZOS)
else:
    img=Image.new('RGBA',(w,h));img.putdata([((x*3+y)%256,(x+y*2)%256,((x//24+y//24)%2)*160+40,255) for y in range(h) for x in range(w)])
img.save(out/'base.png');(out/'input.fgr').write_bytes(struct.pack('<5I',0x31524746,w,h,21,4)+img.tobytes('raw','BGRA'))
guard=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command','if (Get-Process -Name wow,WowClient64 -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }'],creationflags=subprocess.CREATE_NO_WINDOW)
if guard.returncode:raise SystemExit('Game process detected; offscreen GPU test deferred')
subprocess.run([str(exe),str(a.backend.resolve()),str(out/'input.fgr')],cwd=out,check=True,timeout=180)
for name in ('rain','rain-later','snow','snow-later'):
    img=Image.frombytes('RGBA',(w,h),(out/(name+'.raw')).read_bytes(),'raw','BGRA');img.save(out/(name+'.png'))
print('Previews:',out/'rain.png',out/'snow.png')
