#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Material replay avoids duplicate state queries and restores failed POM uploads."""
import os,re,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets,decode_blp
from extract_signatures import variants

out=fp.output_dir()/'material-replay-state';out.mkdir(parents=True,exist_ok=True)
assets=Assets(archives='stock')
try:
    for model,families in (
        ('vs_3_0',('terrain','mapobjdiffuse_t1','detaildoodad','mapobjspecular_t1')),
        ('vs_2_0',('terrain','mapobjdiffuse_t1','detaildoodad')),
        ('ps_3_0',('terrain1','terrain0','mapobjdiffuse','mapobjcomposite','detaildoodad')),
    ):
        for family in families:
            path=f'shaders\\{"vertex" if model.startswith("vs") else "pixel"}\\{model}\\{family}.bls'
            index=30 if family=='mapobjspecular_t1' else 0
            code={i:c for i,_,c in variants(assets.read(path))}[index]
            (out/(path.replace('\\','_')+f'-{index}.bin')).write_bytes(code)
    source=assets.read('dungeons\\textures\\silvermooncity\\bm_slvrmn_cobble01.blp')
finally:assets.close()
level=0
while True:
    w,h,rgba=decode_blp(source,max(1,256>>level));bgra=bytearray(rgba)
    bgra[0::4]=rgba[2::4];bgra[2::4]=rgba[0::4]
    payload=struct.pack('<5I',0x31524746,w,h,21,4)+bgra
    (out/f'source.fgr.{level}').write_bytes(payload)
    if not level:(out/'source.fgr').write_bytes(payload)
    if w==h==1:break
    level+=1
(out/'old-LocalDirect.bin').write_bytes((fp.TESTS/'fixtures/material-before/material-before-LocalDirect.bin').read_bytes())
exe=out/'material-replay-state.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'auto_bump_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll'))
run=subprocess.run([str(exe),backend,str(out)+'/',str(out/'source.fgr')],cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=90)
(out/'gpu.log').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,run.stdout+run.stderr
assert len(re.findall(r'POM_STATE_CALLS family=\S+ vertexGets=1 pixelGets=2 constantSets=2',run.stdout))==6,run.stdout
assert 'AUTOBUMP_GPU_PASS' in run.stdout
print(run.stdout.strip())
