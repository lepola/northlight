# northlight-test: requires=windows-exe,zig manual
"""Production binary coverage and sampler restoration; execution is opt-in."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
 parser=argparse.ArgumentParser(__doc__);parser.add_argument('--run-gpu',action='store_true');parser.add_argument('--backend',type=Path);args=parser.parse_args();out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=out/'point-transmission.exe'
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.test_include_flags(),str(fp.TESTS/'point_transmission_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW)
 evidence={'game_launched':False,'gpu_executed':False,'source_sha256':{n:digest(fp.tracked(n)) for n in ('point_transmission_gpu.cpp','point_transmission_filter.h','local_light_effects.hlsl')}}
 if args.run_gpu:
  assert args.backend and args.backend.is_file();import windows_gpu_guard;windows_gpu_guard.processes();manifest=json.loads((fp.SHADERS/'local-light-shader-build.json').read_text());assert manifest['source_sha256']==digest(fp.SHADERS/'local_light_effects.hlsl')
  for name in ('LocalShadowVS','LocalShadowPS'):assert digest(fp.COMPILED/(name+'.bin'))==manifest['shaders'][name]['sha256']
  env=fp.zig_env()
  for key in ('DXVK_LOG_PATH','DXVK_SHADER_CACHE_PATH','DXVK_STATE_CACHE_PATH'):folder=out/key.lower();folder.mkdir(exist_ok=True);env[key]=str(folder)
  run=subprocess.run([str(exe),str(fp.COMPILED),str(args.backend.resolve())],env=env,cwd=out,capture_output=True,text=True,timeout=90,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW);(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True);assert run.returncode==0,run.stderr;assert 'checkedPixels=49152' in run.stdout and 'POINT_TRANSMISSION_GPU_PASS' in run.stdout
  evidence.update(gpu_executed=True,checked_pixels=49152,backend_sha256=digest(args.backend),samplers_restored=True,wrong_linear_control_rejected=True)
 (out/'validation.json').write_text(json.dumps(evidence,indent=2)+'\n');print('POINT_TRANSMISSION_COMPILE_PASS' if not args.run_gpu else 'POINT_TRANSMISSION_PASS')
if __name__=='__main__':main()
