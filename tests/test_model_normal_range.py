# northlight-test: requires=windows-exe,zig,client,stormlib
"""CPU proofs for the narrow native rigid clip.w rejection; no GPU/game."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import client_fixtures
from extract_signatures import variants
from world_scene_builder import Assets
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
assets=Assets()
try:
    code=next(code for i,_,code in variants(assets.read('shaders\\vertex\\vs_3_0\\diffuse_t1.bls')) if i==1)
finally:assets.close()
source=out/'range-rigid.bin';source.write_bytes(code);exe=out/'model-normal-range.exe'
client_fixtures.actor_client_programs(out)
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'model_normal_range.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe),str(source),str(client_fixtures.four_bone_body_vs3())],check=True)
