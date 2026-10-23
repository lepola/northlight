"""Check every mounted SM3 M2 pixel combiner's original alpha and native validator."""
from pathlib import Path
import argparse,json,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path[:0]=[str(root),str(root/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets
from extract_signatures import variants
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);args=p.parse_args();backend='native' if 'windows' in args.backend.lower() else 'dxvk'
out=fp.out()/('native-alpha-'+backend);out.mkdir(exist_ok=True);assets=Assets();files=[]
try:
    for name in assets.providers:
        if name.startswith('shaders\\pixel\\ps_3_0\\combiners_') and name.endswith('.bls'):
            for index,_,code in variants(assets.read(name)):
                file=name.rsplit('\\',1)[1]+f'-{index}.bin';(out/file).write_bytes(code);files.append(file)
finally:assets.close()
(out/'files.txt').write_text('\n'.join(files)+'\n')
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'));exe=out/'alpha.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests/native_material_alpha_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(out)],cwd=out,env=env,capture_output=True,text=True,timeout=120);(out/'gpu.log').write_text(run.stdout+run.stderr)
if run.returncode:raise RuntimeError(run.stdout+run.stderr)
assert 'NATIVE_MATERIAL_ALPHA_PASS' in run.stdout;print(run.stdout,flush=True)
(out/'validation.json').write_text(json.dumps({'backend':args.backend,'files':len(files),'output':run.stdout},indent=2)+'\n')
