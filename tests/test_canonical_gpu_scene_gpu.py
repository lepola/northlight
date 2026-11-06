#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib manual
"""Canonical native scene versus shipped peer layout, actual MRT/MSAA, no game."""
import argparse,ast,hashlib,importlib.util,json,os,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path[:0]=[str(fp.TESTS),str(fp.SCRIPTS/'shaders')]
from client_fixtures import shared_gpu_models
from shader_compiler import Compiler
p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path);p.add_argument('--benchmark',action='store_true');p.add_argument('--pairs',type=int,default=7);p.add_argument('--compatible-compiler',action='store_true');a=p.parse_args()
out=(a.output or fp.output_dir()/'canonical-native-gpu').resolve();out.mkdir(parents=True,exist_ok=True)
headers=(a.headers or fp.src('scene_native_store.h').parent).resolve();sha=lambda q:hashlib.sha256(Path(q).read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('scene_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard);guard.processes()
fixture,sources,count=shared_gpu_models(out)
reference=fp.TESTS/'test_shared_gpu_residency_gpu.py'
shader=next(ast.literal_eval(n.args[0]) for n in ast.walk(ast.parse(reference.read_text())) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='write_text' and isinstance(n.func.value,ast.Name) and n.func.value.id=='source')
source=out/'fixture.hlsl';source.write_text(shader);compiler=Compiler(allow_compatible_binary=a.compatible_compiler)
for entry,profile in [('VS','vs_3_0'),('PS','ps_3_0')]:compiler.compile(source,entry,profile,out/(entry.lower()+'.bin'))
watched=[Path(__file__),reference,fp.TESTS/'canonical_gpu_scene_gpu.cpp',fp.TESTS/'shared_gpu_residency_gpu.cpp',headers/'scene_native_store.h']
inputs={str(q):sha(q) for q in watched};env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n');env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
exe=out/'canonical-scene.exe';command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(headers),*fp.test_include_flags(),str(fp.TESTS/'canonical_gpu_scene_gpu.cpp'),'-luser32','-o',str(exe)]
subprocess.run(command,env=env,check=True)
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'));base=[str(exe),str(backend),str(out)+'/',str(fixture)]
guard.processes();result=subprocess.run(base,cwd=out,env=env,text=True,capture_output=True,timeout=240);exact=result.stdout;(out/'gpu.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,flush=True);result.check_returncode();assert 'CANONICAL_GPU_NATIVE_PASS' in exact
rows=[];summary={}
if a.benchmark:
 for pair in range(a.pairs+2):
  for mode in (('peer','canonical') if pair%2==0 else ('canonical','peer')):
   guard.processes();result=subprocess.run([*base,mode],cwd=out,env=env,text=True,capture_output=True,timeout=180);result.check_returncode();(out/f'{pair}-{mode}.log').write_text(result.stdout+result.stderr)
   row=dict(re.findall(r'(\w+)=([^\s]+)',next(line for line in result.stdout.splitlines() if line.startswith('CANONICAL_GPU_TIMING'))));row.update(pair=pair-2,mode=mode);rows.append(row)
 for mode in ('peer','canonical'):summary[mode]={key:statistics.median(float(r[key]) for r in rows if r['mode']==mode and r['pair']>=0) for key in ('cpuMs','uploads','peak','fallback')}
assert all(sha(q)==digest for q,digest in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'sources':sources,'meshes':count,'fixture_sha256':sha(fixture),'inputs':inputs,'compiler':compiler.provenance,'backend':str(backend),'backend_sha256':sha(backend),'command':command,'exact':exact,'rows':rows,'summary':summary,'game_launched':False,'GPU_used':True,'limitation':'Authored real-GPU component workload; not a game FPS measurement.'},indent=2)+'\n');print(json.dumps(summary,indent=2))
