#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu
"""Reuse bounded normal targets without mutating active leases or bound maps."""
from pathlib import Path
import argparse,os,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--honor-hold',action='store_true',help='Also check the active client MonumentRock inscription texture read-only');args=parser.parse_args()
out=fp.output_dir()/'auto-normal-reuse-gpu';out.mkdir(parents=True,exist_ok=True)
binary=out/'normal-reuse.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17',*fp.test_include_flags(),str(Path(__file__).with_name('auto_normal_reuse_gpu.cpp')),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
runtime=os.environ.get('NORTHLIGHT_TEST_D3D9',str(Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll'))
fixture=[]
if args.honor_hold:
    sys.path.insert(0,str(fp.RENDERER))
    from world_scene_builder import Assets,decode_blp
    import struct
    assets=Assets()
    try:w,h,rgba=decode_blp(assets.read(r'world\azeroth\elwynn\passivedoodads\monumentrock\statuebase.blp'),512)
    finally:assets.close()
    bgra=bytearray(rgba);bgra[0::4]=rgba[2::4];bgra[2::4]=rgba[0::4]
    path=out/'honor-hold.fgr';path.write_bytes(struct.pack('<5I',0x31524746,w,h,21,4)+bgra);fixture=[str(path)]
run=subprocess.run([str(binary),runtime,*fixture],cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=180)
(out/'gpu.log').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,run.stdout+run.stderr
assert 'NORMAL_VISIBLE_SET_PASS' in run.stdout,run.stdout
assert re.search(r'NORMAL_REUSE_PASS compared=180 churnAllocations=48 ',run.stdout),run.stdout
print(run.stdout.strip())
