#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Exact ambient fog GPU comparison; authored field/depth fixture, no client required."""
from pathlib import Path
import argparse,ctypes,importlib.util,json,os,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',type=Path,default=Path(r'C:\Windows\SysWOW64\d3d9.dll'));p.add_argument('--prefix');p.add_argument('--output',type=Path);a=p.parse_args()
out=a.output or fp.output_dir();out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('ambient_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
def closed():
    assert not any(q['name'].lower().startswith('wow') and q['name'].lower().endswith('.exe') for q in guard.processes()),'Game process active; defer independent GPU work'
api=ctypes.WinDLL('kernel32',use_last_error=True);api.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_wchar_p];api.CreateMutexW.restype=ctypes.c_void_p;api.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
mutex=api.CreateMutexW(None,0,'Local\\NorthlightOfflineGPU');assert mutex and api.WaitForSingleObject(mutex,0) in (0,128)
if not a.prefix:
    w,h=256,160;bank=[[0.]*4 for _ in range(79)]
    bank[0]=[1/w,1/h,1,1000];bank[1]=[1,1,1,0];bank[2]=[1,192,192,1/64];bank[33]=[w,h,w/2,h/2]
    prefix=str(out/'authored-')
    Path(prefix+'constants.f32').write_bytes(struct.pack('<316f',*(x for row in bank for x in row)))
    def image(name,w,h,fmt,bytesPerPixel,pixel):
        Path(prefix+name+'.fgr').write_bytes(struct.pack('<5I',0x31524746,w,h,fmt,bytesPerPixel)+pixel*(w*h))
    image('scene',w,h,21,4,bytes([80,100,120,255]));image('distance',w//2,h//2,114,4,struct.pack('<f',80))
else:prefix=a.prefix
exe=out/'fog-ambient.exe';closed()
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'world_pass_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
rows=[]
for original,candidate in [('WorldFog','WorldFogAmbient'),('FogClouds','FogClouds')]:
    for scenario in [1,2,8,9]:
        for reverse in [False,True]:
            closed();programs=[fp.COMPILED/(original+'.bin'),fp.COMPILED/(candidate+'.bin')]
            if reverse:programs.reverse()
            command=[str(exe),prefix,*map(str,programs),original,str(a.backend),str(scenario)]
            r=subprocess.run(command,cwd=out,capture_output=True,text=True,timeout=120)
            (out/f'{original}-{scenario}-{int(reverse)}.log').write_text(r.stdout+r.stderr)
            assert r.returncode==0,(command,r.stdout,r.stderr)
            row=json.loads(next(line for line in r.stdout.splitlines() if line.startswith('{')))
            assert row['changed']==row['max_abs']==row['nonfinite']==0,row
            if reverse:row['old_ms'],row['new_ms']=row['new_ms'],row['old_ms']
            row.update(scenario=scenario,reverse=reverse);rows.append(row)
            (out/'validation.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS ambient fog: 16 exact GPU comparisons, complete cloud controls retained; no game launched')
