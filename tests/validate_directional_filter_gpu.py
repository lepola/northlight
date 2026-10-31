#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Exact shared directional PCF outputs; authored inputs, no timing queries or game."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly
from windows_gpu_guard import processes
WRAPPER="""\nfloat4 FilterOracle(float2 uv:TEXCOORD0):COLOR0{float3 p=float3(uv.x*2-1,1-uv.y*2,tex2Dlod(Depth,float4(uv,0,0)).r);bool nearMap=FogInfo.x>1.5?uv.x>.5:FogInfo.x>.5;float v=shadowMap(p,nearMap,1,FogInfo.yz);return float4(v,v*.37,v*.71,v); }\n"""
def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref',required=True);p.add_argument('--backend',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 assert os.name=='nt';processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 compiler=Compiler(allow_compatible_binary=True);metadata={}
 for mode in ('old','new'):
  folder=out/mode;folder.mkdir(exist_ok=True)
  raw=subprocess.check_output(['git','show',a.baseline_ref+':shaders/world_effects.hlsl'],cwd=ROOT) if mode=='old' else (fp.SHADERS/'world_effects.hlsl').read_bytes()
  (folder/'world_effects.hlsl').write_bytes(raw+WRAPPER.encode());
  if b'#include \"auto_bump_normal.hlsl\"' in raw:(folder/'auto_bump_normal.hlsl').write_bytes((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes())
  binary=folder/'FilterOracle.bin';compiler.compile(folder/'world_effects.hlsl','FilterOracle','ps_3_0',binary)
  metadata[mode]={'source_sha256':hashlib.sha256(raw).hexdigest(),'shader_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),**validate_assembly(Path(str(binary)+'.asm'),'ps_3_0')}
 exe=out/'directional-filter.exe';subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.test_include_flags(),str(ROOT/'tests/directional_filter_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
 (out/'dxvk.conf').write_bytes(b'd3d9.presentInterval = 0\n')
 env=os.environ.copy();env.update(DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out),DXVK_SHADER_CACHE_PATH=str(out/'shader-cache'),DXVK_STATE_CACHE_PATH=str(out/'shader-cache'),DISABLE_VULKAN_OBS_CAPTURE='1')
 processes();result=subprocess.run([str(exe),str(out/'old/FilterOracle.bin'),str(out/'new/FilterOracle.bin'),str(a.backend.resolve())],cwd=out,env=env,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
 (out/'gpu.log').write_text(result.stdout+result.stderr,encoding='utf-8');cases=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
 report={'baseline_ref':a.baseline_ref,'backend_sha256':hashlib.sha256(a.backend.read_bytes()).hexdigest(),'compiler':compiler.provenance,'metadata':metadata,'cases':cases,'game_launched':False,'benchmark':False}
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 assert result.returncode==0,result.stdout+result.stderr
 assert cases and all(c['changed']==0 and c['nonfinite']==0 and not c['timings'] for c in cases)
 print(result.stdout.splitlines()[-1])
if __name__=='__main__':main()
