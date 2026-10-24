#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Replay production celestial shaders against archived project bytecode; no game data."""
from pathlib import Path
import math,os,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
baseline=fp.TESTS/'fixtures/celestial-before'
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
moon=out/'synthetic-moon.fct';size=128;pixels=bytearray()
for y in range(size):
    for x in range(size):
        radius=math.hypot((x+.5-size/2)/(size/2),(y+.5-size/2)/(size/2))
        pixels.extend((190,190,190,255 if radius<.9 else 0))
moon.write_bytes(struct.pack('<4I',0x31544346,1,size,size)+pixels)
cases=[('celestial_occlusion_gpu.cpp',['sky-before-CelestialDiscPS.bin','sky-before-CelestialVeilPS.bin']),
       ('celestial_cloud_order_gpu.cpp',['sky-before-CelestialDiscPS.bin',moon]),
       ('celestial_diffuse_bloom_gpu.cpp',['sky-opaque-CelestialVeilPS.bin','sky-hot-CelestialVeilPS.bin']),
       ('celestial_fog_occlusion_gpu.cpp',['sky-before-WorldFog.bin','sky-before-CelestialHaloVisibilityPS.bin'])]
for source,arguments in cases:
    exe=out/(Path(source).stem+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    '-Wno-return-type',*fp.include_flags(),str(fp.TESTS/source),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
    args=[str(argument if isinstance(argument,Path) else baseline/argument) for argument in arguments]
    subprocess.run([str(exe),backend,*args],cwd=out,check=True,timeout=90)
print('PASS four celestial production-shader replay families')
