#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib manual
"""Native shared-residency MRT/depth/cutout/rotation oracle, stock M2 bytes, MSAA 0/4/8."""
import argparse,hashlib,importlib.util,json,os,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path[:0]=[str(fp.TESTS),str(fp.SCRIPTS/'shaders')]
from client_fixtures import shared_gpu_models
from shader_compiler import Compiler
p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path)
p.add_argument('--benchmark',action='store_true');p.add_argument('--pairs',type=int,default=7);p.add_argument('--compatible-compiler',action='store_true');a=p.parse_args()
assert a.pairs>0
out=(a.output or fp.output_dir()/'shared-gpu-residency-native').resolve();out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('native_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
guard.processes()
fixture,sources,count=shared_gpu_models(out)
source=out/'fixture.hlsl';source.write_text('''
float4 Center:register(c0);float4 Rows[3]:register(c1);float4 Pose:register(c4);
struct Output {float4 position:POSITION;float3 normal:TEXCOORD0;float2 uv:TEXCOORD1;float lightPass:TEXCOORD2;};
Output VS(float3 position:POSITION,float4 weights:BLENDWEIGHT,float3 normal:NORMAL,float2 uv:TEXCOORD0){
 Output o;float3 p=(position-Center.xyz)*Center.w+weights.xyz*Pose.x;
 float3 q=float3(dot(p,Rows[0].xyz),dot(p,Rows[1].xyz),dot(p,Rows[2].xyz));
 o.position=float4(q.xy*.8,q.z*.15+.5,1);o.normal=normal;o.uv=uv;o.lightPass=Pose.y;return o;
}
struct Targets {float4 colour:COLOR0;float4 normal:COLOR1;};
Targets PS(Output input){Targets o;clip(frac(input.uv.x*13)+frac(input.uv.y*11)-.3);
 float3 n=normalize(input.normal);o.colour=float4(.2+.6*frac(input.uv),.2+input.lightPass*.4,1);
 o.normal=float4(n*.5+.5,1);return o;}
''')
compiler=Compiler(allow_compatible_binary=a.compatible_compiler)
for entry,profile in [('VS','vs_3_0'),('PS','ps_3_0')]:compiler.compile(source,entry,profile,out/(entry.lower()+'.bin'))
headers=(a.headers or fp.src('replay_gpu_batches.h').parent).resolve()
sha=lambda q:hashlib.sha256(Path(q).read_bytes()).hexdigest()
inputs={str(q):sha(q) for q in [Path(__file__),fp.TESTS/'client_fixtures.py',fp.TESTS/'shared_gpu_residency_gpu.cpp',headers/'replay_gpu_cache.h',headers/'replay_gpu_batches.h',headers/'replay_gpu_binding.h']}
snapshot=headers/'draw_snapshot.h' if (headers/'draw_snapshot.h').is_file() else fp.src('draw_snapshot.h')
inputs[str(snapshot)]=sha(snapshot)
env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
exe=out/'shared-residency.exe';command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-O2','-Wall','-Wextra','-Werror','-I',str(headers),*fp.test_include_flags(),str(fp.TESTS/'shared_gpu_residency_gpu.cpp'),'-luser32','-o',str(exe)]
subprocess.run(command,env=env,check=True)
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))
base=[str(exe),str(backend),str(out)+'/',str(fixture)]
guard.processes();result=subprocess.run(base,cwd=out,env=env,text=True,capture_output=True,timeout=240)
(out/'gpu.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);result.check_returncode();assert 'SHARED_GPU_NATIVE_PASS' in result.stdout
exact_output=result.stdout
rows=[];summary={}
if a.benchmark:
 for pair in range(a.pairs+2):
  for mode in (('separate','shared') if pair%2==0 else ('shared','separate')):
   guard.processes();result=subprocess.run([*base,mode],cwd=out,env=env,text=True,capture_output=True,timeout=180)
   (out/f'timing-{pair}-{mode}.log').write_text(result.stdout+result.stderr);result.check_returncode()
   row=dict(re.findall(r'(\w+)=([^\s]+)',next(line for line in result.stdout.splitlines() if line.startswith('SHARED_GPU_NATIVE_TIMING'))));row.update(pair=pair-2,mode=mode);rows.append(row)
 for mode in ('separate','shared'):
  summary[mode]={key:statistics.median(float(r[key]) for r in rows if r['mode']==mode and r['pair']>=0) for key in ('cpuMs','uploads','peak','fallback')}
assert all(sha(q)==value for q,value in inputs.items()),'Source changed during validation'
report={'pass':True,'game_launched':False,'GPU_used':True,'meshes':count,'sources':sources,'fixture_sha256':sha(fixture),'compiler':compiler.provenance,'command':command,'backend':str(backend),'backend_sha256':sha(backend),'source_sha256':inputs,'stdout':exact_output,'rows':rows,'summary':summary,'limitation':'Authored MRT/cutout/current deformation and camera workload on stock geometry, not a complete game frame or measured game FPS.'}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(summary,indent=2))
