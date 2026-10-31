#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Genuine old/new High GTAO pixels, ABBA timing, and a disabled-AO negative control."""
import argparse,hashlib,importlib.util,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly

def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref',required=True);p.add_argument('--backend',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--benchmark',action='store_true');a=p.parse_args()
 assert os.name=='nt';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 import windows_gpu_guard as guard
 guard.processes()
 sha=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
 source=fp.SHADERS/'effects.hlsl';fixture=ROOT/'tests/gtao_footprint_gpu.cpp';backend=a.backend.resolve()
 report={'pass':False,'baseline_ref':a.baseline_ref,'inputs':{str(q):sha(q) for q in (source,fixture,backend,Path(__file__).resolve(),ROOT/'tests/composite_distance_gpu.cpp',ROOT/'tests/removal_distance_gpu.cpp')},'commands':[],'cases':[],'gpu_busy_time_claimed':False,'game_launched':False}
 env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
 for k,n in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
  (out/n).mkdir(exist_ok=True);env[k]=str(out/n)
 (out/'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
 env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
 def run(label,command,expect_success=True):
  guard.processes();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,timeout=240);(out/(label+'.log')).write_bytes((result.stdout+result.stderr).encode());report['commands'].append({'label':label,'exit_code':result.returncode});print(label,result.returncode,flush=True)
  if expect_success:assert result.returncode==0,result.stdout+result.stderr
  return result
 try:
  old=out/'old.hlsl';old.write_bytes(subprocess.run(['git','show',a.baseline_ref+':shaders/effects.hlsl'],cwd=ROOT,capture_output=True,check=True).stdout)
  new=out/'new.hlsl';new.write_bytes(source.read_bytes());report['baseline_source_sha256']=sha(old)
  compiler=Compiler(allow_compatible_binary=True);report['compiler']=compiler.provenance
  for mode in ('old','new'):
   shader=out/(mode+'.bin');compiler.compile(out/(mode+'.hlsl'),'GTAOContactBloom','ps_3_0',shader);report[mode]={'sha256':sha(shader),**validate_assembly(Path(str(shader)+'.asm'),'ps_3_0')}
  assert report['new']['sha256']==sha(fp.COMPILED/'GTAOContactBloom.bin'),'Production artifact does not match tested source'
  exe=out/'gtao-footprint.exe';run('compile-fixture',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type',*fp.test_include_flags(),'-I'+str(ROOT/'tests'),fixture,'-luser32','-o',exe])
  sizes=[(256,192,16),(137,79,32)]+([(1920,1080,16),(3840,2160,16)] if a.benchmark else [])
  for w,h,bits in sizes:
   result=run(f'gpu-{w}-{bits}',[exe,out/'old.bin',out/'new.bin',backend,w,h,bits]);cases=[json.loads(x) for x in result.stdout.splitlines() if x.startswith('{')];assert len(cases)==9 and all(x['changed_bytes']==0 and x['nonfinite']==0 for x in cases);report['cases']+=cases
  assert any(c['min_ao']<.995 for c in report['cases']),'Fixture contains no real occlusion'
  mutant=out/'disabled.hlsl';mutant.write_bytes(new.read_bytes()+b'\nfloat4 DisabledAO(float2 uv:TEXCOORD0):COLOR0{return float4(ContactBloomRgb(uv),1); }\n');compiler.compile(mutant,'DisabledAO','ps_3_0',out/'disabled.bin')
  result=run('negative-disabled-ao',[exe,out/'old.bin',out/'disabled.bin',backend,256,192,16],False);rows=[json.loads(x) for x in result.stdout.splitlines() if x.startswith('{')];assert result.returncode!=0 and any(x['changed_bytes']>0 for x in rows),'Disabled AO escaped the pixel oracle';report['negative_control_rejected']=True;report['pass']=True
 finally:
  report['inputs_unchanged']=all(sha(Path(q))==v for q,v in report['inputs'].items());report['pass'] &= report['inputs_unchanged'];(out/'validation.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
 assert report['pass'];print('PASS High GTAO exact pixels, disabled-AO rejection, and paired elapsed query timings')

if __name__=='__main__':main()
