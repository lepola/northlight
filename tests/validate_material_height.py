"""Prove raised leaves, recessed cracks and centered POM with production code."""
from pathlib import Path
import argparse,json,struct,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path[:0]=[str(root),str(root/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);args=p.parse_args()
name='native' if 'windows' in args.backend.lower() else 'dxvk';out=fp.out()/('height-'+name);out.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'renderer'))
from world_scene_builder import Assets,decode_blp
assets=Assets()
try:
    w,h,pixels=decode_blp(assets.read('dungeons\\textures\\silvermooncity\\bm_slvrmn_cobble01.blp'),128)
finally:assets.close()
bgra=bytearray(pixels);bgra[0::4]=pixels[2::4];bgra[2::4]=pixels[0::4]
(out/'silvermoon-source.fgr').write_bytes(struct.pack('<5I',0x31524746,w,h,21,4)+bgra)
fixture=out/'SignedPOM.hlsl';fixture.write_text((root/'shaders/auto_parallax.hlsl').read_text()+'''
sampler2D HeightMap:register(s0);
float4 SignedPOM(float2 uv:TEXCOORD0):COLOR0 {MaterialFrame f;f.p=float3(-1,0,-2);f.tangent=float3(1,0,0);f.bitangent=float3(0,1,0);f.normal=float3(0,0,1);f.tx=float2(.001,0);f.ty=float2(0,.001);return float4(parallaxUVFrame(HeightMap,uv,2,float4(.2,16,32,0),f),0,1);}
''')
Compiler(allow_compatible_binary=True).compile(fixture,'SignedPOM','ps_3_0',out/'SignedPOM.bin')
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'));exe=out/'height.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests/material_height_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(out)],cwd=out,env=env,capture_output=True,text=True,timeout=60);(out/'gpu.log').write_text(run.stdout+run.stderr)
if run.returncode:raise RuntimeError(run.stdout+run.stderr)
assert 'MATERIAL_HEIGHT_PASS' in run.stdout;print(run.stdout,flush=True)
(out/'validation.json').write_text(json.dumps({'backend':args.backend,'raised_leaves':True,'negative_crevices':True,'cpu_gpu_agreement':True,'centered_POM':True,'output':run.stdout},indent=2)+'\n')
