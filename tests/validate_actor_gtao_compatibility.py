# northlight-test: requires=windows-exe,zig manual
"""Compile both independent actor AO and the base-GTAO compatibility matrix.

--run-gpu keeps the existing ray-truth, color, dispatch and contact assertions.
The archived base algorithm is project source from PR #118, not game data.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly

def main():
 parser=argparse.ArgumentParser(__doc__);parser.add_argument('--run-gpu',action='store_true');parser.add_argument('--backend',type=Path);args=parser.parse_args()
 output=fp.output_dir()/'actor-gtao-compatibility';output.mkdir(parents=True,exist_ok=True)
 compiler=Compiler(allow_compatible_binary=True);original=json.loads((fp.SHADERS/'shader-build.json').read_text())
 names=['AO','HBAOPlusContactBloom','GTAOContactBloom','HBAOActorContactBloom','GTAOActorContactBloom','HBAOActorOnly','GTAOActorOnly','AOActorPrepare','AOBlur'];actor={n for n in names if 'Actor' in n}
 effects=(fp.SHADERS/'effects.hlsl').read_text();replacement=(fp.TESTS/'fixtures/gtao_isolated_horizon.hlsl').read_text()
 start=effects.index('float NewAO(');end=effects.index('{',start)+1;depth=1
 while depth:depth+=(effects[end]=='{')-(effects[end]=='}');end+=1
 reports=[]
 for mode in ['legacy','isolated']:
  root=output/mode;compiled=root/'compiled';compiled.mkdir(parents=True,exist_ok=True)
  source=effects if mode=='legacy' else effects[:start]+replacement+effects[end:]
  (root/'effects.hlsl').write_text(source,newline='\n');(root/'ao_actor.hlsl').write_bytes((fp.SHADERS/'ao_actor.hlsl').read_bytes());manifest={'source_sha256':hashlib.sha256((root/'effects.hlsl').read_bytes()).hexdigest(),'actor_source_sha256':hashlib.sha256((root/'effects.hlsl').read_bytes()+b'\n'+(root/'ao_actor.hlsl').read_bytes()).hexdigest(),'compatibility_mode':mode,**compiler.provenance,'shaders':{}}
  for entry in names:
   dst=compiled/(entry+'.bin');compiler.compile(root/('ao_actor.hlsl' if entry in actor else 'effects.hlsl'),entry,'ps_3_0',dst)
   gate=validate_assembly(Path(str(dst)+'.asm'),'ps_3_0');digest=hashlib.sha256(dst.read_bytes()).hexdigest();manifest['shaders'][entry]={'sha256':digest,**gate}
   if mode=='legacy' or entry in ['AO','HBAOPlusContactBloom','AOBlur','AOActorPrepare','HBAOActorContactBloom','HBAOActorOnly']:assert digest==original['shaders'][entry]['sha256'],(mode,entry,'unexpected baseline drift')
  (root/'shader-build.json').write_text(json.dumps(manifest,indent=2)+'\n',newline='\n');reports.append({'mode':mode,'shader_count':len(names),'SM3_limits':'pass','legacy_payloads_unchanged':mode=='legacy'})
  if args.run_gpu:
   import windows_gpu_guard
   windows_gpu_guard.processes();cmd=[sys.executable,str(fp.TESTS/'test_ao_actor_gpu.py'),'--run-gpu','--compiled',str(compiled)]
   if args.backend:cmd+=['--backend',str(args.backend)]
   env=os.environ.copy();env['NORTHLIGHT_OUT']=str(root/'gpu');subprocess.run(cmd,check=True,env=env)
 (output/'validation.json').write_text(json.dumps({'compile_pass':True,'gpu_executed':args.run_gpu,'cases':reports},indent=2)+'\n');print('ACTOR_GTAO_COMPATIBILITY_COMPILE_PASS',output,flush=True)
if __name__=='__main__':main()
