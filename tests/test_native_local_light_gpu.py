#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Exact real-client native single-light endpoints; no WoW or client writes."""
from pathlib import Path
import json
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_m2_shader_families import collect
from extract_signatures import variants,fnv1a64
from world_scene_builder import Assets

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
shaders=collect(fp.client_root())[0];vertex=shaders[0xe8d455338d8ae2ff][1]
with_assets=Assets()
try:
 pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2]
 wmo=next(code for _,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjdiffuse_t1.bls')) if fnv1a64(code)==0x22d86896259e7cb9)
 wmo_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\mapobjdiffuse.bls')))[2]
finally:with_assets.close()
exe=out/'native-local-light.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra',*fp.test_include_flags(),fp.TESTS/'native_local_light_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
for family,vs,ps in [('m2',vertex,pixel),('wmo',wmo,wmo_pixel)]:
 run=out/family;run.mkdir(exist_ok=True)
 (run/'native-vs.bin').write_bytes(vs);(run/'native-ps.bin').write_bytes(ps)
 env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 result=subprocess.run(list(map(str,[exe,fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',run/'native-vs.bin',run/'native-ps.bin',run,family,fp.COMPILED])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
 (run/'gpu.log').write_text(result.stdout+result.stderr);print(family,result.stdout+result.stderr);result.check_returncode()
