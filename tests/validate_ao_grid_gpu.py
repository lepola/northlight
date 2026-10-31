#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Exact AO kernel comparisons with authored inputs, without duration queries or the game."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts/shaders'),str(ROOT/'tests')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly
from windows_gpu_guard import processes

def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref',required=True);p.add_argument('--backend',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 assert os.name=='nt';processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 compiler=Compiler(allow_compatible_binary=True);metadata={};runs=[]
 for mode in ('old','new'):
  folder=out/mode;folder.mkdir(exist_ok=True)
  raw=subprocess.check_output(['git','show',a.baseline_ref+':shaders/effects.hlsl'],cwd=ROOT) if mode=='old' else (fp.SHADERS/'effects.hlsl').read_bytes()
  (folder/'effects.hlsl').write_bytes(raw)
  metadata[mode]={'source_sha256':hashlib.sha256(raw).hexdigest(),'entries':{}}
  for method in ('HDAO','HBAOPlus'):
   for quality in ('','Medium','Low'):
    entry=method+'ContactBloom'+quality;binary=folder/(entry+'.bin');compiler.compile(folder/'effects.hlsl',entry,'ps_3_0',binary)
    metadata[mode]['entries'][entry]={'shader_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),**validate_assembly(Path(str(binary)+'.asm'),'ps_3_0')}
 exe=out/'ao-grid.exe';subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.test_include_flags(),str(ROOT/'tests/ao_grid_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
 (out/'dxvk.conf').write_bytes(b'd3d9.presentInterval = 0\n')
 env=os.environ.copy();env.update(DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out),DXVK_SHADER_CACHE_PATH=str(out/'shader-cache'),DXVK_STATE_CACHE_PATH=str(out/'shader-cache'),DISABLE_VULKAN_OBS_CAPTURE='1')
 for entry in metadata['new']['entries']:
  processes();r=subprocess.run([str(exe),str(out/'old'/(entry+'.bin')),str(out/'new'/(entry+'.bin')),str(a.backend.resolve())],cwd=out,env=env,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
  (out/(entry+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');cases=[json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
  runs.append({'entry':entry,'cases':cases,'returncode':r.returncode})
  assert r.returncode==0,r.stdout+r.stderr
  assert cases and all(c['changed']==0 and c['nonfinite']==0 and not c['timings'] for c in cases)
 report={'baseline_ref':a.baseline_ref,'backend_sha256':hashlib.sha256(a.backend.read_bytes()).hexdigest(),'compiler':compiler.provenance,'metadata':metadata,'runs':runs,'game_launched':False,'benchmark':False}
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print('AO_GRID_MATRIX_PASS entries=%u cases=%u timings=0'%(len(runs),sum(len(r['cases']) for r in runs)))
if __name__=='__main__':main()
