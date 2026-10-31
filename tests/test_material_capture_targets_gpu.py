#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Production material flush, exact MRT pixels and paired target-continuity timings; no game."""
import argparse,hashlib,importlib.util,json,os,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler
p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path);p.add_argument('--pairs',type=int,default=5);p.add_argument('--benchmark',action='store_true');p.add_argument('--compatible-compiler',action='store_true');a=p.parse_args()
out=a.output or fp.output_dir()/'material-capture-targets-gpu';out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('target_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard);guard.processes()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();header=(a.headers or fp.src('auto_bump_renderer.h').parent)/'auto_bump_renderer.h'
reference=out/'reference';reference.mkdir(exist_ok=True)
old=subprocess.check_output(['git','show','dbda9f3:src/water/auto_bump_renderer.h'],cwd=fp.REPO,text=True)
# Correct the old target-before-viewport ordering for this semantic reference.
# The old ordering is separately rejected by the captured-viewport CPU oracle.
old=old.replace('    bool occlusionUnavailable=false;','    bool independentCaptureWrites=false;\n    bool occlusionUnavailable=false;',1)
needle='            if(SUCCEEDED(hr)){P::Scope draw(profile,P::CaptureDraw,packet->tag);'
assert old.count(needle)==1;old=old.replace(needle,'            if(SUCCEEDED(hr)&&FAILED(d->SetViewport(&packet->viewport)))return false;\n'+needle,1)
(reference/'auto_bump_renderer.h').write_bytes(old.encode())
source=out/'targets.hlsl';source.write_text('''struct Vertex {float4 position:POSITION;};Vertex VS(float4 p:POSITION){Vertex o;o.position=p;return o;}
float4 Normal:register(c20);float4 Occlusion:register(c21);
struct Outputs {float4 normal:COLOR0;float4 occlusion:COLOR1;};Outputs PS(){Outputs o;o.normal=Normal;o.occlusion=Occlusion;return o;}
''')
compiler=Compiler(allow_compatible_binary=a.compatible_compiler)
(out/'material_target_gpu_support.h').write_bytes((fp.TESTS/'auto_bump_gpu.cpp').read_text().split('int main(')[0].encode())
for entry,profile in [('VS','vs_3_0'),('PS','ps_3_0')]:compiler.compile(source,entry,profile,out/(entry.lower()+'.bin'))
env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n');env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'));executables={};folders={};runs=[]
watched=[header,fp.TESTS/'material_capture_targets_gpu.cpp',Path(__file__)];inputs={str(p):sha(p) for p in watched}
for name,include in [('reference',reference),('current',header.parent)]:
 folder=out/name;folder.mkdir(exist_ok=True);folders[name]=folder
 for binary in ['vs.bin','ps.bin']:(folder/binary).write_bytes((out/binary).read_bytes())
 exe=folder/'targets.exe';subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17','-I',str(include),'-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'material_capture_targets_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True);executables[name]=exe
 guard.processes();r=subprocess.run([str(exe),str(backend),str(folder)+'/', 'exact'],cwd=folder,env=env,text=True,capture_output=True,timeout=240);(folder/'exact.log').write_text(r.stdout+r.stderr);r.check_returncode();assert 'CAPTURE_TARGET_GPU_PASS' in r.stdout
files=list(folders['reference'].glob('*.pixels'));assert len(files)==72,len(files)
for file in files:assert file.read_bytes()==(folders['current']/file.name).read_bytes(),file.name
rows=[]
if a.benchmark:
 for pair in range(a.pairs+2):
  for name in (['reference','current'] if pair%2==0 else ['current','reference']):
   folder=folders[name];guard.processes();r=subprocess.run([str(executables[name]),str(backend),str(folder)+'/', 'timing'],cwd=folder,env=env,text=True,capture_output=True,timeout=180);(out/(str(pair)+'-'+name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
   for line in r.stdout.splitlines():
    if line.startswith('CAPTURE_TARGET_TIMING '):rows.append(dict(re.findall(r'(\w+)=([^\s]+)',line),mode=name,pair=pair-2))
summary={}
for pattern in ['0','1','2']:
 summary[pattern]={name:{key:statistics.median(float(r[key]) for r in rows if r['pattern']==pattern and r['mode']==name and r['pair']>=0) for key in ['gpuMs','cpuMs','targetBinds']} for name in ['reference','current']} if rows else {}
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'baseline':'dbda9f3 with captured viewport reapplied','backend':str(backend),'backend_sha256':sha(backend),'compiler':compiler.provenance,'exact_files':len(files),'exact_bytes':sum(p.stat().st_size for p in files),'rows':rows,'summary':summary,'game_launched':False,'limitation':'Authored actual-flush workload, unchanged two-output shader; not game FPS or an effect-kernel timing.'},indent=2)+'\n')
print('CAPTURE_TARGET_GPU_EXACT_PASS files='+str(len(files)));print(json.dumps(summary,indent=2))
